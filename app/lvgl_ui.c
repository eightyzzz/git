#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl_ui.h"
#include "lv_fs_lfs.h"
#include "src/extra/widgets/keyboard/lv_keyboard.h"
#include "src/widgets/lv_textarea.h"
#include "wifi.h"
#include "storage.h"
#include "workqueue.h"
#include "lfs.h"
#include "lfs_port.h"

static const lv_font_t *font_cn;	//中文字库 (编进固件)

//中文显示字符串: 用 UTF-8 十六进制转义, 避免 ARMCC 对源文件编码(GBK)的兼容问题
#define STR_CN_QING		"\xE6\x99\xB4"					/*晴*/
#define STR_CN_YIN		"\xE9\x98\xB4"					/*阴*/
#define STR_CN_DUOYUN	"\xE5\xA4\x9A\xE4\xBA\x91"		/*多云*/
#define STR_CN_YU		"\xE9\x9B\xA8"					/*雨*/
#define STR_CN_LEI		"\xE9\x9B\xB7\xE9\x9B\xA8"				/*雷雨*/
#define STR_CN_XUE		"\xE9\x9B\xAA"					/*雪*/
#define STR_CN_WU		"\xE9\x9B\xBE"					/*雾*/
#define STR_CN_WEIZHI	"\xE6\x9C\xAA\xE7\x9F\xA5"		/*未知*/
#define STR_CN_SHINEI	"\xE5\xAE\xA4\xE5\x86\x85\xE7\x8E\xAF\xE5\xA2\x83"	/*室内温湿度*/
#define STR_CN_LONGYAN	"\xE9\xBE\x99\xE5\xB2\xA9"		/*龙岩*/
#define STR_CN_XIAMEN	"\xE5\x8E\xA6\xE9\x97\xA8"		/*厦门*/
#define STR_CN_WANGLUO	"\xE7\xBD\x91\xE7\xBB\x9C\xE6\x96\xAD\xE5\xBC\x80"	/*网络断开*/
#define STR_CN_WIFI_S	"\xE8\xAE\xBE\xE7\xBD\xAE\x57\x69\x46\x69"			/*设置WiFi*/
#define STR_CN_SSID		"\xE5\x90\x8D\xE7\xA7\xB0"							/*名称*/
#define STR_CN_PASS		"\xE5\xAF\x86\xE7\xA0\x81"							/*密码*/
#define STR_CN_SAVE		"\xE4\xBF\x9D\xE5\xAD\x98"							/*保存*/
#define STR_CN_CANCEL	"\xE5\x8F\x96\xE6\xB6\x88"							/*取消*/
#define STR_CN_CONNECT	"\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD"				/*连接中*/
#define STR_CN_BACK		"\xE8\xBF\x94\xE5\x9B\x9E"							/*返回*/
#define STR_CN_FILES	"\xE6\x96\x87\xE4\xBB\xB6\xE6\x9F\xA5\xE7\x9C\x8B"	/*文件查看*/
#define STR_CN_VIEW		"\xE9\xA2\x84\xE8\xA7\x88"							/*预览*/

#define LVGL_TASK_PRIO		3
#define LVGL_TASK_STACK		2048

#define UI_PAGE_MAIN		0
#define UI_PAGE_BLANK		1
#define UI_PAGE_COUNT		2

//由 app 任务写入的待更新数据, lvgl 任务统一应用到控件 (避免LVGL线程安全问题)
static rtc_date_time_t data_time;
static char data_ssid[32];
static bool data_wifi_connected;
static float data_temperature, data_humidity;
static char data_city[32];
static float data_outdoor_temp;
static int data_weather_code;
static char data_weather_text[16];
static volatile uint8_t data_dirty;

#define DATA_DIRTY_TIME		(1 << 0)
#define DATA_DIRTY_WIFI		(1 << 1)
#define DATA_DIRTY_INDOOR	(1 << 2)
#define DATA_DIRTY_OUTDOOR	(1 << 3)

static lv_obj_t *screen_main;
static lv_obj_t *screen_blank;
static lv_obj_t *card_out;

static lv_obj_t *label_time;
static lv_obj_t *label_date;
static lv_obj_t *label_wifi_ssid;
static lv_obj_t *img_wifi;

static lv_obj_t *label_indoor_temp;
static lv_obj_t *label_indoor_hum;

static lv_obj_t *label_outdoor_city;
static lv_obj_t *label_outdoor_temp;
static lv_obj_t *img_outdoor_icon;

/* ---- WiFi 设置界面 (第二页) ---- */
static lv_obj_t *scr_wifi_entry;	/* 第二页: 入口界面 */
static lv_obj_t *scr_wifi_set;		/* WiFi 编辑界面 */
static lv_obj_t *ta_ssid;		/* SSID 输入框 */
static lv_obj_t *ta_pass;		/* 密码输入框 */
static lv_obj_t *kb;			/* 键盘 */
static bool kb_active;			/* 键盘是否弹出(屏蔽滑动翻页) */
static lv_obj_t *btn_eye;		/* 显示/隐藏密码按钮 */
static lv_obj_t *lbl_eye;		/* 按钮上的眼睛符号 */

/* ---- 文件查看界面 (第三屏, 从第二页进入) ---- */
static lv_obj_t *scr_file_list;		/* 文件列表页 */
static lv_obj_t *scr_file_view;		/* 图片预览页 */
static lv_obj_t *list_file;			/* 文件列表容器 */
static lv_obj_t *img_file_preview;	/* 预览图片 */
static char cur_file[64];			/* 当前预览的文件名 */
static char file_preview_path[72];			/* 当前预览的 .bin 文件路径(流式读取, 需常驻) */
static char cur_dir[64] = "/";				/* 当前浏览的目录(文件查看页) */

/* ---- 文件列表虚拟化: 只建固定行数控件, 滚动按索引复用, LVGL 堆占用恒定 ---- */
#define FILE_LIST_ROWS	8
#define FILE_LIST_MAX	128	/* ponytail: 目录条目上限(文件夹+文件), 每项约41B静态RAM; 超出不显示, 需要时加大 */
typedef struct
{
	char     name[40];
	uint8_t  is_dir;
} file_entry_t;
static file_entry_t file_entries[FILE_LIST_MAX];
static int file_entry_cnt;
static lv_obj_t *file_rows[FILE_LIST_ROWS];
static bool file_rows_ready;
static lv_obj_t *list_spacer;

static lv_img_dsc_t dsc_weather_file;
static uint8_t weather_icon_buf[54 * 54 * 2];	//最大图标 54x54 RGB565

static int current_page = UI_PAGE_MAIN;

static lv_img_dsc_t dsc_wifi_file;
static uint8_t wifi_icon_buf[24 * 24 * 2];
static lv_font_t font_wifi_small;			//WiFi名称: 14px ASCII + 中文回退

extern const lv_font_t lv_font_time_68;		//大字号时间字体 (68px, 只含数字/冒号)
extern const lv_font_t lv_font_cn;			//中文字库 (20px, 编进固件)

//固定布局页面: 禁用滚动, 防止触摸拖动把屏幕内容拖偏 (LVGL 的 lv_obj 默认可滚动)
static void ui_no_scroll(lv_obj_t *obj)
{
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

//把工程里的 RGB565 图片数据包装成 LVGL 图片描述符
static const char *weather_icon_path(int code)
{
	if (code == 0 || code == 2 || code == 38)
		return "A:/icon_qingtian.bin";
	else if (code == 1 || code == 3)
		return "A:/icon_yueliang.bin";
	else if (code == 4 || code == 9)
		return "A:/icon_yintian.bin";
	else if (code == 5 || code == 6 || code == 7 || code == 8)
		return "A:/icon_duoyun.bin";
	else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
		return "A:/icon_zhongyu.bin";
	else if (code == 11 || code == 12)
		return "A:/icon_leizhenyu.bin";
	else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
		return "A:/icon_zhongxue.bin";
	else if (code == 30)
		return "A:/icon_wu.bin";
	else
		return "A:/icon_na.bin";
}

//LVGL 软件渲染不支持"文件图片+zoom", 图标需先读入内存再缩放显示
static bool icon_file_load(const char *path, uint8_t *buf, uint32_t buf_cap, lv_img_dsc_t *dsc)
{
	lv_fs_file_t f;
	uint8_t hdr[4];
	uint32_t br;

	if (lv_fs_open(&f, path, LV_FS_MODE_RD) != LV_FS_RES_OK)
		return false;
	if (lv_fs_read(&f, hdr, 4, &br) != LV_FS_RES_OK || br != 4)
	{
		lv_fs_close(&f);
		return false;
	}
	uint32_t h = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
				 ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
	uint32_t w = (h >> 10) & 0x7FF;
	uint32_t hh = (h >> 21) & 0x7FF;
	uint32_t size = w * hh * 2;
	if (w == 0 || hh == 0 || size > buf_cap)
	{
		lv_fs_close(&f);
		return false;
	}
	if (lv_fs_read(&f, buf, size, &br) != LV_FS_RES_OK || br != size)
	{
		lv_fs_close(&f);
		return false;
	}
	lv_fs_close(&f);

	memset(dsc, 0, sizeof(lv_img_dsc_t));
	dsc->header.always_zero = 0;
	dsc->header.w = w;
	dsc->header.h = hh;
	dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
	dsc->data_size = size;
	dsc->data = buf;
	return true;
}

static void weather_icon_set(const char *path, const lv_img_dsc_t *fallback)
{
	if (icon_file_load(path, weather_icon_buf, sizeof(weather_icon_buf), &dsc_weather_file))
		lv_img_set_src(img_outdoor_icon, &dsc_weather_file);
	else
		lv_img_set_src(img_outdoor_icon, fallback);
}

static void wifi_icon_set(const char *path, const lv_img_dsc_t *fallback)
{
	if (icon_file_load(path, wifi_icon_buf, sizeof(wifi_icon_buf), &dsc_wifi_file))
		lv_img_set_src(img_wifi, &dsc_wifi_file);
	else
		lv_img_set_src(img_wifi, fallback);
}

//中文字体样式 (加载失败时退到默认字体)
static void ui_style_cn_font(lv_obj_t *obj)
{
	lv_obj_set_style_text_font(obj, font_cn ? font_cn : &lv_font_montserrat_14, 0);
}

//天气文字按心知天气 code 映射为中文
static const char *weather_text_cn(int code)
{
	if (code == 0 || code == 1 || code == 2 || code == 3 || code == 38)
		return STR_CN_QING;
	if (code == 4 || code == 9)
		return STR_CN_YIN;
	if (code == 5 || code == 6 || code == 7 || code == 8)
		return STR_CN_DUOYUN;
	if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
		return STR_CN_YU;
	if (code == 11 || code == 12)
		return STR_CN_LEI;
	if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
		return STR_CN_XUE;
	if (code == 30)
		return STR_CN_WU;
	return STR_CN_WEIZHI;
}

//主页面: 参考最初布局, 三张卡片合成一屏
static void ui_build_main_screen(void)
{
	screen_main = lv_obj_create(NULL);
	ui_no_scroll(screen_main);
	lv_obj_set_style_bg_color(screen_main, lv_color_black(), 0);

	//顶部时间卡片
	lv_obj_t *card_time = lv_obj_create(screen_main);
	ui_no_scroll(card_time);
	lv_obj_set_size(card_time, 210, 140);
	lv_obj_align(card_time, LV_ALIGN_TOP_LEFT, 15, 15);
	lv_obj_set_style_bg_color(card_time, lv_color_white(), 0);
	lv_obj_set_style_radius(card_time, 8, 0);
	lv_obj_set_style_border_width(card_time, 0, 0);
	lv_obj_set_style_pad_all(card_time, 0, 0);	//清除主题默认16px内边距, 让坐标精确贴角

	img_wifi = lv_img_create(card_time);
	wifi_icon_set("A:/icon_no_wifi.bin", NULL);
	lv_img_set_zoom(img_wifi, 180);	//缩小到约70%
	lv_obj_align(img_wifi, LV_ALIGN_TOP_LEFT, 3, 3);

	label_wifi_ssid = lv_label_create(card_time);
	lv_obj_align(label_wifi_ssid, LV_ALIGN_TOP_RIGHT, -4, 3);
	lv_label_set_text(label_wifi_ssid, STR_CN_WANGLUO);
	lv_obj_set_style_text_font(label_wifi_ssid, &font_wifi_small, 0);
	lv_obj_set_style_text_color(label_wifi_ssid, lv_color_hex(0x8F8F8F), 0);

	label_time = lv_label_create(card_time);
	lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -4);
	lv_label_set_text(label_time, "--:--");
	lv_label_set_recolor(label_time, true);
	lv_obj_set_style_text_font(label_time, &lv_font_time_68, 0);
	lv_obj_set_style_text_color(label_time, lv_color_black(), 0);

	label_date = lv_label_create(card_time);
	lv_obj_align(label_date, LV_ALIGN_BOTTOM_MID, 0, -1);
	lv_label_set_text(label_date, "----/--/--");
	lv_obj_set_style_text_color(label_date, lv_color_hex(0x8F8F8F), 0);

	//左下室内卡片
	lv_obj_t *card_in = lv_obj_create(screen_main);
	ui_no_scroll(card_in);
	lv_obj_set_size(card_in, 104, 140);
	lv_obj_align(card_in, LV_ALIGN_TOP_LEFT, 15, 165);
	lv_obj_set_style_bg_color(card_in, lv_color_white(), 0);
	lv_obj_set_style_radius(card_in, 8, 0);
	lv_obj_set_style_border_width(card_in, 0, 0);
	lv_obj_set_style_pad_all(card_in, 0, 0);

	lv_obj_t *title_in = lv_label_create(card_in);
	lv_obj_align(title_in, LV_ALIGN_TOP_MID, 0, 4);
	lv_label_set_text(title_in, STR_CN_SHINEI);
	lv_obj_set_width(title_in, 100);
	lv_label_set_long_mode(title_in, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_align(title_in, LV_TEXT_ALIGN_CENTER, 0);
	ui_style_cn_font(title_in);

	label_indoor_temp = lv_label_create(card_in);
	lv_obj_align(label_indoor_temp, LV_ALIGN_TOP_LEFT, 6, 32);
	lv_label_set_text(label_indoor_temp, "--");
	lv_obj_set_style_text_font(label_indoor_temp, &lv_font_montserrat_48, 0);

	lv_obj_t *unit_c1 = lv_label_create(card_in);
	lv_obj_align(unit_c1, LV_ALIGN_TOP_LEFT, 62, 36);
	lv_label_set_text(unit_c1, "\xC2\xB0" "C");	//℃ (° 为 UTF-8, 与 C 分开避免\x转义吞掉十六进制字符)
	lv_obj_set_style_text_font(unit_c1, &lv_font_montserrat_20, 0);

	label_indoor_hum = lv_label_create(card_in);
	lv_obj_align(label_indoor_hum, LV_ALIGN_TOP_LEFT, 6, 86);
	lv_label_set_text(label_indoor_hum, "--%");
	lv_obj_set_style_text_font(label_indoor_hum, &lv_font_montserrat_32, 0);

	//右下室外卡片
	card_out = lv_obj_create(screen_main);
	ui_no_scroll(card_out);
	lv_obj_set_size(card_out, 99, 140);
	lv_obj_align(card_out, LV_ALIGN_TOP_LEFT, 126, 165);
	lv_obj_set_style_bg_color(card_out, lv_color_white(), 0);
	lv_obj_set_style_radius(card_out, 8, 0);
	lv_obj_set_style_border_width(card_out, 0, 0);
	lv_obj_set_style_pad_all(card_out, 0, 0);

	label_outdoor_city = lv_label_create(card_out);
	lv_obj_align(label_outdoor_city, LV_ALIGN_TOP_LEFT, 6, 4);
	lv_label_set_text(label_outdoor_city, "--");
	lv_label_set_long_mode(label_outdoor_city, LV_LABEL_LONG_DOT); /* 单行, 超长省略号 */
	lv_obj_align(label_outdoor_city, LV_ALIGN_TOP_MID, 0, 4); /* 以 label 自身宽度居中 */
	ui_style_cn_font(label_outdoor_city);

	label_outdoor_temp = lv_label_create(card_out);
	lv_obj_align(label_outdoor_temp, LV_ALIGN_TOP_LEFT, 6, 32);
	lv_label_set_text(label_outdoor_temp, "--");
	lv_obj_set_style_text_font(label_outdoor_temp, &lv_font_montserrat_48, 0);

	lv_obj_t *unit_c2 = lv_label_create(card_out);
	lv_obj_align(unit_c2, LV_ALIGN_TOP_LEFT, 62, 36);
	lv_label_set_text(unit_c2, "\xC2\xB0" "C");	//℃
	lv_obj_set_style_text_font(unit_c2, &lv_font_montserrat_20, 0);

	img_outdoor_icon = lv_img_create(card_out);
	weather_icon_set("A:/icon_na.bin", NULL);
	lv_img_set_zoom(img_outdoor_icon, 150);	//约59%, 最大约31x31, 完全在卡片内
	lv_obj_align(img_outdoor_icon, LV_ALIGN_TOP_MID, 0, 86);

}

//布局诊断: 打印室外卡片/天气图标/WiFi图标的实际坐标和来源, 用于定位图标错位/串图问题
static void ui_dump_layout(void)
{
	if (!card_out || !img_outdoor_icon || !img_wifi)
		return;
	printf("[LVGL] card_out(%d,%d %dx%d) icon(%d,%d %dx%d zoom=%u type=%d) wifi(%d,%d %dx%d type=%d)\n",
		   lv_obj_get_x(card_out), lv_obj_get_y(card_out),
		   lv_obj_get_width(card_out), lv_obj_get_height(card_out),
		   lv_obj_get_x(img_outdoor_icon), lv_obj_get_y(img_outdoor_icon),
		   lv_obj_get_width(img_outdoor_icon), lv_obj_get_height(img_outdoor_icon),
		   lv_img_get_zoom(img_outdoor_icon),
		   (int)((lv_img_t *)img_outdoor_icon)->src_type,
		   lv_obj_get_x(img_wifi), lv_obj_get_y(img_wifi),
		   lv_obj_get_width(img_wifi), lv_obj_get_height(img_wifi),
		   (int)((lv_img_t *)img_wifi)->src_type);
}

//第二页: 空白占位
/* ==================== WiFi 设置界面 (第二页) ==================== */

/* 文件查看回调前置声明 (供第二页按钮使用) */
static void ui_file_enter_click_cb(lv_event_t *e);
static void ui_file_view_click_cb(lv_event_t *e);

/* workqueue 回调包装: 解析 "ssid\npass" 并调 wifi_reconnect */
static void wifi_reconnect_async(void *param)
{
	char *buf = (char *)param;
	char *nl = strchr(buf, '\n');
	if (nl)
	{
		*nl = '\0';
		wifi_reconnect(buf, nl + 1);
	}
	free(buf);
}

/* 收键盘/进入时: 停止光标闪烁动画 + 光标样式透明, 点击输入框才恢复可见 */
static void ui_kb_hide_cursor(lv_obj_t *ta)
{
	if (!ta) return;
	lv_anim_del(ta, NULL);	/* 停止闪烁动画, cursor.show 固定 */
	lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR); /* 隐形 */
	lv_obj_set_style_border_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
}
static void ui_kb_show_cursor(lv_obj_t *ta)
{
	if (!ta) return;
	lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);  /* 可见 */
	lv_obj_set_style_border_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
	/* 显式触发聚焦事件, 确保光标闪烁动画启动(否则第一次点击不闪) */
	lv_event_send(ta, LV_EVENT_FOCUSED, NULL);
}

/* 保存 SSID/密码到 FlashDB 并重连, 然后回到入口页 */
static void ui_wifi_do_save(lv_event_t *e)
{
	(void)e;
	const char *ssid = lv_textarea_get_text(ta_ssid);
	const char *pass = lv_textarea_get_text(ta_pass);
	storage_wifi_set(ssid, pass);
	/* 存配置后异步重连: 避免在 LVGL 回调里阻塞 + 与串口轮询任务抢占用 */
	char *buf = malloc(128);
	if (buf)
	{
		snprintf(buf, 128, "%s\n%s", ssid, pass);
		workqueue_run((work_t)wifi_reconnect_async, buf);
	}
	ui_kb_hide_cursor(ta_ssid);
	ui_kb_hide_cursor(ta_pass);
	kb_active = false;
	lv_scr_load(scr_wifi_entry);
}

/* 点击"取消": 关闭键盘, 回到入口页 */
static void ui_cancel_click_cb(lv_event_t *e)
{
	(void)e;
	ui_kb_hide_cursor(ta_ssid);
	ui_kb_hide_cursor(ta_pass);
	kb_active = false;
	lv_scr_load(scr_wifi_entry);
}

/* 键盘按键事件 */
static void ui_kb_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	/* 打勾(READY)/键盘切换键(CANCEL): 只收起键盘, 不退出, 不保存。
	 * 保存/退出分别由"保存"和"取消"按钮处理。 */
	if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
	{
		lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
		ui_kb_hide_cursor(ta_ssid);
		ui_kb_hide_cursor(ta_pass);
		kb_active = false;
	}
}

/* 点击"设置 WiFi"按钮: 预填配置, 弹出键盘, 进入编辑页 */
static void ui_wifi_enter_click_cb(lv_event_t *e)
{
	char ssid[64], pass[64];
	(void)e;
	if (storage_wifi_get(ssid, sizeof(ssid), pass, sizeof(pass)))
	{
		lv_textarea_set_text(ta_ssid, ssid);
		lv_textarea_set_text(ta_pass, pass);
	}
	else
	{
		lv_textarea_set_text(ta_ssid, WIFI_SSID);
		lv_textarea_set_text(ta_pass, WIFI_PASSWD);
	}
	lv_keyboard_set_textarea(kb, ta_ssid);
	/* 进入设置界面时强制密码隐藏(圆点), 即使上次退出时点过"显示密码" */
	lv_textarea_set_password_mode(ta_pass, true);
	lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_OPEN);
	kb_active = false;		/* 键盘默认收起, 点击输入框才弹出 */
	lv_scr_load(scr_wifi_set);
	/* lv_scr_load 之后 indev 会自动聚焦第一个 CLICK_FOCUSABLE 对象,
	 * 这里再清除, 保证刚进入时光标不显示, 点击输入框时才出现 */
	ui_kb_hide_cursor(ta_ssid);
	ui_kb_hide_cursor(ta_pass);
}

/* 点击 SSID/密码框时切换键盘目标 */
static void ui_ta_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	if (code == LV_EVENT_CLICKED)
	{
		if (e->target == ta_ssid)
		{
			lv_keyboard_set_textarea(kb, ta_ssid);
			lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
			ui_kb_show_cursor(ta_ssid);
			ui_kb_hide_cursor(ta_pass);
			kb_active = true;
		}
		else if (e->target == ta_pass)
		{
			lv_keyboard_set_textarea(kb, ta_pass);
			lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
			ui_kb_show_cursor(ta_pass);
			ui_kb_hide_cursor(ta_ssid);
			kb_active = true;
		}
	}
}

/* 点击屏幕空白处: 收起键盘 */
static void ui_scr_click_cb(lv_event_t *e)
{
	(void)e;
	lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
	/* 点击空白: 取消输入框聚焦, 光标消失 */
	ui_kb_hide_cursor(ta_ssid);
	ui_kb_hide_cursor(ta_pass);
	kb_active = false;
}

/* 显示/隐藏密码: 切换 textarea 的密码模式 */
static void ui_eye_click_cb(lv_event_t *e)
{
	(void)e;
	bool pw = lv_textarea_get_password_mode(ta_pass);
	lv_textarea_set_password_mode(ta_pass, !pw);
	/* 密码可见时显示"闭眼"(点击隐藏), 隐藏时显示"开眼"(点击显示) */
	lv_label_set_text(lbl_eye, pw ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

/* 构建 WiFi 编辑页: 输入框 + 键盘 + 按钮 */
static void ui_build_wifi_set_screen(void)
{
	lv_obj_t *title, *lbl, *btn;

	scr_wifi_set = lv_obj_create(NULL);
	ui_no_scroll(scr_wifi_set);
	lv_obj_set_style_bg_color(scr_wifi_set, lv_color_black(), 0);
	lv_obj_add_flag(scr_wifi_set, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(scr_wifi_set, ui_scr_click_cb, LV_EVENT_CLICKED, NULL);

	title = lv_label_create(scr_wifi_set);
	lv_label_set_text(title, STR_CN_WIFI_S);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	ui_style_cn_font(title);

	lbl = lv_label_create(scr_wifi_set);
	lv_label_set_text(lbl, STR_CN_SSID);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), 0);
	lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 40);
	ui_style_cn_font(lbl);

	ta_ssid = lv_textarea_create(scr_wifi_set);
	lv_obj_set_size(ta_ssid, 220, 34);
	lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 10, 62);
	lv_textarea_set_one_line(ta_ssid, true);
	lv_textarea_set_max_length(ta_ssid, 32);
	lv_textarea_set_cursor_click_pos(ta_ssid, true);	/* 显示闪烁光标, 可点击定位 */
	lv_obj_set_style_bg_color(ta_ssid, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
	lv_obj_set_style_bg_opa(ta_ssid, LV_OPA_COVER, LV_PART_CURSOR);
	lv_obj_set_style_anim_time(ta_ssid, 400, LV_PART_CURSOR);
	lv_obj_set_style_bg_color(ta_ssid, lv_color_hex(0x222222), 0);
	lv_obj_set_style_text_color(ta_ssid, lv_color_white(), 0);
	lv_obj_add_event_cb(ta_ssid, ui_ta_event_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_clear_flag(ta_ssid, LV_OBJ_FLAG_SCROLLABLE);

	lbl = lv_label_create(scr_wifi_set);
	lv_label_set_text(lbl, STR_CN_PASS);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), 0);
	lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 104);
	ui_style_cn_font(lbl);

	ta_pass = lv_textarea_create(scr_wifi_set);
	lv_obj_set_size(ta_pass, 180, 34);	/* 右侧留出 40px 放显示/隐藏按钮 */
	lv_obj_align(ta_pass, LV_ALIGN_TOP_LEFT, 10, 126);
	lv_textarea_set_one_line(ta_pass, true);
	lv_textarea_set_password_mode(ta_pass, true);
	lv_textarea_set_password_show_time(ta_pass, 0);	/* 关闭密码末尾位显示 */
	lv_textarea_set_max_length(ta_pass, 64);
	lv_textarea_set_cursor_click_pos(ta_pass, true);	/* 显示闪烁光标 */
	lv_obj_set_style_bg_color(ta_pass, lv_color_hex(0xFFFFFF), LV_PART_CURSOR);
	lv_obj_set_style_bg_opa(ta_pass, LV_OPA_COVER, LV_PART_CURSOR);
	lv_obj_set_style_anim_time(ta_pass, 400, LV_PART_CURSOR);
	lv_obj_set_style_bg_color(ta_pass, lv_color_hex(0x222222), 0);
	lv_obj_set_style_text_color(ta_pass, lv_color_white(), 0);
	lv_obj_add_event_cb(ta_pass, ui_ta_event_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_clear_flag(ta_pass, LV_OBJ_FLAG_SCROLLABLE);

	/* 显示/隐藏密码按钮 (密码框右侧) */
	btn_eye = lv_btn_create(scr_wifi_set);
	lv_obj_set_size(btn_eye, 36, 34);
	lv_obj_align(btn_eye, LV_ALIGN_TOP_LEFT, 200, 126);
	lv_obj_add_event_cb(btn_eye, ui_eye_click_cb, LV_EVENT_CLICKED, NULL);
	lbl_eye = lv_label_create(btn_eye);
	lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_OPEN);
	lv_obj_set_style_text_font(lbl_eye, &lv_font_montserrat_20, 0);
	lv_obj_center(lbl_eye);

	btn = lv_btn_create(scr_wifi_set);
	lv_obj_set_size(btn, 104, 40);
	lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 12, 168);
	lv_obj_add_event_cb(btn, ui_wifi_do_save, LV_EVENT_CLICKED, NULL);
	lbl = lv_label_create(btn);
	lv_label_set_text(lbl, STR_CN_SAVE);
	lv_obj_center(lbl);
	ui_style_cn_font(lbl);

	btn = lv_btn_create(scr_wifi_set);
	lv_obj_set_size(btn, 104, 40);
	lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -12, 168);
	lv_obj_add_event_cb(btn, ui_cancel_click_cb, LV_EVENT_CLICKED, NULL);
	lbl = lv_label_create(btn);
	lv_label_set_text(lbl, STR_CN_BACK);
	lv_obj_center(lbl);
	ui_style_cn_font(lbl);

	kb = lv_keyboard_create(scr_wifi_set);
	lv_obj_set_size(kb, 240, 140);
	lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_keyboard_set_textarea(kb, ta_ssid);
	lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);		/* 默认收起, 点击输入框才弹出 */
	lv_obj_add_event_cb(kb, ui_kb_event_cb, LV_EVENT_READY, NULL);
	lv_obj_add_event_cb(kb, ui_kb_event_cb, LV_EVENT_CANCEL, NULL);
}

/* 构建第二页 WiFi 设置入口界面 */
static void ui_build_wifi_entry_screen(void)
{
	lv_obj_t *title, *btn, *lbl, *st;

	scr_wifi_entry = lv_obj_create(NULL);
	ui_no_scroll(scr_wifi_entry);
	lv_obj_set_style_bg_color(scr_wifi_entry, lv_color_black(), 0);

	btn = lv_btn_create(scr_wifi_entry);
	lv_obj_set_size(btn, 200, 60);
	lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 20);
	lv_obj_add_event_cb(btn, ui_wifi_enter_click_cb, LV_EVENT_CLICKED, NULL);
	lbl = lv_label_create(btn);
	lv_label_set_text(lbl, STR_CN_WIFI_S);
	lv_obj_center(lbl);
	ui_style_cn_font(lbl);

	btn = lv_btn_create(scr_wifi_entry);
	lv_obj_set_size(btn, 200, 60);
	lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 92);
	lv_obj_add_event_cb(btn, ui_file_enter_click_cb, LV_EVENT_CLICKED, NULL);
	lbl = lv_label_create(btn);
	lv_label_set_text(lbl, STR_CN_FILES);
	lv_obj_center(lbl);
	ui_style_cn_font(lbl);

	(void)title; (void)st;
}

/* ==================== 文件查看 (W25Q128 图片) ==================== */

/* 拼当前目录下的完整 littlefs 路径 */
static void ui_dir_path(const char *name, char *out, size_t out_sz)
{
	if (strcmp(cur_dir, "/") == 0)
		snprintf(out, out_sz, "/%s", name);
	else
		snprintf(out, out_sz, "%s/%s", cur_dir, name);
}

/* 返回上一级目录 */
static void ui_dir_up(void)
{
	char *slash = strrchr(cur_dir, '/');
	if (slash && slash != cur_dir)
		*slash = '\0';
	else
		strcpy(cur_dir, "/");
}

/* 拼 A:/ 前缀的预览文件路径(避免 ui_dir_path 的斜杠导致 A://) */
static void ui_build_preview_path(const char *name)
{
	if (strcmp(cur_dir, "/") == 0)
		snprintf(file_preview_path, sizeof(file_preview_path), "A:/%s", name);
	else
		snprintf(file_preview_path, sizeof(file_preview_path), "A:%s/%s", cur_dir, name);
}

/* 创建一行(按钮+标签); 分配失败返回 NULL, 调用方跳过该行 */
static lv_obj_t *ui_row_create(void)
{
	lv_obj_t *b = lv_btn_create(list_file);
	if (b == NULL)
		return NULL;
	lv_obj_set_size(b, 232, 34);
	lv_obj_set_style_pad_all(b, 0, 0);
	lv_obj_set_style_radius(b, 0, 0);
	lv_obj_add_event_cb(b, ui_file_view_click_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *lbl = lv_label_create(b);
	if (lbl == NULL)
		return NULL;
	lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
	lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
	ui_style_cn_font(lbl);
	return b;
}

/* 给列表一个高度 = 条目数*36 的透明占位, 使 LVGL 有完整的滚动范围 */
static void ui_file_list_set_extent(void)
{
	if (list_spacer == NULL)
	{
		list_spacer = lv_obj_create(list_file);
		if (list_spacer == NULL)
			return;
		lv_obj_clear_flag(list_spacer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_bg_opa(list_spacer, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(list_spacer, 0, 0);
		lv_obj_set_style_pad_all(list_spacer, 0, 0);
		lv_obj_set_pos(list_spacer, 0, 0);
	}
	lv_obj_set_size(list_spacer, 232, (lv_coord_t)(file_entry_cnt * 36));
}

/* 按滚动位置把可见行映射到 file_entries 索引 */
static void ui_file_list_update_rows(void)
{
	int scroll_y, first, i;
	if (!file_rows_ready || list_file == NULL)
		return;
	scroll_y = lv_obj_get_scroll_y(list_file);
	if (scroll_y < 0)
		scroll_y = 0;
	first = scroll_y / 36;
	for (i = 0; i < FILE_LIST_ROWS; i++)
	{
		int idx = first + i;
		lv_obj_t *b = file_rows[i];
		if (b == NULL)
			continue;
		if (idx < file_entry_cnt)
		{
			lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_pos(b, 0, idx * 36);
			lv_obj_set_user_data(b, (void *)(uintptr_t)file_entries[idx].is_dir);
			lv_label_set_text(lv_obj_get_child(b, 0), file_entries[idx].name);
		}
		else
			lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
	}
}

static void ui_file_list_scroll_cb(lv_event_t *e)
{
	(void)e;
	ui_file_list_update_rows();
}

/* 给定当前文件名, 在排序后的 file_entries 里找前一个/后一个图片 (dir<0 前一个, dir>0 后一个)。
 * 与列表显示顺序一致, 不占额外内存。*/
static bool file_list_neighbor(const char *cur, int dir, char *out, size_t out_sz)
{
	int i;
	for (i = 0; i < file_entry_cnt; i++)
	{
		if (file_entries[i].is_dir || strcmp(file_entries[i].name, cur) != 0)
			continue;
		{
			int j = (dir > 0) ? i + 1 : i - 1;
			while (j >= 0 && j < file_entry_cnt && file_entries[j].is_dir)
				j += (dir > 0) ? 1 : -1;
			if (j < 0 || j >= file_entry_cnt)
				return false;
			strncpy(out, file_entries[j].name, out_sz - 1);
			out[out_sz - 1] = '\0';
			return true;
		}
	}
	return false;
}

/* 自然排序: 数字段按数值比较, 字母大小写不敏感 (类似资源管理器) */
static int file_entry_cmp(const void *a, const void *b)
{
	const char *s1 = ((const file_entry_t *)a)->name;
	const char *s2 = ((const file_entry_t *)b)->name;
	while (*s1 && *s2)
	{
		if (isdigit((unsigned char)*s1) && isdigit((unsigned char)*s2))
		{
			/* 跳过前导零, 比较数字长度与数值 */
			const char *p1 = s1, *p2 = s2;
			const char *q1, *q2;
			size_t l1, l2;
			int c;
			while (*p1 == '0') p1++;
			while (*p2 == '0') p2++;
			q1 = p1; q2 = p2;
			while (isdigit((unsigned char)*q1)) q1++;
			while (isdigit((unsigned char)*q2)) q2++;
			l1 = (size_t)(q1 - p1);
			l2 = (size_t)(q2 - p2);
			if (l1 != l2)
				return l1 < l2 ? -1 : 1;
			c = strncmp(p1, p2, l1);
			if (c != 0)
				return c < 0 ? -1 : 1;
			s1 = q1; s2 = q2;
			continue;
		}
		{
			int c1 = tolower((unsigned char)*s1);
			int c2 = tolower((unsigned char)*s2);
			if (c1 != c2)
				return c1 < c2 ? -1 : 1;
			s1++; s2++;
		}
	}
	if (*s1) return 1;
	if (*s2) return -1;
	return 0;
}

/* 枚举当前目录: 文件夹 + .bin 文件, 填入虚拟列表索引 */
static void ui_file_list_refresh(void)
{
	lfs_t *lfs = storage_lfs_get();
	lfs_dir_t dir;
	struct lfs_info info;
	bool capped = false;
	int folder_cnt = 0;

	printf("[FILE] refresh start\n");
	file_entry_cnt = 0;

	storage_lfs_lock();
	printf("[FILE] locked\n");
	if (lfs_dir_open(lfs, &dir, cur_dir) < 0)
	{
		printf("[FILE] dir_open fail: %s\n", cur_dir);
		storage_lfs_unlock();
		return;
	}
	printf("[FILE] enuming %s...\n", cur_dir);

	/* 第一遍: 文件夹 */
	while (lfs_dir_read(lfs, &dir, &info) > 0)
	{
		if (info.type != LFS_TYPE_DIR || info.name[0] == '~' ||
		    strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
			continue;
		if (file_entry_cnt >= FILE_LIST_MAX)
		{
			capped = true;
			continue;
		}
		strncpy(file_entries[file_entry_cnt].name, info.name,
		        sizeof(file_entries[0].name) - 1);
		file_entries[file_entry_cnt].name[sizeof(file_entries[0].name) - 1] = 0;
		file_entries[file_entry_cnt].is_dir = 1;
		file_entry_cnt++;
		folder_cnt++;
	}

	/* 第二遍: .bin 图片文件 */
	lfs_dir_close(lfs, &dir);
	if (lfs_dir_open(lfs, &dir, cur_dir) < 0)
	{
		printf("[FILE] dir_open fail: %s\n", cur_dir);
		storage_lfs_unlock();
		return;
	}
	while (lfs_dir_read(lfs, &dir, &info) > 0)
	{
		if (info.type != LFS_TYPE_REG || info.name[0] == '~')
			continue;
		/* 只列 .bin 图片文件 */
		if (strstr(info.name, ".bin") == NULL)
			continue;
		if (file_entry_cnt >= FILE_LIST_MAX)
		{
			capped = true;
			continue;
		}
		strncpy(file_entries[file_entry_cnt].name, info.name,
		        sizeof(file_entries[0].name) - 1);
		file_entries[file_entry_cnt].name[sizeof(file_entries[0].name) - 1] = 0;
		file_entries[file_entry_cnt].is_dir = 0;
		file_entry_cnt++;
	}
	lfs_dir_close(lfs, &dir);
	storage_lfs_unlock();

	/* 文件夹、图片各自按名称排序, 滑动切换与显示顺序一致 */
	if (folder_cnt > 1)
		qsort(file_entries, (size_t)folder_cnt, sizeof(file_entry_t), file_entry_cmp);
	if (file_entry_cnt - folder_cnt > 1)
		qsort(file_entries + folder_cnt, (size_t)(file_entry_cnt - folder_cnt),
		      sizeof(file_entry_t), file_entry_cmp);

	/* 首次进入时创建固定行数控件 */
	if (!file_rows_ready)
	{
		int i;
		for (i = 0; i < FILE_LIST_ROWS; i++)
			file_rows[i] = ui_row_create();
		file_rows_ready = true;
	}

	if (list_file)
		lv_obj_scroll_to_y(list_file, 0, LV_ANIM_OFF);
	ui_file_list_set_extent();
	ui_file_list_update_rows();
	if (capped)
		printf("[FILE] list capped at %d entries\n", FILE_LIST_MAX);
	printf("[FILE] refresh done, entries=%d\n", file_entry_cnt);
}

/* 点击列表项: 文件夹进入, 文件记录文件名并进入预览 */
static void ui_file_view_click_cb(lv_event_t *e)
{
	lv_obj_t *btn = lv_event_get_target(e);
	lv_obj_t *lbl = lv_obj_get_child(btn, 0);
	const char *txt = lbl ? lv_label_get_text(lbl) : NULL;
	if (!txt)
		return;

	if (lv_obj_get_user_data(btn))
	{
		char path[72];
		ui_dir_path(txt, path, sizeof(path));
		strncpy(cur_dir, path, sizeof(cur_dir) - 1);
		cur_dir[sizeof(cur_dir) - 1] = '\0';
		printf("[FILE] enter %s\n", cur_dir);
		ui_file_list_refresh();
		return;
	}

	strncpy(cur_file, txt, sizeof(cur_file) - 1);
	cur_file[sizeof(cur_file) - 1] = '\0';
	printf("[FILE] view %s\n", cur_file);
	ui_build_preview_path(cur_file);
	/* TRUE_COLOR 文件由 LVGL 内建解码器按行流式读取, 支持 240x320 且不占大 RAM */
	lv_img_set_src(img_file_preview, file_preview_path);
	lv_scr_load(scr_file_view);
}

/* 横划切换图片: 左划=下一张, 右划=上一张 */
static void ui_file_preview_gesture_cb(lv_event_t *e)
{
	lv_indev_t *indev = lv_event_get_indev(e);
	if (indev == NULL)
		return;
	lv_dir_t dir = lv_indev_get_gesture_dir(indev);
	if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT)
		return;

	int step = (dir == LV_DIR_LEFT) ? 1 : -1;
	char name[40];
	if (!file_list_neighbor(cur_file, step, name, sizeof(name)))
		return;

	strncpy(cur_file, name, sizeof(cur_file) - 1);
	cur_file[sizeof(cur_file) - 1] = '\0';
	ui_build_preview_path(cur_file);
	lv_img_set_src(img_file_preview, file_preview_path);
	printf("[FILE] swipe %s\n", cur_file);
}

/* 点击"返回": 回到文件列表 */
static void ui_file_back_click_cb(lv_event_t *e)
{
	(void)e;
	lv_scr_load(scr_file_list);
}

/* 列表页返回: 子目录时返回上一级, 根目录时回到第二页入口 */
static void ui_file_back_to_entry_cb(lv_event_t *e)
{
	(void)e;
	if (strcmp(cur_dir, "/") != 0)
	{
		ui_dir_up();
		ui_file_list_refresh();
	}
	else
		lv_scr_load(scr_wifi_entry);
}

/* 第二页点击"文件查看": 进入文件列表页 */
static void ui_file_enter_click_cb(lv_event_t *e)
{
	(void)e;
	strcpy(cur_dir, "/");
	ui_file_list_refresh();
	lv_scr_load(scr_file_list);
}

/* 构建文件列表页 */
static void ui_build_file_list_screen(void)
{
	lv_obj_t *title, *lbl, *btn;

	scr_file_list = lv_obj_create(NULL);
	ui_no_scroll(scr_file_list);
	lv_obj_set_style_bg_color(scr_file_list, lv_color_black(), 0);

	title = lv_label_create(scr_file_list);
	lv_label_set_text(title, STR_CN_FILES);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	ui_style_cn_font(title);

	list_file = lv_obj_create(scr_file_list);
	lv_obj_set_size(list_file, 232, 240);
	lv_obj_align(list_file, LV_ALIGN_TOP_MID, 0, 32);
	lv_obj_set_style_bg_color(list_file, lv_color_hex(0x222222), 0);
	lv_obj_set_style_text_color(list_file, lv_color_white(), 0);
	lv_obj_set_style_pad_all(list_file, 0, 0);
	/* 允许上下滚动, 避免按钮超出容器被裁剪而无法查看 */
	lv_obj_add_flag(list_file, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(list_file, LV_DIR_VER);
	lv_obj_set_style_radius(list_file, 4, 0);
	lv_obj_add_event_cb(list_file, ui_file_list_scroll_cb, LV_EVENT_SCROLL, NULL);

	btn = lv_btn_create(scr_file_list);
	lv_obj_set_size(btn, 200, 40);
	lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -4);
	/* 列表页返回 -> 回到第二页入口 */
	lv_obj_add_event_cb(btn, ui_file_back_to_entry_cb, LV_EVENT_CLICKED, NULL);
	lbl = lv_label_create(btn);
	lv_label_set_text(lbl, STR_CN_BACK);
	lv_obj_center(lbl);
	ui_style_cn_font(lbl);
}

/* 构建图片预览页 */
static void ui_build_file_view_screen(void)
{
	lv_obj_t *lbl, *btn;

	scr_file_view = lv_obj_create(NULL);
	ui_no_scroll(scr_file_view);
	lv_obj_set_style_bg_color(scr_file_view, lv_color_black(), 0);

	/* 整屏显示 240x320 图片(流式读 flash, 尺寸由图片本身决定) */
	img_file_preview = lv_img_create(scr_file_view);
	lv_obj_align(img_file_preview, LV_ALIGN_CENTER, 0, 0);

	/* 手势冒泡到屏, 用横划切换图片 */
	lv_obj_add_flag(img_file_preview, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_add_event_cb(scr_file_view, ui_file_preview_gesture_cb, LV_EVENT_GESTURE, NULL);

	/* 返回按钮后创建 -> 绘制在图片图层之上, 始终可见可点 */
	btn = lv_btn_create(scr_file_view);
	lv_obj_set_size(btn, 60, 40);
	lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
	lv_obj_set_style_bg_opa(btn, 13, 0);
	lv_obj_add_event_cb(btn, ui_file_back_click_cb, LV_EVENT_CLICKED, NULL);
	lbl = lv_label_create(btn);
	lv_label_set_text(lbl, STR_CN_BACK);
	lv_obj_center(lbl);
	lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
	ui_style_cn_font(lbl);
}

/* 第二页: WiFi 设置 (原空白页) */
static void ui_build_blank_screen(void)
{
	screen_blank = scr_wifi_entry;
}

//把 app 任务缓存的数据刷新到控件 (必须在 lvgl 任务里调用)
static void ui_apply_pending(void)
{
	char buf[32];

	if (data_dirty & DATA_DIRTY_TIME)
	{
		if (data_time.year >= 2020)
		{
			//冒号按秒闪烁: 用 LVGL 文字变色把冒号在黑/白之间切换,
			//字符串宽度始终不变, 时间数字不会左右移动, 只刷新冒号区域
			//注意: recolor 命令格式为 "#RRGGBB 文字#", 颜色码后必须带空格, 否则#会被画出来
			snprintf(buf, sizeof(buf), "%02u#%06X :#%02u", data_time.hour,
					 (data_time.second & 1) ? 0x000000 : 0xFFFFFF, data_time.minute);
			lv_label_set_text(label_time, buf);
			snprintf(buf, sizeof(buf), "%04u/%02u/%02u", data_time.year, data_time.month, data_time.day);
			lv_label_set_text(label_date, buf);
		}
		data_dirty &= ~DATA_DIRTY_TIME;
	}

	if (data_dirty & DATA_DIRTY_WIFI)
	{
		lv_label_set_text(label_wifi_ssid, data_wifi_connected ? data_ssid : STR_CN_WANGLUO);
		wifi_icon_set(data_wifi_connected ? "A:/icon_wifi.bin" : "A:/icon_no_wifi.bin",
					  NULL);
		data_dirty &= ~DATA_DIRTY_WIFI;
	}

	if (data_dirty & DATA_DIRTY_INDOOR)
	{
		snprintf(buf, sizeof(buf), "%.0f", data_temperature);
		lv_label_set_text(label_indoor_temp, buf);
		snprintf(buf, sizeof(buf), "%.0f%%", data_humidity);
		lv_label_set_text(label_indoor_hum, buf);
		data_dirty &= ~DATA_DIRTY_INDOOR;
	}

	if (data_dirty & DATA_DIRTY_OUTDOOR)
	{
		snprintf(buf, sizeof(buf), "%s %s", data_city, data_weather_text);
		lv_label_set_text(label_outdoor_city, buf);
		snprintf(buf, sizeof(buf), "%.0f", data_outdoor_temp);
		lv_label_set_text(label_outdoor_temp, buf);
		weather_icon_set(weather_icon_path(data_weather_code), NULL);
		lv_obj_align(img_outdoor_icon, LV_ALIGN_TOP_MID, 0, 86);	//换图后重新对齐, 防止尺寸变化导致位置偏移
		printf("[LVGL] outdoor: %s %s %.0fC icon=%d\n", data_city, data_weather_text, data_outdoor_temp, data_weather_code);
		data_dirty &= ~DATA_DIRTY_OUTDOOR;
	}
}

static void lvgl_task(void *param)
{
	uint32_t ticks = 0;
	while (1)
	{
		ui_apply_pending();
		lv_timer_handler();
		/* 编辑页且键盘未弹出时, 强制清除输入框焦点 -> 光标不显示。
		 * 避免 lv_scr_load 或文本预填后 indev 自动聚焦导致进入页面光标闪烁。 */
		if (!kb_active && scr_wifi_set && lv_disp_get_scr_act(NULL) == scr_wifi_set)
		{
	ui_kb_hide_cursor(ta_ssid);
	ui_kb_hide_cursor(ta_pass);
		}
		if (++ticks == 20)	//约100ms后布局稳定, 打印一次实际坐标
			ui_dump_layout();
		vTaskDelay(pdMS_TO_TICKS(5));
	}
}

void lvgl_ui_init(void)
{
	lv_init();
	lv_fs_lfs_init();
	font_cn = &lv_font_cn;	//不再运行时从 flash 加载, 省 ~14KB LVGL 堆
	font_wifi_small = lv_font_montserrat_14;	//WiFi名称用14px
	font_wifi_small.fallback = font_cn;			//中文(如"网络断开")回退到加载的字库
	lv_port_disp_init();
	lv_port_indev_init();

	ui_build_main_screen();
	ui_build_wifi_entry_screen();
	ui_build_wifi_set_screen();
	ui_build_file_list_screen();
	ui_build_file_view_screen();
	ui_build_blank_screen();
	lv_scr_load(screen_main);

	xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);
}

//滑动切换页面 (由 lv_port_indev 在抬起时调用)
void lvgl_ui_on_gesture(int dir)
{
	/* 键盘弹出时不响应滑动翻页, 避免误触 */
	if (kb_active)
		return;
	/* 处于 WiFi 设置页或文件查看/预览页时, 不响应滑动翻页 */
	if (scr_wifi_set && lv_disp_get_scr_act(NULL) == scr_wifi_set)
		return;
	if (scr_file_list && lv_disp_get_scr_act(NULL) == scr_file_list)
		return;
	if (scr_file_view && lv_disp_get_scr_act(NULL) == scr_file_view)
		return;

	if (dir == LVGL_GESTURE_LEFT)
		current_page = (current_page + 1) % UI_PAGE_COUNT;
	else if (dir == LVGL_GESTURE_RIGHT)
		current_page = (current_page + UI_PAGE_COUNT - 1) % UI_PAGE_COUNT;
	else
		return;

	switch (current_page)
	{
		case UI_PAGE_MAIN:
			lv_scr_load(screen_main);
			break;
		case UI_PAGE_BLANK:
			lv_scr_load(screen_blank);
			break;
	}

	printf("[LVGL] page %d\n", current_page);
}

void lvgl_ui_set_time(const rtc_date_time_t *dt)
{
	data_time = *dt;
	data_dirty |= DATA_DIRTY_TIME;
}

void lvgl_ui_set_wifi(const char *ssid, bool connected)
{
	strncpy(data_ssid, ssid, sizeof(data_ssid) - 1);
	data_ssid[sizeof(data_ssid) - 1] = '\0';
	data_wifi_connected = connected;
	data_dirty |= DATA_DIRTY_WIFI;
}

void lvgl_ui_set_indoor(float temperature, float humidity)
{
	data_temperature = temperature;
	data_humidity = humidity;
	data_dirty |= DATA_DIRTY_INDOOR;
}

void lvgl_ui_set_outdoor(const char *city, float temperature, int weather_code, const char *text)
{
	(void)text;	//天气文字按 code 映射为中文

	strncpy(data_city, city, sizeof(data_city) - 1);
	data_city[sizeof(data_city) - 1] = '\0';
	if (strcmp(data_city, "Longyan") == 0)
		strcpy(data_city, STR_CN_LONGYAN);
	else if (strcmp(data_city, "Xiamen") == 0)
		strcpy(data_city, STR_CN_XIAMEN);

	strncpy(data_weather_text, weather_text_cn(weather_code), sizeof(data_weather_text) - 1);
	data_weather_text[sizeof(data_weather_text) - 1] = '\0';
	data_outdoor_temp = temperature;
	data_weather_code = weather_code;
	data_dirty |= DATA_DIRTY_OUTDOOR;
}
