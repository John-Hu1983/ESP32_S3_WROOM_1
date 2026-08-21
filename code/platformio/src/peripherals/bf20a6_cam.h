#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_camera.h"
#include "esp_err.h"
#include "user_config.h"

#define CAM_BF20A6_PROFILE_ID (0U)                /* Fixed candidate index: 0->21, 1->31, 2->37, 3->39, 4->45, 5->47. */
#define CAM_BF20A6_SENSOR_YUV_SEQ (2U)            /* Sensor YUV sequence selector (register 0x16[3:2]). */
#define CAM_BF20A6_SENSOR_COM0_BYTE_SWAP (0)      /* COM0 byte-swap bit for YUV output. */
#define CAM_BF20A6_SENSOR_COM1_VCLK_REV_AFTER (0) /* COM1 VCLK reverse after internal timing. */
#define CAM_BF20A6_TEST_MODE (0x00U)              /* Sensor register 0xB6 test-pattern mode. */
#define CAM_BF20A6_FRAME_SIZE (FRAMESIZE_VGA)     /* Captured frame size passed to esp32-camera. */
#define CAM_BF20A6_FB_COUNT (2U)                  /* Number of camera frame buffers. */
#define CAM_BF20A6_YUV_ORDER (3)                  /* UI parser order: 0 AUTO, 1 YUYV, 2 UYVY, 3 YVYU, 4 VYUY. */
#define CAM_BF20A6_PCLK_INVERT (0)                /* Host LCD_CAM PCLK sampling edge invert. */
#define CAM_BF20A6_HSYNC_INVERT (0)               /* Host LCD_CAM HSYNC polarity invert. */
#define CAM_BF20A6_VSYNC_INVERT (0)               /* Host LCD_CAM VSYNC polarity invert. */
#define CAM_BF20A6_HMIRROR (1)                    /* Sensor horizontal mirror enable. */
#define CAM_BF20A6_VFLIP (1)                      /* Sensor vertical flip enable. */

typedef struct
{
    bool sensor_div;       /* Apply sensor-side divider/clock registers E3/F0. */
    uint8_t pll_ctrl;      /* BF20A6 reg 0xE3 PLL/divider control value. */
    uint8_t int_time_ctrl; /* BF20A6 reg 0xF0 integration-time/frame-rate control value. */
    bool hsync_as_href;    /* COM1[3]: output HSYNC pin as HREF when set. */
    bool vclk_rev_before;  /* COM1[7]: reverse VCLK before internal timing stage. */
    bool vclk_rev_after;   /* COM1[4]: reverse VCLK after internal timing stage. */
    bool reg_div_vclk_inv; /* REG_DIV[4]: invert VCLK divider output polarity. */
} cam_profile_s;

/* Initialize BF20A6 camera path with fixed board config and power/reset sequence. */
esp_err_t bf20a6_cam_open(void);
/* Deinitialize BF20A6 camera path and put sensor into low-power state. */
void bf20a6_cam_close(void);
/* Query whether BF20A6 driver has been initialized successfully. */
bool bf20a6_cam_is_open(void);
/* Acquire one frame buffer from esp32-camera backend. */
camera_fb_t *bf20a6_cam_fb_get(void);
/* Return one frame buffer to esp32-camera backend. */
void bf20a6_cam_fb_return(camera_fb_t *fb);
/* Control external camera light pin exposed by GPBA02B. */
esp_err_t bf20a6_cam_set_light(bool enable);
