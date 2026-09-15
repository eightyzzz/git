#ifndef __FT6336_H_
#define __FT6336_H_

#include <stdbool.h>
#include <stdint.h>

//触摸事件标志 (来自触点寄存器的 XH[7:6])
#define FT6336_EVENT_PRESS_DOWN		0x00	//按下
#define FT6336_EVENT_LIFT_UP		0x01	//抬起
#define FT6336_EVENT_CONTACT		0x02	//接触/滑动
#define FT6336_EVENT_NO_EVENT		0x03	//无事件

typedef struct
{
	uint8_t count;		//当前触点数量 (0~5)
	uint8_t event;		//第一个触点的事件
	uint16_t x;			//第一个触点的 X 坐标
	uint16_t y;			//第一个触点的 Y 坐标
} ft6336_touch_t;

bool ft6336_init(void);
bool ft6336_read_touch(ft6336_touch_t *touch);
bool ft6336_irq_pending(void);
void ft6336_irq_clear(void);

#endif
