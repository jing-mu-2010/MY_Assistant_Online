/*
 * Ball (OpenAI-style orb) animations for the four UI panels.
 *
 * Operates purely on the existing SquareLine-generated objects
 * (ui_ImageSleepBody / ui_ImageListenBody / glows / ui_ImageRelyBody),
 * so no generated UI file needs to change.
 *
 * Must be called with the LVGL lock held (ui_ctrl_show_panel already
 * takes bsp_display_lock before invoking these).
 */

#pragma once

#include "app_ui_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the animation set matching the given panel */
void ui_ball_anim_start(ui_ctrl_panel_t panel);

/* Stop all ball animations and restore default styles */
void ui_ball_anim_stop_all(void);

#ifdef __cplusplus
}
#endif
