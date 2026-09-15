#ifndef __WIFI_H__
#define __WIFI_H__

#define WIFI_SSID	"Xiaomi 14"
#define WIFI_PASSWD	"1234567890"

void wifi_init(void);
void wifi_wait_connect(void);
// 保存新的 WiFi 配置到 FlashDB 并尝试重连。返回 true 表示 AT 发起连接成功(不保证连上)。
bool wifi_reconnect(const char *ssid, const char *pass);


#endif
