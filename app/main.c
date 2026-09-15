#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "lvgl_ui.h"
#include "storage.h"
#include "usb_mtp.h"
#include "wifi.h"
#include "app.h"

extern void board_lowlevel_init(void);
extern void board_init(void);

static void main_init(void *param)
{
	board_init();

	storage_init();
	usb_mtp_init();
	lvgl_ui_init();

	wifi_init();
	wifi_wait_connect();

	app_init();

	vTaskDelete(NULL);
}

int main(void)
{
	board_lowlevel_init();
	workqueue_init();
	
	xTaskCreate(main_init, "init", 1024, NULL, 9, NULL);
	
	vTaskStartScheduler();
	
	while (1)
	{
		;	//代码不会运行到这个
	}
}


