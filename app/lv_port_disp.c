#include <stdint.h>
#include "lvgl.h"
#include "lv_port_disp.h"
#include "st7789.h"

#define DISP_BUF_ROWS	10	// double buffer: 2x10 rows = 9600B total, render overlaps DMA transfer

static lv_disp_draw_buf_t disp_draw_buf;
static lv_color_t disp_buf1[ST7789_WIDTH * DISP_BUF_ROWS];
static lv_color_t disp_buf2[ST7789_WIDTH * DISP_BUF_ROWS];
static lv_disp_drv_t disp_drv;
static lv_disp_drv_t *flush_drv;

/* Called from DMA ISR when the async transfer finished */
static void disp_flush_done(void)
{
	if (flush_drv)
		lv_disp_flush_ready(flush_drv);
}

/* LVGL flush callback: start async transfer only; completion reported by disp_flush_done */
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
	flush_drv = drv;
	st7789_flush_async(area->x1, area->y1, area->x2, area->y2, (uint16_t *)color_p);
}

void lv_port_disp_init(void)
{
	st7789_init();
	st7789_set_flush_done_cb(disp_flush_done);

	lv_disp_draw_buf_init(&disp_draw_buf, disp_buf1, disp_buf2, ST7789_WIDTH * DISP_BUF_ROWS);

	lv_disp_drv_init(&disp_drv);
	disp_drv.hor_res = ST7789_WIDTH;
	disp_drv.ver_res = ST7789_HEIGHT;
	disp_drv.flush_cb = disp_flush;
	disp_drv.draw_buf = &disp_draw_buf;
	lv_disp_drv_register(&disp_drv);
}
