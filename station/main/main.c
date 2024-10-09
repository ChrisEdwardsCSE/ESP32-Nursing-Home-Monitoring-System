/**
 * Main for Station in Nursing Home Monitoring System
 * 
 * Author: Christopher Edward 
 * Created September 8, 2024
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "sdkconfig.h"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

#include "mqtt_client.h"

#include "driver/gpio.h"
#include "hal/i2c_types.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "driver/i2c_slave.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "main.h"


QueueHandle_t q_res_data; // Queue to hold incoming Resident data

TaskHandle_t mqtt_app_start_task_handle;
TaskHandle_t wifi_mqtt_send_msg_handle;

static EventGroupHandle_t s_wifi_event_group; // Event group for WiFi events

esp_mqtt_client_handle_t mqtt_client;

// WiFi Event group significant bits for s_wifi_event_group
#define WIFI_SUCCESS    1 << 0
#define WIFI_FAIL       1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAIL 1 << 1

#define MAX_FAILURES 10 // Maximum number of connection attempts

// Tags for ESP_LOGI statements
static const char *BLE_TAG = "BLE";
static const char *WIFI_TAG = "WIFI";

static uint8_t wifi_connect_attempt_count = 0; // Counts number of esp_wifi_connect() attempts

uint8_t ble_addr_type; // Holds automatically inferred address type of BLE device

// Struct to hold resident data
struct res_data_t {
    char* name;
    uint8_t heart_rate;
    float spo2_level;
    uint8_t fall_detected:1;
} res_data;

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init()); // BLE & WiFi use NVS Flash
    nimble_port_init(); // Initialize NimBLE controller stack
    ble_svc_gap_device_name_set("Smart-Nursing-Home-Station"); // Set device name
    ble_svc_gap_init(); // Initialize GAP 
    ble_hs_cfg.sync_cb = ble_app_on_sync; // Set callback for Host & Controller sync
    nimble_port_freertos_init(host_task); // Set infinite task

    q_res_data = xQueueCreate(7, 50);

    wifi_init_sta(); // Configure and initialize WiFi
}


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(WIFI_TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * WiFi event handler
 * @param event_base - base type of event, should be WIFI_EVENT
 * @param event_id - the event that occurred
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    // If WiFi-related event & it's a station start
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect(); // Connect to WiFi, eventually calls ip_event_handler
        ESP_LOGI(WIFI_TAG, "Connecting to AP...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_connect_attempt_count < MAX_FAILURES) { // Retry to connect MAX_FAILURES times
            esp_wifi_connect();
            wifi_connect_attempt_count++;
            ESP_LOGI(WIFI_TAG, "Reconnecting to AP...");
        } else { // If number of tries > MAX_FAILURES, then connection failed. Set event group
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL);
            ESP_LOGI(WIFI_TAG, "Connection to AP failed");
        }
    }
    else {
        ESP_LOGI(WIFI_TAG, "Wifi Event: %ld", event_id);
    }
}

esp_err_t esp_error;

/**
 * IP event handler
 * @param event_base - base type of event, should be IP_EVENT
 * @param event_id - the IP event that occurred
 */
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connect_attempt_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_SUCCESS);
    }
    else {
        ESP_LOGI(WIFI_TAG, "IP Event: %ld", event_id);
    }
}

/**
 * Task that connects to WiFi, sends MQTT message, gracefully disconnects
 * after successful transmission of resident data, and restarts BLE scanning.
 */
void wifi_mqtt_send_msg(void*)
{
    uint8_t first_time = 1; // First time connecting to WiFi flag
    for(;;) {
        s_wifi_event_group = xEventGroupCreate();
    
        // Establish wifi event event handler: If see WIFI_EVENT, then call wifi_event_handler
        esp_event_handler_instance_t wifi_event_handler_instance;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &wifi_event_handler_instance));

        /**
         * Establish IP event event handler: If IP_EVENT_STA_GOT_IP (WiFi Station gets an IP),
         * then call ip_event_handler. The ip_event_handler gets called sometime after we've connected
         * to WiFi */ 
        esp_event_handler_instance_t ip_event_handler_instance;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        &ip_event_handler_instance));

        /**
         * Take for Notification 0. Wait until BLE Observer picks up a signal from a resident's device.
         * Blocks after we recreate the event group and handlers for WiFi and IP events but before
         * we connect/reconnect to WiFi
         */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (first_time) {
            esp_wifi_start(); // Start (connect for the first time) the WiFi driver
            ESP_LOGI(WIFI_TAG, "esp_wifi_start: %d", (int)esp_error);
        }
        else {
            esp_error = esp_wifi_connect(); // Reconnect the WiFi driver to WiFi
            ESP_LOGI(WIFI_TAG, "esp_wifi_connect: %d", (int)esp_error);
        }

        /* Wait in BLOCKED for either WIFI_SUCCESS or WIFI_FAIL events to occur. 
        * These get set by wifi_event_handler() */
        EventBits_t wifi_result_bits = xEventGroupWaitBits(
                s_wifi_event_group,         // Event group
                WIFI_SUCCESS | WIFI_FAIL,   // Bits to wait for
                pdFALSE,                    // Don't clear bits on exit
                pdFALSE,                    // Any of the specified bits unblock the task 
                portMAX_DELAY);

        // TODO: Add WiFi error handling
        if (wifi_result_bits & WIFI_SUCCESS) {
            ESP_LOGI(WIFI_TAG, "connected to ap SSID:%s password:%s",
                    ESP_WIFI_SSID, ESP_WIFI_PASS);
        } else if (wifi_result_bits & WIFI_FAIL) {
            ESP_LOGI(WIFI_TAG, "Failed to connect to SSID:%s, password:%s",
                    ESP_WIFI_SSID, ESP_WIFI_PASS);
        } else {
            ESP_LOGE(WIFI_TAG, "UNEXPECTED EVENT");
        }

        mqtt_app_start();
        
        ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY); // Wait for MQTT to connect and publish

        esp_mqtt_client_stop(mqtt_client);
        
        // Unregister WiFi handlers and event group before disconnecting WiFI
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler_instance));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler_instance));
        vEventGroupDelete(s_wifi_event_group);

        esp_error = esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(10));

        ble_app_scan(); // Restart BLE Scanning now that we've serviced resident data MQTT message

        first_time = 0;
    }
}

/**
 * Initialize WiFi as station and create task for connection, disconnection
 * and MQTT publishing between BLE scanning.
 */
void wifi_init_sta(void)
{   
    // TODO: Handle if attempting to connect to a new WiFi

    ESP_ERROR_CHECK(esp_netif_init()); // Initialize TCP/IP stack

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_t* esp_netif = esp_netif_create_default_wifi_sta(); // Create default WiFi station

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); 

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
            * However these modes are deprecated and not advisable to be used. Incase your Access point
            * doesn't support WPA2, these mode can be enabled by commenting below line */
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // Set WiFi to be a station
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // Configure WiFi
    
    // Create ask to connect to WiFi, send MQTT messages, and disconnect between BLE readings
    xTaskCreate(wifi_mqtt_send_msg, "Connect WiFi and Send MQTT",
                8192, NULL, configMAX_PRIORITIES-1, &wifi_mqtt_send_msg_handle);
    
}

/**
 * MQTT event handler
 * 
 * @param handler_args - optional arguments along with event
 * @param base - the base of the event; should be MQTT_EVENT
 * @param event_id - identifier of the event to handle
 * @param event_data - any event_data that came with event
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    static uint8_t s_mqtt_published_flag = 0;
    ESP_LOGD(WIFI_TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    char mqtt_send_data_buf[50];
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        xQueueReceive(q_res_data, mqtt_send_data_buf, pdMS_TO_TICKS(100)); // Get resident data off the queue

        msg_id = esp_mqtt_client_publish(client, "residents", (char *)mqtt_send_data_buf, 50, 1, 0); // Publish it

        ESP_LOGI(WIFI_TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (!s_mqtt_published_flag) {
            ESP_LOGI(WIFI_TAG, "Disconnected without publishing");
            // TODO: Handle republishing
        }
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(WIFI_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        s_mqtt_published_flag = 1;

        // Published. Relieve WiFi and restart BLE scanning
        xTaskNotifyGiveIndexed(wifi_mqtt_send_msg_handle, 1);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(WIFI_TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(WIFI_TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(WIFI_TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/**
 * Creates and starts the client as well as event handler
 */
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg); // Create MQTT client

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL); // Register MQTT event handler cb

    esp_mqtt_client_start(mqtt_client);

}


uint8_t g_res_arr[MAX_NUM_RESIDENTS]; // DS to store whether we've received BLE signal for resident with ID
/**
 * BLE GAP event handler. 
 * 
 * @param event - the BLE GAP event detected
 * @param arg - optional arguments along with the event
 */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;

    char res_data_buf[50]; // Buffer to hold resident data to send via MQTT

    switch (event->type)
    {
        // Discovery event (received advertisement)
        case BLE_GAP_EVENT_DISC:
            ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);

            /**
             * name = "Smart-Nursing-Home-Device"
             * mfg_data = Resident's Name
             * mfd_data_len = Length of Resident's Name
             * uint8_t le_role = Measured Heart Rate
             * unsigned le_role_is_present = Fall Detection flag
             * uint8_t *uri = oxygen level "XX.X%"
             */
            if (strncmp("Smart-Nursing-Home-Device", (char *)fields.name, (size_t)fields.name_len)) {
                /*** Parse the fields into a message to send ***/
                // "First Last, ID, <HR>, 1";
                snprintf(res_data_buf, 50, "%*s, %u, %u, %u", fields.mfg_data_len, fields.mfg_data, fields.uri_len, fields.le_role, fields.le_role_is_present);

                xQueueSend(q_res_data, res_data_buf, pdMS_TO_TICKS(100));
                
                ble_gap_disc_cancel();
                vTaskDelay(pdMS_TO_TICKS(10));
                xTaskNotifyGive(wifi_mqtt_send_msg_handle);
            }
            break;
        default:
            break;
    }
    return 0;
}

/**
 * Configure and Initialize GAP service.
 */
void ble_app_scan(void)
{
    // Define GAP connectivity
    struct ble_gap_disc_params disc_params;
    disc_params.filter_duplicates = 1;  // No duplicates
    disc_params.passive = 0;            // No Scan-Request service
    disc_params.itvl = 0;               // Default scan intervals
    disc_params.window = 0;             // Default window intervals
    disc_params.filter_policy = 0;
    disc_params.limited = 0;            // Not limited discovery

    /**
     * Handles discovery procedure and sets discovery event callback function (ble_gap_event).
     * Does all the parts of configuring the RF receiver, setting channel, initializing 
     * specifications, etc.
     */
    ble_gap_disc(ble_addr_type, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
    
}

/**
 * Begin BLE app
 */
void ble_app_on_sync(void)
{
    /* Determines the best address type, sets ble_addr_type with it*/
    int res = ble_hs_id_infer_auto(0, &ble_addr_type);
    if (res != 0)
    {
        printf("Device Address Set Error: %d", res);
    }
    ble_app_scan();
}

/**
 * Start the NimBLE (BLE Host) infinite task
 */
void host_task(void *param)
{
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
}

