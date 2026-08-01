/*
 * Button input for BOX1 (Mute button on GPIO 1).
 *
 * Mapping (方案 B):
 *   single click = volume down
 *   double click = volume up
 *   long press   = volume up
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_buttons_init(void);

#ifdef __cplusplus
}
#endif
