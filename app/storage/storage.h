#ifndef __STORAGE_H__
#define __STORAGE_H__

#include <stdbool.h>
#include <stddef.h>

//初始化 SPI Flash + LittleFS(片2) + 资源自供给
//必须在 lvgl_ui_init 之前调用 (LVGL 资源从 LittleFS 加载)
bool storage_init(void);

//FlashDB (片1 FDB 分区) 初始化
bool storage_fdb_init(void);

// 读写 WiFi 配置 (持久化到 FlashDB)。返回 true 表示成功。
// get 时若键不存在或读失败, 返回 false 且不改动 *ssid/*pass。
bool storage_wifi_set(const char *ssid, const char *pass);
bool storage_wifi_get(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

#endif
