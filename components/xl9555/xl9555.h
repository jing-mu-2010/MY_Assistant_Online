/* XL9555 I2C GPIO expander driver for BOX1 (old I2C API, GPIO 47/48) */
#pragma once
#include "driver/i2c.h"
#include "esp_err.h"

#define XL9555_ADDR                 0x20
#define XL9555_INPUT_PORT0_REG      0
#define XL9555_OUTPUT_PORT0_REG     2
#define XL9555_CONFIG_PORT0_REG     6

/* pin bitmasks */
#define AP_INT_IO                   0x0001
#define QMA_INT_IO                  0x0002
#define BEEP_IO                     0x0004
#define KEY1_IO                     0x0008
#define KEY0_IO                     0x0010
#define SPK_CTRL_IO                 0x0020
#define CTP_RST_IO                  0x0040
#define LCD_BL_IO                   0x0080
#define LEDR_IO                     0x0100
#define CTP_INT_IO                  0x0200

#define KEY0_PRES                   2
#define KEY1_PRES                   3
#define KEY0                        xl9555_pin_read(KEY0_IO)
#define KEY1                        xl9555_pin_read(KEY1_IO)

esp_err_t xl9555_init(void);
void      xl9555_set_addr(uint8_t addr);
int       xl9555_pin_read(uint16_t pin);
uint16_t  xl9555_pin_write(uint16_t pin, int val);
uint8_t   xl9555_key_scan(uint8_t mode);
