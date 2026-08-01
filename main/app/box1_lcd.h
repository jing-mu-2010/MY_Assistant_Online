/* BOX1 MCU 8080 parallel LCD driver (ST7789 via Intel 8080 interface) */
#pragma once
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"
#include "esp_err.h"

esp_err_t box1_lcd_init(esp_lcd_panel_io_handle_t *ret_io, esp_lcd_panel_handle_t *ret_panel);
