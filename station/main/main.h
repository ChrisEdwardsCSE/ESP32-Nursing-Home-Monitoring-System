#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define ESP_WIFI_SSID "SpectrumSetup-DB"
#define ESP_WIFI_PASS "superpear962"

#define MAX_NUM_RESIDENTS 100

#define RES_DATA_BUF_SIZE 10

#define HR_LOWER_THRESHOLD 60
#define HR_UPPER_THRESHOLD 130

static void log_error_if_nonzero(const char *message, int error_code);
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
void wifi_mqtt_send_msg(void*);
void wifi_init_sta(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_app_start(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
void ble_app_scan(void);
void ble_app_on_sync(void);
void host_task(void *param);

#endif