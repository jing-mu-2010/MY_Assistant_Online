/*
 * Ball (OpenAI-style orb) animations for the four UI panels.
 *
 *   SLEEP  — slow breathing zoom, dimmed
 *   LISTEN — gentle bounce + glow pulse while recording
 *   GET    — hue-cycling glow while thinking
 *   REPLY  — tinted, pulsing small ball while speaking
 */

#include "app_ui_anim.h"
#include "ui.h"

/* lv_img zoom factor: 256 == 100% */
#define ZOOM_100_PERCENT    (256)

/* ---------- exec callbacks ---------- */

static void anim_exec_zoom(void *target, int32_t v)
{
    lv_img_set_zoom((lv_obj_t *)target, (uint16_t)v);
}

static void anim_exec_offset_y(void *target, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)target, (lv_coord_t)v);
}

static void anim_exec_img_opa(void *target, int32_t v)
{
    lv_obj_set_style_img_opa((lv_obj_t *)target, (lv_opa_t)v, LV_PART_MAIN);
}

/* hue 0..359 -> recolor of the glow image ("thinking" state) */
static void anim_exec_glow_hue(void *target, int32_t v)
{
    lv_color_t c = lv_color_hsv_to_rgb((uint16_t)(v % 360), 70, 100);
    lv_obj_set_style_img_recolor((lv_obj_t *)target, c, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa((lv_obj_t *)target, 160, LV_PART_MAIN);
}

/* ---------- helpers ---------- */

static void anim_start_loop(lv_obj_t *target, lv_anim_exec_xcb_t exec_cb,
                            int32_t start, int32_t end, uint32_t time_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_exec_cb(&a, exec_cb);
    lv_anim_set_values(&a, start, end);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_playback_time(&a, time_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void anim_start_hue_cycle(lv_obj_t *target, uint32_t period_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_exec_cb(&a, anim_exec_glow_hue);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

/* ---------- public API ---------- */

void ui_ball_anim_stop_all(void)
{
    lv_anim_del(ui_ImageSleepBody, NULL);
    lv_anim_del(ui_ImageListenBody, NULL);
    lv_anim_del(ui_ImageListenBackGlow, NULL);
    lv_anim_del(ui_ImageGetBody, NULL);
    lv_anim_del(ui_ImageGetBackGlow, NULL);
    lv_anim_del(ui_ImageRelyBody, NULL);

    /* restore default styles */
    lv_img_set_zoom(ui_ImageSleepBody, ZOOM_100_PERCENT);
    lv_img_set_zoom(ui_ImageListenBody, ZOOM_100_PERCENT);
    lv_img_set_zoom(ui_ImageGetBody, ZOOM_100_PERCENT);
    lv_img_set_zoom(ui_ImageRelyBody, ZOOM_100_PERCENT);
    lv_obj_set_y(ui_ImageListenBody, 0);
    lv_obj_set_style_img_opa(ui_ImageListenBackGlow, 255, LV_PART_MAIN);
    lv_obj_set_style_img_opa(ui_ImageGetBackGlow, 255, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(ui_ImageGetBackGlow, 0, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(ui_ImageRelyBody, 0, LV_PART_MAIN);
}

void ui_ball_anim_start(ui_ctrl_panel_t panel)
{
    switch (panel) {
    case UI_CTRL_PANEL_SLEEP:
        /* slow breathing */
        anim_start_loop(ui_ImageSleepBody, anim_exec_zoom, 240, ZOOM_100_PERCENT, 2000);
        break;

    case UI_CTRL_PANEL_LISTEN:
        /* gentle bounce + glow pulse while recording */
        anim_start_loop(ui_ImageListenBody, anim_exec_offset_y, 0, -6, 800);
        anim_start_loop(ui_ImageListenBackGlow, anim_exec_img_opa, 120, 255, 800);
        break;

    case UI_CTRL_PANEL_GET:
        /* hue-cycling glow + subtle breathing while thinking */
        anim_start_hue_cycle(ui_ImageGetBackGlow, 2400);
        anim_start_loop(ui_ImageGetBody, anim_exec_zoom, 248, ZOOM_100_PERCENT, 1200);
        break;

    case UI_CTRL_PANEL_REPLY:
        /* tinted, pulsing small ball while speaking */
        lv_obj_set_style_img_recolor(ui_ImageRelyBody, lv_color_hex(0x4FC3F7), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(ui_ImageRelyBody, 90, LV_PART_MAIN);
        anim_start_loop(ui_ImageRelyBody, anim_exec_zoom, 240, 272, 500);
        break;

    default:
        break;
    }
}
