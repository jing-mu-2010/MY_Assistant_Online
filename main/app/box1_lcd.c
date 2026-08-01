/* BOX1 MCU 8080 parallel LCD for ST7789 — adapted from 正点原子验收代码 */
#include "box1_lcd.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#define LCD_CS   GPIO_NUM_1
#define LCD_DC   GPIO_NUM_2
#define LCD_RD   GPIO_NUM_41
#define LCD_WR   GPIO_NUM_42
#define LCD_RST  GPIO_NUM_NC  /* ST7789 hardware reset pin — not connected on BOX1 */

static const int LCD_D0 = 40, LCD_D1 = 39, LCD_D2 = 38, LCD_D3 = 12;
static const int LCD_D4 = 11, LCD_D5 = 10, LCD_D6 = 9,  LCD_D7 = 46;

static const char *TAG = "box1_lcd";

esp_err_t box1_lcd_init(esp_lcd_panel_io_handle_t *ret_io, esp_lcd_panel_handle_t *ret_panel)
{
    gpio_config_t rd_cfg = {
        .pin_bit_mask = 1ULL << LCD_RD,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rd_cfg));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RD, 1));

    /* Configure 8080 parallel bus */
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = LCD_DC,
        .wr_gpio_num = LCD_WR,
        .data_gpio_nums = { LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_D4, LCD_D5, LCD_D6, LCD_D7 },
        .bus_width = 8,
        .max_transfer_bytes = 320 * 240 * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &i80_bus));

    /* Panel IO (8080 mode) */
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 10,
        .dc_levels = { .dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1 },
        .flags = { .swap_color_bytes = 0 },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, ret_io));

    /* ST7789 panel */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(*ret_io, &panel_cfg, ret_panel));

    esp_lcd_panel_reset(*ret_panel);
    esp_lcd_panel_init(*ret_panel);
    esp_lcd_panel_invert_color(*ret_panel, true);
    esp_lcd_panel_set_gap(*ret_panel, 0, 0);
    esp_lcd_panel_io_tx_param(*ret_io, 0x36, (uint8_t[]){ 0 }, 1);
    esp_lcd_panel_io_tx_param(*ret_io, 0x3A, (uint8_t[]){ 0x65 }, 1);  /* pixel format 16-bit */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(*ret_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(*ret_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*ret_panel, true));

    ESP_LOGI(TAG, "MCU 8080 LCD ready (320x240 ST7789)");
    return ESP_OK;
}
