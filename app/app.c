#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "workqueue.h"
#include "weather.h"
#include "espat.h"
#include "aht20.h"
#include "lvgl_ui.h"
#include "rtc.h"

#define MILLISECONDS(x)	(x)
#define SECONDS(x)		MILLISECONDS((x) * 1000)
#define MINUTES(x)		SECONDS((x) * 60)
#define HOURS(x)		MINUTES((x) * 60)
#define DAYS(x)			HOURS((x) * 24)

#define TIME_SYNC_INTERVAL			HOURS(1)
#define WIFI_UPDATE_INTERVAL		SECONDS(3)
#define TIME_UPDATE_INTERVAL		SECONDS(1)
#define INNER_UPDATE_INTERVAL		SECONDS(3)
#define OUTDOOR_UPDATE_INTERVAL		MINUTES(1)

#define LOOP_EVT_TIME_SYNC			(1 << 0)	//0001
#define LOOP_EVT_WIFI_UPDATE		(1 << 1)	//0010
#define LOOP_EVT_INNER_UPDATE		(1 << 2)	//0100
#define LOOP_EVT_OUTDOOR_UPDATE		(1 << 3)	//1000
#define LOOP_EVT_ALL				(LOOP_EVT_TIME_SYNC | \
									 LOOP_EVT_WIFI_UPDATE | \
									 LOOP_EVT_INNER_UPDATE | \
									 LOOP_EVT_OUTDOOR_UPDATE)

static TimerHandle_t time_sync_timer;
static TimerHandle_t wifi_update_timer;
static TimerHandle_t time_update_timer;
static TimerHandle_t inner_update_timer;
static TimerHandle_t outdoor_update_timer;

static void time_sync(void)
{
	uint32_t restart_sync_delay = TIME_SYNC_INTERVAL;
	rtc_date_time_t rtc_date = {0};
	esp_date_time_t esp_date = {0};

	if (!esp_at_sntp_get_time(&esp_date))
	{
		printf("[SNTP] get time failed\n");
		restart_sync_delay = SECONDS(1);
		goto err;
	}
	if (esp_date.year < 2000)
	{
		printf("[SNTP] invaid date fotmate\n");
		restart_sync_delay = SECONDS(1);
		goto err;
	}

    printf("[SNTP] sync time: %04u-%02u-%02u %02u:%02u:%02u (%d)\n",
				esp_date.year, esp_date.month, esp_date.day,
				esp_date.hour, esp_date.minute, esp_date.second, esp_date.weekday);

	rtc_date.year = esp_date.year;
    rtc_date.month = esp_date.month;
    rtc_date.day = esp_date.day;
    rtc_date.hour = esp_date.hour;
    rtc_date.minute = esp_date.minute;
    rtc_date.second = esp_date.second;
    rtc_date.weekday = esp_date.weekday;
    rtc_set_time(&rtc_date);

err:
	xTimerChangePeriod(time_sync_timer, pdMS_TO_TICKS(restart_sync_delay), 0);
}

static void wifi_update(void)
{
	static esp_wifi_info_t last_info = {0};

	esp_wifi_info_t info = {0};
	if (!esp_at_get_wifi_info(&info))
		{
			printf("[AT] wifi info get failed\n");
			return;
		}

	if (memcmp(&info, &last_info, sizeof (esp_wifi_info_t)) == 0)
		return;

	/* 仅当连接状态和 SSID 都没变才跳过; 若连到了不同网段(SSID 变了),
	 * 即使状态仍是 connected 也要刷新主页面 WiFi 名称 */
	if (last_info.connected == info.connected &&
	    strncmp(last_info.ssid, info.ssid, sizeof(last_info.ssid)) == 0)
		return;

	if (info.connected)
	{
		/* 状态未变但 SSID 变了也要更新主页面 */
		lvgl_ui_set_wifi(info.ssid, true);
		printf("[WIFI] connected to %s\n", info.ssid);
		printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
				info.ssid, info.bssid, info.channel, info.rssi);
	}
	else
	{
		printf("[WIFI] disconnected from %s\n", last_info.ssid);
		lvgl_ui_set_wifi("wifi lost", false);
	}

	memcpy(&last_info, &info, sizeof(esp_wifi_info_t));
}

static void time_update(void)
{
	static rtc_date_time_t last_date = { 0 };

	rtc_date_time_t date;
	rtc_get_time(&date);

	if (date.year < 2020)
		return;

	if (memcmp(&date, &last_date, sizeof(rtc_date_time_t)) == 0)
		return;

	memcpy(&last_date, &date, sizeof(rtc_date_time_t));
	lvgl_ui_set_time(&date);
}

static void inner_update(void)
{
	static float last_temperature, last_humidity;

	if (!aht20_start_measurement())
	{
		printf ("[AHT20] start measurement failed\n");
		return;
	}

	if (!aht20_wait_for_measurement())
	{
		printf ("[AHT20] wait measurement failed\n");
		return;
	}

	static float temperature = 0.0f, humidity = 0.0f;
	if (!aht20_read_measurement(&temperature, &humidity))
	{
		printf ("[AHT20] read measurement failed\n");
		return;
	}

	if (temperature == last_temperature && humidity == last_humidity)
		return;

	last_temperature = temperature;
	last_humidity = humidity;

	printf ("[AHT20] Temperature: %.1f, Humidity: %.1f\n", temperature, humidity);
	lvgl_ui_set_indoor(temperature, humidity);
}

static void outdoor_update(void)
{
	static weather_info_t last_weather = { 0 };

	weather_info_t weather = { 0 };
	const char *weather_url = "https://api.seniverse.com/v3/weather/now.json?key=SLGPI8dHGWDaTC218&location=xiamen&language=en&unit=c";
	const char *weather_http_response = esp_at_http_get(weather_url);
	if (weather_http_response == NULL)
	{
		printf("[WEATHER] http error\n");
		return;
	}

	if (!parse_seniverse_response(weather_http_response, &weather))
	{
		printf("[WEATHER] parse failed\n");
		return;
	}

	if (memcmp(&last_weather, &weather, sizeof(weather_info_t)) == 0)
		return;

	memcpy(&last_weather, &weather, sizeof(weather_info_t));
	printf("[WEATHER] %s, %s, %.1f\n", weather.city, weather.weather, weather.temperature);

	lvgl_ui_set_outdoor(weather.city, weather.temperature, weather.weather_code, weather.weather);
}

typedef void (*app_job_t)(void);

static void app_work(void *param)
{
	app_job_t job = (app_job_t)param;
	job();
}

static void work_timer_cb(TimerHandle_t timer)
{
	app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
	workqueue_run(app_work, job);
}

static void app_timer_cb(TimerHandle_t timer)
{
	app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
	job();
}

void app_init(void)
{
	time_update_timer = xTimerCreate("time updata", pdMS_TO_TICKS(TIME_UPDATE_INTERVAL), pdTRUE, time_update, app_timer_cb);
	time_sync_timer = xTimerCreate("time sync", pdMS_TO_TICKS(200), pdFALSE, time_sync, work_timer_cb);
	wifi_update_timer = xTimerCreate("wifi updata", pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL), pdTRUE, wifi_update, work_timer_cb);
	inner_update_timer = xTimerCreate("inner updata", pdMS_TO_TICKS(INNER_UPDATE_INTERVAL), pdTRUE, inner_update, work_timer_cb);
	outdoor_update_timer = xTimerCreate("outdoor data", pdMS_TO_TICKS(OUTDOOR_UPDATE_INTERVAL), pdTRUE, outdoor_update, work_timer_cb);

	workqueue_run(app_work, time_sync);
	workqueue_run(app_work, wifi_update);
	workqueue_run(app_work, inner_update);
	workqueue_run(app_work, outdoor_update);


	xTimerStart(time_update_timer, 0);
	xTimerStart(time_sync_timer, 0);
	xTimerStart(wifi_update_timer, 0);
	xTimerStart(inner_update_timer, 0);
	xTimerStart(outdoor_update_timer, 0);
}
