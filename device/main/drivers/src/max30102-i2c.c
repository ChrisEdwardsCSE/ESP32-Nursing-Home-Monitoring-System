/**
 * SCL - <= 400kHz
 * 8b transactions.
 * address = 0xa7 << 1
 * "fixed i2c addresses": 0xae for write, 0xaf for read
 *
 *
 * Power Up - after power up, interrupt occurs to alert that
 * MAX30102 ready. then read the int register to clear it
 *
 * Interrupts - read the interrupt source reg or the data reg to clear
 * an int
 * interrupts are IDLE-HIGH
 */

// https://github.com/eepj/stm32-max30102/blob/master/max30102_for_stm32_hal.c
// https://github.com/aromring/MAX30102_by_RF/blob/master/algorithm_by_RF.cpp
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "../inc/max30102-i2c.h"



#define MAX_DEBUG 0


/**
 * Read and clear all interrupts
 */
void max30102_int_handle(i2c_master_dev_handle_t master_dev_handle)
{
#if MAX_DEBUG
    printf("max30102_int_handle() called\n");
#endif
    /* Read interrupt registers; Clears all interrupts */
    uint8_t int_source_buf[1] = {
        MAX_INT_SOURCE_1_REG};
    uint8_t read_int_buf[2]; // Read both Interrupt Status Registers 1 & 2
    i2c_master_transmit_receive(master_dev_handle, int_source_buf, 1,
                                read_int_buf, 2, 100);
    // if PWR_RDY interrupt
    if (read_int_buf[0] & 0x1)
    {
#if MAX_DEBUG
        printf("max30102_int_handle(): power ready: 0x%X, 0x%X\n", read_int_buf[0], read_int_buf[1]);
#endif
    }
    ESP_LOGI("max30102", "max_int_handle");
}

/**
 * Set to heart rate mode,
 */
void max30102_init(i2c_master_dev_handle_t master_dev_handle)
{
#if MAX_DEBUG
    printf("max30102_init() called\n");
#endif
    /* Set the mode to HR (heart rate) mode */
    uint8_t mode_buf[2] = {
        MAX_MODE_CONFIG_REG,
        (uint8_t)0x02 // HR mode
    };

    i2c_master_transmit(master_dev_handle, mode_buf, 2, 100);

#if MAX_DEBUG
    uint8_t rx_mode_buf[1];
    i2c_master_transmit_receive(master_dev_handle, mode_buf, 1,
                                rx_mode_buf, 1, 100);
    printf("MODE_CONF: 0x%X\n", rx_mode_buf[0]);
#endif

    max30102_config(master_dev_handle);

    max30102_fifo_init(master_dev_handle);
}

/**
 * Configure sampling stuff
 */
void max30102_config(i2c_master_dev_handle_t master_dev_handle)
{
#if MAX_DEBUG
    printf("max301020_config() called\n");
#endif
    /* Set LED Pulse Amplitude of Red LED */
    uint8_t led1_pa_buf[2] = {
        MAX_LED1_PA_REG,
        0x24 // 7mA
    };
    i2c_master_transmit(master_dev_handle, led1_pa_buf, 2, 100);

#if MAX_DEBUG

    uint8_t rx_buf[1];
    i2c_master_transmit_receive(master_dev_handle, led1_pa_buf,
                                1, rx_buf, 1, 100);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    printf("LED1 config: 0x%X\n", rx_buf[0]);
#endif

    /* Set ADC configurations */
    uint8_t config_adc_buf[2] = {
        MAX_CONFIG_SPO2_REG,
        (uint8_t)((0b01 << 5) | (0b001 << 2) | (0b11)) // 4096nA range, 100 samples/s, 411us pulse width
    };

    i2c_master_transmit(master_dev_handle, config_adc_buf, 2, 100);

#if MAX_DEBUG
    i2c_master_transmit_receive(master_dev_handle, config_adc_buf,
                                1, rx_buf, 1, 100);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    printf("SP02 config: 0x%X\n", rx_buf[0]);
#endif
}

/**
 * So FIFO works like, 32 spots for 32 samples of data.
 * A channel provides 3B of data. There are 2 channels: IR & RED.
 * If we're using both, that means a sample can be 2(channels) * 3(Bytes) = 6B per sample
 * The 3B are actually only 18b max, from 15-18b ADC resolutions ([23:18] aren't used)
 */

/**
 * Initialize the FIFO with configurations
 * Gets called upon Power-Up and Brownout
 */
void max30102_fifo_init(i2c_master_dev_handle_t master_dev_handle)
{
    // config fifo
    // enable fifo interrupts
    // reset the write ptr, read ptr, ovf counter
    // clear any interrupts
    uint8_t rx_buf[2];
    /* Set FIFO configurations */
    uint8_t tx_buf[2] = {
        MAX_FIFO_CONFIG_REG,
        (uint8_t)((0b010 << 5) | (1 << 4)) // // 4 samples averaged per FIFO sample; FIFO rollover enabled
    };
    i2c_master_transmit(master_dev_handle, tx_buf, 2, 100);

#if MAX_DEBUG
    printf("max30102_fifo_init() called\n");
    i2c_master_transmit_receive(master_dev_handle, tx_buf,
                                1, rx_buf, 1, 100);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    printf("FIFO config: 0x%X\n", rx_buf[0]);
#endif

    /* Reset FIFO values u*/
    tx_buf[0] = MAX_FIFO_WRITE_PTR;
    tx_buf[1] = 0x00;
    i2c_master_transmit(master_dev_handle, tx_buf, 2, 100);

    tx_buf[0] = MAX_FIFO_OVF_CTR;
    i2c_master_transmit(master_dev_handle, tx_buf, 2, 100);

    tx_buf[0] = MAX_FIFO_READ_PTR;
    i2c_master_transmit(master_dev_handle, tx_buf, 2, 100);

    /* Clear FIFO interrupts by reading source */
    tx_buf[0] = MAX_INT_SOURCE_1_REG;
    i2c_master_receive(master_dev_handle, tx_buf, 1, 100);
}

const char* TAG = "max-i2c";
uint8_t rx_data_buf[6];
/**
 * Read data out of FIFO
 */
void max30102_fifo_read(i2c_master_dev_handle_t master_dev_handle, uint32_t* hr_sample, uint32_t* ir_sample)
{
    rx_data_buf[0] = MAX_FIFO_DATA_REG;
    i2c_master_transmit(master_dev_handle, rx_data_buf, 1, 100); // just sent MAX_FIFO_DATA_REG
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_master_receive(master_dev_handle, rx_data_buf, 6, 100); // read 6 bytes into buf
    vTaskDelay(pdMS_TO_TICKS(5));
    *hr_sample = (uint32_t)((rx_data_buf[0] << 16) | (rx_data_buf[1] << 8) | (rx_data_buf[2])) & 0x3ffff;
    *ir_sample = (uint32_t)((rx_data_buf[3] << 16) | (rx_data_buf[4] << 8) | (rx_data_buf[5])) & 0x3ffff;
}