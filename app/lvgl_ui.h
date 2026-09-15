#ifndef __LVGL_UI_H__
#define __LVGL_UI_H__

#include <stdbool.h>
#include <stdint.h>
#include "rtc.h"

//滑动方向
#define LVGL_GESTURE_UP		0
#define LVGL_GESTURE_DOWN	1
#define LVGL_GESTURE_LEFT	2
#define LVGL_GESTURE_RIGHT	3

void lvgl_ui_init(void);
void lvgl_ui_on_gesture(int dir);

//以下接口由 app 任务调用, 内部只缓存数据, 由 lvgl 任务统一刷新控件
void lvgl_ui_set_time(const rtc_date_time_t *dt);
void lvgl_ui_set_wifi(const char *ssid, bool connected);
void lvgl_ui_set_indoor(float temperature, float humidity);
void lvgl_ui_set_outdoor(const char *city, float temperature, int weather_code, const char *text);

#endif
