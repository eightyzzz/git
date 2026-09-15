#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "espat.h"
#include "lvgl_ui.h"
#include "wifi.h"
#include "storage.h"

// 启动时从 FlashDB 读取用户配置; 若用户尚未保存过, 回退到 wifi.h 的默认值。
static char g_ssid[64];
static char g_pass[64];

void wifi_init(void)
{
	strncpy(g_ssid, WIFI_SSID, sizeof(g_ssid) - 1);
	g_ssid[sizeof(g_ssid) - 1] = '\0';
	strncpy(g_pass, WIFI_PASSWD, sizeof(g_pass) - 1);
	g_pass[sizeof(g_pass) - 1] = '\0';

	if (storage_wifi_get(g_ssid, sizeof(g_ssid), g_pass, sizeof(g_pass)))
	{
		printf("[WIFI] loaded config ssid=%s\n", g_ssid);
	}
	else
	{
		printf("[WIFI] no saved config, use default\n");
	}

	if (!esp_at_init())
    {
        printf("[AT] init failed\n");
        goto err;
    }
    printf("[AT] inited\n");
    
    if (!esp_at_wifi_init())
    {
        printf("[WIFI] init failed\n");
        goto err;
    }
    printf("[WIFI] inited\n");
        
    if (!esp_at_sntp_init())
    {
        printf("[SNTP] init failed\n");
        goto err;
    }
    printf("[SNTP] inited\n");
	
	return;
	
err:
	printf("[WIFI] init failed, continue without wifi\n");
	lvgl_ui_set_wifi("wifi lost", false);
}

void wifi_wait_connect(void)
{
	printf("[WIFI] connecting\n");
    esp_at_connect_wifi(g_ssid, g_pass, NULL);

	for (uint32_t t = 0; t < 100; t ++)
	{
		vTaskDelay(pdMS_TO_TICKS(100));
	    esp_wifi_info_t wifi = { 0 };
        if (esp_at_get_wifi_info(&wifi) && wifi.connected)
        {
            printf("[WIFI] Connected\n");
			printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n", 
				wifi.ssid, wifi.bssid, wifi.channel, wifi.rssi);
            return;
        }
	}

	printf("[WIFI] Connection Timeout\n");
	lvgl_ui_set_wifi("wifi lost", false);
}

bool wifi_reconnect(const char *ssid, const char *pass)
{
	if (ssid == NULL || pass == NULL)
		return false;
	strncpy(g_ssid, ssid, sizeof(g_ssid) - 1);
	g_ssid[sizeof(g_ssid) - 1] = '\0';
	strncpy(g_pass, pass, sizeof(g_pass) - 1);
	g_pass[sizeof(g_pass) - 1] = '\0';

	printf("[WIFI] reconnect to %s ...\n", g_ssid);
	if (!esp_at_connect_wifi(g_ssid, g_pass, NULL))
	{
		printf("[WIFI] reconnect AT failed\n");
		return false;
	}
	printf("[WIFI] reconnect command sent, waiting for link\n");
	return true;
}
