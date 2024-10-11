#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "sdkconfig.h"

#include "drivers/inc/adxl345-i2c.h"
#include "drivers/inc/max30102-i2c.h"
#include "drivers/inc/algorithm_by_RF.h"

#include "main.h"

const char *BLE_TAG = "BLE"; // Tag for ESP_LOGI logging

// ADXL345 I2C Master handles
i2c_master_dev_handle_t adxl_master_dev_handle;
i2c_master_bus_handle_t adxl_master_bus_handle;

// MAX30102 I2C Master handle
i2c_master_dev_handle_t max_master_dev_handle;

TaskHandle_t sample_hr_handle;

uint8_t ble_addr_type;

uint8_t green_led_val = 1;

TaskHandle_t ble_advertise_task_handle;

QueueHandle_t q_heartrate; // Queue to transfer heart rate readings between tasks

struct resident_data {
    uint8_t heart_rate;
    float oxygen_level;
    uint8_t fall_detected:1;
};
struct resident_data res_data;

// Buffers to MAX30102 heart rate and oxygen level readings
uint32_t fifo_hr_buffer[BUFFER_SIZE];
uint32_t fifo_spo2_buffer[BUFFER_SIZE];


/**
 * ISR for Physical Trauma event from ADXL345
 */
static void IRAM_ATTR adxl_int1_isr_handler(void *arg)
{
    
    adxl345_int_tap_clear(adxl_master_dev_handle, adxl_master_bus_handle); // Clear interrupts
    
    res_data.fall_detected = 1; // Set Fall Detection to 1

    gpio_set_level(GPIO_NUM_1, green_led_val);
    green_led_val = !green_led_val;

    vTaskNotifyGiveFromISR(ble_advertise_task_handle, NULL); // Start advertising emergency with Fall Detection set
}

void app_main()
{
    nvs_flash_init(); // Initialize NVS flash using
    nimble_port_init(); // Initialize the controller stack
    ble_svc_gap_device_name_set("ESP32-Ched"); // Set device name characteristic
    ble_svc_gap_init(); // Initialize GAP service
    ble_hs_cfg.sync_cb = ble_app_on_sync; // Set application as callback
    nimble_port_freertos_init(host_task); // Set infinite task

    vTaskDelay(pdMS_TO_TICKS(10));
    
    gpio_config_adxl_int(); // Initialize GPIO pins of ADXL345 (and MAX30102)
    i2c_config_adxl(); // Configure I2C peripheral for ADXL345
    i2c_config_max(); // Configure I2C peripheral for MAX30102

    vTaskDelay(pdMS_TO_TICKS(10));

    // Start heart rate sampling task
    BaseType_t sample_hr_err = xTaskCreate(sample_hr, "Sample Heart Rate", 8192, NULL, 
                configMAX_PRIORITIES-2, &sample_hr_handle);
    if (sample_hr_err != pdPASS) {
        ESP_LOGI(BLE_TAG, "Task Create failed!!!: %d", sample_hr_err);
    }
}

/**
 * Handle GAP events. No real events to handle apart from errors since
 * device only broadcasts
 */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    printf("ble_gap_event() called\n");
    // the different connection events we could have
    switch (event->type)
    {
        case BLE_GAP_EVENT_TERM_FAILURE:
            ESP_LOGI(BLE_TAG, "Termination Failure");
        default:
            break;
    }
    return 0;
}

/**
 * Set and begin advertisements
 */
void ble_app_advertise(void *)
{
    const char *device_name = ble_svc_gap_device_name(); // Read device name
    
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON; // Unconnectable; just broadcasting
    adv_params.disc_mode = BLE_GAP_DISC_MODE_NON; // Undiscoverable; don't accept scan requests

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Wait for notification[0] - Send out BLE advertisement

        /**
         * name = "Smart-Nursing-Home-Device"
         * uri_len = Resident ID
         * uint8_t le_role = Measured Heart Rate
         * unsigned le_role_is_present = Fall Detection flag
         * uint8_t *uri = oxygen level "XX.X%"
         */
        // Set fields according to the above mappnig
        // const uint8_t resident_name[9] = "John Doe";
        // fields.mfg_data = resident_name;
        // fields.mfg_data_len = 9;
        fields.uri_len = 0; // id
        fields.le_role = (uint8_t)res_data.heart_rate;
        fields.le_role_is_present = res_data.fall_detected;

        // Configure and begin advertising for 30 seconds
        ble_gap_adv_set_fields(&fields);
        ESP_LOGI(BLE_TAG, "name: %*s", fields.name_len, fields.name);
        int rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
        ESP_LOGI(BLE_TAG, "Started GAP advertising: %d", rc);

        vTaskDelay(pdMS_TO_TICKS(10000));

        rc = ble_gap_adv_stop();
        ESP_LOGI(BLE_TAG, "Stopped GAP advertising: %d", rc);

        res_data.fall_detected = 0; // Reset fall detected
    }
}

/**
 * Set address and create BLE advertising task on Host & Controller sync
 */
void ble_app_on_sync(void)
{
    /* Determines the best address type, sets ble_addr_type with it*/
    int res = ble_hs_id_infer_auto(0, &ble_addr_type);
    if (res != 0)
    {
        printf("Device Address Set Error: %d", res);
    }

    xTaskCreate(ble_app_advertise, "Advertise",
                4096, NULL, configMAX_PRIORITIES - 1,
                &ble_advertise_task_handle);
}

/**
 * Start the NimBLE (BLE Host) infinite task
 */
void host_task(void *param)
{
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
}

/**
 * Sample resident's heart rate
 */
void sample_hr(void*)
{
    int32_t heart_rate;
    int8_t heart_rate_valid, hr_send_wait_count;
    hr_send_wait_count = 0;
    float pn_spo2;
    int8_t spo2_valid;
    float ratio, correl;
    res_data.heart_rate = 80;

    for (;;) {
        // Take samples
        for (int i = 0; i < BUFFER_SIZE; i++)
        {   
            max30102_fifo_read(max_master_dev_handle, &fifo_hr_buffer[i], &fifo_spo2_buffer[i]);
        }
        // Calculate Heart Rate
        rf_heart_rate_and_oxygen_saturation(fifo_spo2_buffer, BUFFER_SIZE, 
                                    fifo_hr_buffer, &pn_spo2, &spo2_valid, &heart_rate, 
                                    &heart_rate_valid, &ratio, &correl);
        // res_data.heart_rate = heart_rate;

        // If abnormal heart rate, send immediately to Station via BLE
        if ( (res_data.heart_rate <= HR_LOWER_THRESHOLD) || (res_data.heart_rate >= HR_UPPER_THRESHOLD) ) {
            ESP_LOGI(BLE_TAG, "abnormal heart rate detected");
            xTaskNotifyGive(ble_advertise_task_handle);
            hr_send_wait_count = 0;
        }
        // else {
        //     hr_send_wait_count++;
        //     // Every 10 seconds update Station with normal Heart Rate reading
        //     if (hr_send_wait_count == 10) {
        //         xTaskNotifyGive(ble_advertise_task_handle);
        //         hr_send_wait_count = 0;
        //     }
        // }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Take Heart Rate reading every 1 second
    }
}



/**
 * Configures the ADXL345 for fall detection
 */
void i2c_config_adxl()
{
    i2c_master_bus_config_t adxl_master_bus_config = {
        .i2c_port = -1,
        .scl_io_num = ADXL_I2C_SCL,
        .sda_io_num = ADXL_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .trans_queue_depth = 20,
        .flags = {1} // enable pull up
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&adxl_master_bus_config, &adxl_master_bus_handle));

    i2c_device_config_t adxl_device_config = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = ADXL_DEV_I2C_ADDR, // I2C device address when SDO cnctd to GND
        .scl_speed_hz = 100000  // ADXL either 100kHz or 400kHz
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(adxl_master_bus_handle, &adxl_device_config, &adxl_master_dev_handle));

    ESP_LOGI(BLE_TAG, "b4 init");
    adxl345_init(adxl_master_dev_handle, adxl_master_bus_handle);
    ESP_LOGI(BLE_TAG, "after init");
    adxl345_interrupt_init(adxl_master_dev_handle, adxl_master_bus_handle, 2000, 100, 800, 200);
    ESP_LOGI(BLE_TAG, "after int enable");
    /* TESTING LEDs for ADXL interrupts */
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(GPIO_NUM_1, GPIO_PULLUP_PULLDOWN);
}
/**
 * Configures INT1 GPIO pin of ADXL345
 */
void gpio_config_adxl_int(void)
{
    gpio_reset_pin(ADXL_IO_INT1);

    gpio_config_t adxl_int1_gpio_config = {
        .pin_bit_mask = (1 << ADXL_IO_INT1),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = 1,
        .intr_type = GPIO_INTR_POSEDGE, // rising edge interrupt
    };
    gpio_config(&adxl_int1_gpio_config);


    gpio_install_isr_service(0);
    gpio_isr_handler_add(ADXL_IO_INT1, adxl_int1_isr_handler, NULL);

    gpio_set_level(ADXL_IO_INT1, 0);

    gpio_intr_enable(ADXL_IO_INT1);
}

/**
 * Configure the I2C communication for the MAX30102
 */
void i2c_config_max(void)
{
    /* Configure device */
    i2c_device_config_t max_device_config = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = MAX_DEV_I2C_ADDR, // I2C default device address
        .scl_speed_hz = 100000
    };

    i2c_master_bus_add_device(adxl_master_bus_handle, &max_device_config, &max_master_dev_handle);

    max30102_init(max_master_dev_handle);

    max30102_int_handle(max_master_dev_handle); // Handle the Power Up interrupt
}

