#ifndef ADXL345_I2C_H_
#define ADXL345_ISC_H_

// Datasheet https://www.analog.com/media/en/technical-documentation/data-sheets/adxl345.pdf

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ADXL345 default I2C address
#define ADXL_DEV_I2C_ADDR 0x53
// ADXL345 register addresses
#define ADXL_POWER_CTL_REG 0x2d
#define ADXL_DATA_FORMAT_REG 0x21
#define ADXL_DATA_XOUT_REG 0x32
#define ADXL_INT_ENABLE_REG 0x2e
#define ADXL_INT_MAP_REG 0x2f
#define ADXL_INT_THRESH_TAP_REG 0x1d
#define ADXL_INT_THRESH_DUR_REG 0x21
#define ADXL_INT_THRESH_FF_REG 0x28
#define ADXL_INT_TIME_FF_REG 0x29
#define ADXL_TAP_AXES 0x2a
#define ADXL_INT_SOURCE 0x30

// Set ADXL345 connection pins
#define ADXL_I2C_SCL GPIO_NUM_15
#define ADXL_I2C_SDA GPIO_NUM_16
#define ADXL_IO_INT1 GPIO_NUM_5

#define ADXL_DEBUG 0

/**
 * Clears the SINGLE_TAP interrupt by reading the interrupt source register
 */
void adxl345_int_tap_clear(i2c_master_dev_handle_t master_dev_handle,
                           i2c_master_bus_handle_t master_bus_handle);

/**
 * Configures ADXL in Measure Mode & formats data
 */
void adxl345_init(i2c_master_dev_handle_t master_dev_handle,
                  i2c_master_bus_handle_t master_bus_handle);

/**
 * Set up the interrupts on SINGLE_TAP and FREE_FALL occurrences.
 * SINGLE_TAP on INT1 IO pin; FREE_FALL on INT2 IO pin
 *
 * @param master_dev_handle I2C master device handler instance
 * @param int_tap_thresh treshold minimum acceleration magnitude (mg) for
 * TAP interrupt to occur. Range 0 to 16000mg
 * @param int_thresh_dur treshold maximum duration of acceleration (ms) for
 * TAP interrupt to trigger. Range 0 to 160ms
 * @param int_thresh_ff threshold maximum acceleration magnitude (mg) for
 * FF interrupt to trigger. Recommended 300mg - 600mg
 * @param int_time_ff threshold minimum time (ms) for FF interrupt to trigger.
 *  Recommended 100ms - 350ms
 */
void adxl345_interrupt_init(i2c_master_dev_handle_t master_dev_handle, i2c_master_bus_handle_t master_bus_handle,
                            uint16_t int_thresh_tap, uint8_t int_thresh_dur, uint16_t int_thresh_ff, uint16_t int_time_ff);

#endif