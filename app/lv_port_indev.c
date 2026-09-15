#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "lv_port_indev.h"
#include "ft6336.h"
#include "lvgl_ui.h"

#define SWIPE_THRESHOLD	40	//滑动判定阈值(像素)

static lv_indev_drv_t indev_drv;

//触摸读取回调 (由 lv_timer_handler 周期性调用):
//读取 FT6336 坐标, 抬起时根据位移识别滑动方向并交给 UI 处理
static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	ft6336_touch_t touch;
	static uint16_t start_x, start_y, last_x, last_y;
	static bool pressed;

	if (ft6336_read_touch(&touch) && touch.count > 0)
	{
		if (!pressed)
		{
			pressed = true;
			start_x = last_x = touch.x;
			start_y = last_y = touch.y;
		}
		else
		{
			last_x = touch.x;
			last_y = touch.y;
		}

		data->point.x = touch.x;
		data->point.y = touch.y;
		data->state = LV_INDEV_STATE_PR;
	}
	else
	{
		if (pressed)
		{
			pressed = false;

			int16_t dx = (int16_t)last_x - (int16_t)start_x;
			int16_t dy = (int16_t)last_y - (int16_t)start_y;
			uint16_t adx = (dx < 0) ? (uint16_t)-dx : (uint16_t)dx;
			uint16_t ady = (dy < 0) ? (uint16_t)-dy : (uint16_t)dy;

			if (adx >= SWIPE_THRESHOLD && adx > ady)
				lvgl_ui_on_gesture(dx > 0 ? LVGL_GESTURE_RIGHT : LVGL_GESTURE_LEFT);
			else if (ady >= SWIPE_THRESHOLD)
				lvgl_ui_on_gesture(dy > 0 ? LVGL_GESTURE_DOWN : LVGL_GESTURE_UP);
		}

		data->state = LV_INDEV_STATE_REL;
	}
}

void lv_port_indev_init(void)
{
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = touchpad_read;
	lv_indev_drv_register(&indev_drv);
}
