/* XL9555 I2C GPIO expander — uses old I2C API (compatible with box1_board_init) */
#include "xl9555.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xl9555";
static i2c_port_t i2c_port = I2C_NUM_0;
static uint8_t xl9555_addr = XL9555_ADDR;

void xl9555_set_addr(uint8_t addr)
{
    xl9555_addr = addr;
}

static esp_err_t xl9555_read_regs(uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (xl9555_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, XL9555_INPUT_PORT0_REG, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (xl9555_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t xl9555_write_regs(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (xl9555_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void xl9555_ioconfig(uint16_t config_value)
{
    uint8_t data[2] = { (uint8_t)config_value, (uint8_t)(config_value >> 8) };
    esp_err_t err;
    do {
        err = xl9555_write_regs(XL9555_CONFIG_PORT0_REG, data, 2);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ioconfig 0x%04X failed: 0x%x", config_value, err);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (err != ESP_OK);
}

int xl9555_pin_read(uint16_t pin)
{
    uint8_t r_data[2];
    if (xl9555_read_regs(r_data, 2) != ESP_OK) return 0;
    uint16_t ret = ((uint16_t)r_data[1] << 8) | r_data[0];
    return (ret & pin) ? 1 : 0;
}

uint16_t xl9555_pin_write(uint16_t pin, int val)
{
    uint8_t w_data[2];
    xl9555_read_regs(w_data, 2);
    if (pin <= 0x00FF) {
        w_data[0] = val ? (w_data[0] | (uint8_t)pin) : (w_data[0] & ~(uint8_t)pin);
    } else {
        w_data[1] = val ? (w_data[1] | (uint8_t)(pin >> 8)) : (w_data[1] & ~(uint8_t)(pin >> 8));
    }
    xl9555_write_regs(XL9555_OUTPUT_PORT0_REG, w_data, 2);
    return ((uint16_t)w_data[1] << 8) | w_data[0];
}

uint8_t xl9555_key_scan(uint8_t mode)
{
    uint8_t keyval = 0;
    static uint8_t key_up = 1;
    if (mode) key_up = 1;
    if (key_up && (KEY0 == 0 || KEY1 == 0)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        key_up = 0;
        if (KEY0 == 0) keyval = KEY0_PRES;
        if (KEY1 == 0) keyval = KEY1_PRES;
    } else if (KEY0 == 1 && KEY1 == 1) {
        key_up = 1;
    }
    return keyval;
}

esp_err_t xl9555_init(void)
{
    uint8_t r_data[2];
    /* read once to clear interrupt */
    xl9555_read_regs(r_data, 2);
    /* configure pins: 0=output, 1=input. 0xFE1B → P10-P14 input, rest output */
    xl9555_ioconfig(0xFE1B);
    /* beeper off */
    xl9555_pin_write(BEEP_IO, 1);
    /* speaker amp off initially */
    xl9555_pin_write(SPK_CTRL_IO, 1);
    ESP_LOGI(TAG, "XL9555 ready at 0x%02X", xl9555_addr);
    return ESP_OK;
}
