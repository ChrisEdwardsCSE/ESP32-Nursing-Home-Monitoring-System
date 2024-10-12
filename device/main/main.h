#ifndef MAIN_H_
#define MAIN_H_

// Heart Rate threshold for abnormal values in BPM
#define HR_LOWER_THRESHOLD 60
#define HR_UPPER_THRESHOLD 130

#define BLE_ADV_DURATION 3000 // BLE Advertising duration 

static int ble_gap_event(struct ble_gap_event *event, void *arg);
void ble_app_advertise(void *);
void ble_app_on_sync(void);
void host_task(void *param);
void sample_hr(void*);
void i2c_config_adxl();
void gpio_config_adxl_int(void);
void i2c_config_max(void);

#endif