#ifndef MAX30102_I2C_H_
#define MAX30102_I2C_H_

#include <stdint.h>
#include "driver/i2c_master.h"

#define MAX_DEV_I2C_ADDR 0x57
#define MAX_INT_SOURCE_1_REG 0x00
#define MAX_LED1_PA_REG 0x0c
#define MAX_LED2_PA_REG 0x0d
#define MAX_MODE_CONFIG_REG 0x09
#define MAX_CONFIG_SPO2_REG 0x0a
#define MAX_FIFO_CONFIG_REG 0x08
#define MAX_FIFO_DATA_REG 0x07

#define MAX_FIFO_WRITE_PTR 0x04
#define MAX_FIFO_OVF_CTR 0x05
#define MAX_FIFO_READ_PTR 0x06

#define MAX_DEBUG 1

void max30102_config(i2c_master_dev_handle_t master_dev_handle);
void max30102_init(i2c_master_dev_handle_t master_dev_handle);
void max30102_fifo_read(i2c_master_dev_handle_t master_dev_handle, uint32_t* hr_sample, uint32_t* ir_sample);
void max30102_fifo_init(i2c_master_dev_handle_t master_dev_handle);
void max30102_int_handle(i2c_master_dev_handle_t master_dev_handle);

#endif