#ifndef __ST7789_H__
#define __ST7789_H__

#include <stdint.h>

#define ST7789_WIDTH	240
#define ST7789_HEIGHT	320

void st7789_init(void);
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void st7789_set_flush_done_cb(void (*cb)(void));
void st7789_flush_async(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *colors);


#endif
