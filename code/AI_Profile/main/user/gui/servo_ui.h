#pragma once

#include "user/desktop/desktop_app.h"
#include "user/device/dev_servo.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define SERVO_UI_TASK_STACK_SIZE            (6144U)
#define SERVO_UI_TASK_PERIOD_MS             (10U)

#define SERVO_UI_CARD_GAP_PX                (6)
#define SERVO_UI_CARD_RADIUS_PX             (8)
#define SERVO_UI_SCOPE_GRID_H_LINE_CNT      (6U)
#define SERVO_UI_SCOPE_GRID_V_LINE_CNT      (8U)
#define SERVO_UI_SCOPE_POINT_COUNT          (96U)
#define SERVO_UI_SCOPE_Y_MIN_DEG            (-30)
#define SERVO_UI_SCOPE_Y_MAX_DEG            (130)
// clang-format on

#if CONFIG_LV_FONT_MONTSERRAT_12
#define SERVO_UI_TITLE_FONT                 lv_font_montserrat_12
#elif CONFIG_LV_FONT_MONTSERRAT_14
#define SERVO_UI_TITLE_FONT                 lv_font_montserrat_14
#else
#define SERVO_UI_TITLE_FONT                 DESKTOP_TEXT_FONT
#endif

#if CONFIG_LV_FONT_MONTSERRAT_12
#define SERVO_UI_TEXT_FONT                  lv_font_montserrat_12
#else
#define SERVO_UI_TEXT_FONT                  DESKTOP_TEXT_FONT
#endif
typedef struct {
    TaskHandle_t task_handle;
    ui_menu_home_cb_t home_cb;
    void* home_user_ctx;
    btn_scan_s button_scan;
} servo_ui_runtime_s;

lv_obj_t* servo_open_screen(lv_obj_t* parent, lv_coord_t area_w, lv_coord_t area_h,
                            ui_menu_home_cb_t home_cb, void* home_user_ctx);
void servo_close_screen(lv_obj_t* screen);

#ifdef __cplusplus
}
#endif
