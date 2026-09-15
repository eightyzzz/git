/*
 * usb_mtp.c - STM32F407 OTG_FS 上的 USB MTP 设备（PA11=USB_D-，PA12=USB_D+）
 *
 * 硬件链路：Type-C -> CH334 HUB -> CH334_D3+/D3- -> STM32_USB_D+/D- -> PA12/PA11
 * 存储：向 PC 以 MTP 存储卷形式暴露 W25Q128_2（LittleFS）。
 *
 * 软件栈：Keil CMSIS-Driver USBD_FS_STM32F4xx.c（Driver_USBD0）+ 本文件的 MTP 协议实现。
 * 仅使用 USB 全速设备模式；未使能 VBUS 检测（PA9 已被 USART1 串口占用）。
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lfs.h"
#include "lfs_port.h"
#include "flash_partition.h"

#include "Driver_USBD.h"
#include "OTG_FS_STM32F4xx.h"

#include "rtc.h"
#include "tim_delay.h"
#include "usb_mtp.h"

 /* 由 third_lib/usb/USBD_FS_STM32F4xx.c 提供 */
extern ARM_DRIVER_USBD Driver_USBD0;
extern void USBD_FS_IRQ(uint32_t gintsts);

 /* 在 OTG_FS_IRQHandler 中自增，用于诊断 */
static volatile uint32_t usb_irq_count;
static volatile uint32_t usb_last_gint;
#define USB_GINT_HIST_SIZE 8
static volatile uint32_t usb_gint_hist[USB_GINT_HIST_SIZE];
static volatile uint32_t usb_gint_hist_cnt;

 /* ------------------------- 配置 ------------------------- */
#define MTP_STORAGE_ID          0x00010001UL
#define MTP_TMP_NAME            "~mtp.tmp"
#define MTP_PENDING_MAX         4
#define MTP_RECENT_MAX          8
#define MTP_NAME_MAX            64
#define MTP_PATH_MAX            96
#define MTP_STREAM_SIZE         2048
#define MTP_PROP_SIZE           3072
#define MTP_PENDING_BASE        0x0000F000UL
#define FORMAT_ASSOC            0x3001
#define MTP_CACHE_MAX           48
#define MTP_CACHE_POOL          1536

#define EP_BULK_OUT             0x01
#define EP_BULK_IN              0x82
#define EP_INTR_IN              0x83

 /* MTP 容器类型 */
#define MTP_TYPE_COMMAND        1
#define MTP_TYPE_DATA           2
#define MTP_TYPE_RESPONSE       3

 /* MTP 操作码 */
#define OP_GET_DEVICE_INFO          0x1001
#define OP_OPEN_SESSION             0x1002
#define OP_CLOSE_SESSION            0x1003
#define OP_GET_STORAGE_IDS          0x1004
#define OP_GET_STORAGE_INFO         0x1005
#define OP_GET_NUM_OBJECTS          0x1006
#define OP_GET_OBJECT_HANDLES       0x1007
#define OP_GET_OBJECT_INFO          0x1008
#define OP_GET_OBJECT               0x1009
#define OP_GET_THUMB                0x100A
#define OP_DELETE_OBJECT            0x100B
#define OP_SEND_OBJECT_INFO         0x100C
#define OP_SEND_OBJECT              0x100D
#define OP_INITIATE_CAPTURE         0x100E
#define OP_COPY_OBJECT              0x1017
#define OP_MOVE_OBJECT              0x1018
#define OP_GET_DEVICE_PROP_DESC     0x1014
#define OP_GET_DEVICE_PROP_VALUE    0x1015
#define OP_SET_DEVICE_PROP_VALUE    0x1016
 /* 微软 MTP 扩展：对象属性相关操作（MTP 规范 D.x） */
#define OP_GET_OBJECT_PROPS_SUPPORT 0x9801
#define OP_GET_OBJECT_PROP_DESC     0x9802
#define OP_GET_OBJECT_PROP_VALUE    0x9803
#define OP_SET_OBJECT_PROP_VALUE    0x9804
#define OP_GET_OBJECT_PROP_LIST     0x9805
#define OP_SET_OBJECT_PROP_LIST     0x9806
#define OP_GET_OBJECT_REFS          0x9809   /* 0x9807 is GetInterdependentPropDesc */
#define OP_SET_OBJECT_REFS          0x9808

 /* MTP 响应码 */
#define RSP_OK                      0x2001
#define RSP_GENERAL_ERROR           0x2002
#define RSP_OPERATION_NOT_SUPPORTED 0x2005
#define RSP_INVALID_STORAGE_ID      0x2008
#define RSP_INVALID_OBJECT_HANDLE   0x2009
#define RSP_DEVICE_PROP_NOT_SUPPORT 0x200A
#define RSP_STORE_FULL              0x200C
#define RSP_NO_THUMBNAIL            0x2010
#define RSP_INCOMPLETE_TRANSFER     0x2007
#define RSP_INVALID_PARAMETER       0x201D

 /* MTP 属性码（MTP 1.1 规范 / 微软，注意不是旧的 PTP 编码） */
#define PROP_STORAGE_ID             0xDC01
#define PROP_OBJECT_FORMAT          0xDC02
#define PROP_PROTECTION_STATUS      0xDC03
#define PROP_OBJECT_SIZE            0xDC04
#define PROP_OBJECT_FILE_NAME       0xDC07
#define PROP_DATE_CREATED           0xDC08
#define PROP_DATE_MODIFIED          0xDC09
#define PROP_PARENT_OBJECT          0xDC0B
#define PROP_PERSISTANT_UID         0xDC41
#define PROP_NAME                   0xDC44

 /* MTP 数据类型 */
#define DTYPE_U8                    0x0002
#define DTYPE_U16                   0x0004
#define DTYPE_U32                   0x0006
#define DTYPE_U64                   0x0008
#define DTYPE_U128                  0x000A
#define DTYPE_STR                   0xFFFF

 /* ------------------------- CMSIS 驱动的 HAL 适配 ------------------------- */
void HAL_Delay(uint32_t ms)
{
	tim_delay_ms(ms);
}

 /* 引脚在 usb_mtp_init() 中配置；以下钩子是定义 RTE_DEVICE_FRAMEWORK_CLASSIC 时驱动所必需的。 */
void OTG_FS_PinsConfigure(uint32_t pins)
{
	(void)pins;
}

void OTG_FS_PinsUnconfigure(uint32_t pins)
{
	(void)pins;
}

/* USB OTG FS 全局中断：把内核中断状态转发给 CMSIS-Driver USBD_FS_STM32F4xx.c（Driver_USBD0）。 */
void OTG_FS_IRQHandler(void)
{
	usb_irq_count++;
	uint32_t gintsts = OTG_FS->GINTSTS;
	usb_last_gint = gintsts;
	if (usb_gint_hist_cnt < USB_GINT_HIST_SIZE)
		usb_gint_hist[usb_gint_hist_cnt] = gintsts;
	usb_gint_hist_cnt++;
/* 总线复位时立即挂起 EP0 以接收第一个 SETUP，无需等待 MTP 任务（避免主机超时竞态）。 */
	if ((gintsts & OTG_FS_GINTSTS_USBRST) != 0U)
	{
		OTG_FS->DOEPTSIZ0 = (1U << OTG_FS_DOEPTSIZx_PKTCNT_POS) |
		                    (3U << OTG_FS_DOEPTSIZ0_STUPCNT_POS);
		OTG_FS->DOEPCTL0 |= OTG_FS_DOEPCTLx_USBAEP |
		                    OTG_FS_DOEPCTLx_EPENA |
		                    OTG_FS_DOEPCTLx_CNAK;
	}
 	/* 当 EP0 IN 传输完成时，在中断里立即挂起 EP0 OUT 以接收状态阶段 ZLP
 	 * （或下一个 SETUP），避免主机状态阶段因任务调度延迟而被 NAK。 */
	{
		uint32_t diep0 = OTG_FS->DIEPINT0;
		USBD_FS_IRQ(gintsts);
		if ((diep0 & OTG_FS_DIEPINTx_XFCR) != 0U)
		{
			OTG_FS->DOEPTSIZ0 = (1U << OTG_FS_DOEPTSIZx_PKTCNT_POS) |
			                    (3U << OTG_FS_DOEPTSIZ0_STUPCNT_POS);
			OTG_FS->DOEPCTL0 |= OTG_FS_DOEPCTLx_USBAEP |
			                    OTG_FS_DOEPCTLx_EPENA |
			                    OTG_FS_DOEPCTLx_CNAK;
		}
	}
}

 /* ------------------------- USB 描述符 ------------------------- */
static const uint8_t usb_dev_desc[] =
{
	18, 0x01,                    /* bLength, bDescriptorType */
	0x00, 0x02,                  /* bcdUSB 2.00 */
	0x00, 0x00, 0x00,            /* class/subclass/protocol */
	64,                          /* bMaxPacketSize0 */
	0x83, 0x04,                  /* idVendor  0x0483 */
	0x41, 0x57,                  /* idProduct 0x5741 (fresh Windows identity) */
	0x00, 0x01,                  /* bcdDevice 1.00 */
	1, 2, 3,                     /* iManufacturer, iProduct, iSerial */
	1                            /* bNumConfigurations */
};

static const uint8_t usb_cfg_desc[] =
{
	9, 0x02, 0x27, 0x00, 1, 1, 0, 0x80, 100,   /* config, 39B, bus powered 200mA */
	9, 0x04, 0, 0, 3, 0x06, 0x01, 0x01, 4,     /* MTP interface (PIMA 15740) */
	7, 0x05, EP_BULK_OUT, 0x02, 64, 0x00, 0x00, /* EP1 OUT bulk */
	7, 0x05, EP_BULK_IN,  0x02, 64, 0x00, 0x00, /* EP2 IN  bulk */
	7, 0x05, EP_INTR_IN,  0x03, 16, 0x00, 0x01  /* EP3 IN interrupt, mps=16, interval=1 */
};

static const char usb_str_manufacturer[] = "STM32";
static const char usb_str_product[]      = "Eightyzhang MTP";
 /* 固定序列号：与手机一致。每次编译都变化的序列号会让 Windows 建立全新设备实例
  * （并重新执行 WPD 驱动初始化，而该流程偶尔会在 GetDevicePropDesc 处卡住）。
  * 固定的身份让 Windows 在多次烧录后仍保留同一个干净设备节点。 */
static char usb_str_serial[33];
static const char usb_str_interface[]    = "MTP";

 /* ------------------------- 静态状态 ------------------------- */
typedef struct
{
	uint8_t  type;   /* 0 = device event, 1 = endpoint event */
	uint8_t  ep;
	uint32_t evt;
} usb_evt_t;

typedef struct
{
	uint32_t handle;
	char     name[MTP_PATH_MAX];
	uint32_t size;
	uint8_t  used;
} mtp_pending_t;

 /* Windows 会给新上传的对象分配自己的句柄（例如 0x20005188），并在 SendObject 之后立即查询；
  * 记住该映射，使后续查询能解析到刚写入的文件。 */
typedef struct
{
	uint32_t handle;
	char     name[MTP_PATH_MAX];
	uint32_t size;
	uint8_t  is_dir;
	uint8_t  used;
} mtp_recent_t;

static QueueHandle_t usb_evt_queue;
static volatile uint8_t usb_configured;
static volatile uint32_t mtp_busy;       /* an MTP operation is in progress */
static uint32_t mtp_trans_id;

static uint8_t mtp_rx_buf[64];        /* command container */
static uint8_t mtp_stream[MTP_STREAM_SIZE];
static uint8_t mtp_stream2[MTP_STREAM_SIZE];
static uint8_t mtp_prop[MTP_PROP_SIZE];
static mtp_pending_t mtp_pending[MTP_PENDING_MAX];
static mtp_recent_t mtp_recent[MTP_RECENT_MAX];

/* 最近一次目录枚举的句柄缓存：Windows 打开盘符时先 GetObjectHandles 再逐对象
 * 查询，逐查询全树扫描会 O(N^2) 地读 SPI flash，卡。枚举时填表、查询时命中。 */
typedef struct
{
	uint32_t handle;
	uint32_t path_off;
	uint32_t size;
	uint16_t is_dir;
} mtp_cache_entry_t;
/* After rename Windows still queries the old handle: map old handle -> new path. */
typedef struct
{
	uint32_t handle;
	char     name[MTP_PATH_MAX];
	uint32_t size;
	uint8_t  is_dir;
	uint8_t  used;
} mtp_rename_t;
#define MTP_RENAME_MAX 16
static mtp_rename_t mtp_rename[MTP_RENAME_MAX];

static void mtp_rename_clear(void);
static void mtp_rename_add(uint32_t handle, const char *path, uint32_t size,
                           bool is_dir);
static int mtp_rename_find(uint32_t handle, char *path, uint32_t path_cap,
                           uint32_t *size, bool *is_dir);

static mtp_cache_entry_t mtp_cache[MTP_CACHE_MAX];
static char mtp_cache_pool[MTP_CACHE_POOL];
static uint32_t mtp_cache_used;
static uint32_t mtp_cache_pool_used;

static void mtp_cache_clear(void);
static void mtp_cache_add(uint32_t handle, const char *path, uint32_t size,
                          bool is_dir);
static int mtp_cache_find(uint32_t handle, char *path, uint32_t path_cap,
                          uint32_t *size, bool *is_dir);

static uint16_t mtp_current_op;

 /* ------------------------- 小端序辅助函数 ------------------------- */
static uint16_t mtp_get16(const uint8_t *p, uint32_t o)
{
	return (uint16_t)((uint16_t)p[o] | ((uint16_t)p[o + 1] << 8));
}

static uint32_t mtp_get32(const uint8_t *p, uint32_t o)
{
	return (uint32_t)p[o] | ((uint32_t)p[o + 1] << 8) |
	       ((uint32_t)p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24);
}

static void mtp_put16(uint8_t *p, uint32_t o, uint16_t v)
{
	p[o] = (uint8_t)v;
	p[o + 1] = (uint8_t)(v >> 8);
}

static void mtp_put32(uint8_t *p, uint32_t o, uint32_t v)
{
	p[o] = (uint8_t)v;
	p[o + 1] = (uint8_t)(v >> 8);
	p[o + 2] = (uint8_t)(v >> 16);
	p[o + 3] = (uint8_t)(v >> 24);
}

static void mtp_put64(uint8_t *p, uint32_t o, uint64_t v)
{
	mtp_put32(p, o, (uint32_t)v);
	mtp_put32(p, o + 4, (uint32_t)(v >> 32));
}

 /* MTP 字符串：1 字节长度 = UTF-16 字符数（含结尾空字符）（MTP 规范 3.2.3 / PIMA 15740；
  * 微软 WPD：“首字节是后续 Unicode 字符数（含 NULL 结束符）”）。注意不是字节数。 */
static uint32_t mtp_put_str(uint8_t *p, const char *s)
{
	const uint8_t *q = (const uint8_t *)s;
	uint32_t o = 1;               /* length byte placeholder */
	uint32_t n = 0;               /* UTF-16 code units written */
	if (*q == 0)
	{
 		/* 空 MTP 字符串：长度字节为 0，无结束符（Android/KurtE 做法） */
		p[0] = 0;
		return 1;
	}
	while (*q != 0)
	{
		uint32_t cp;
		if (q[0] < 0x80)
		{
			cp = q[0]; q += 1;
		}
		else if ((q[0] & 0xE0) == 0xC0 && q[1] != 0)
		{
			cp = ((uint32_t)(q[0] & 0x1F) << 6) | (q[1] & 0x3F); q += 2;
		}
		else if ((q[0] & 0xF0) == 0xE0 && q[1] != 0 && q[2] != 0)
		{
			cp = ((uint32_t)(q[0] & 0x0F) << 12) |
			     ((uint32_t)(q[1] & 0x3F) << 6) | (q[2] & 0x3F); q += 3;
		}
		else if ((q[0] & 0xF8) == 0xF0 && q[1] != 0 && q[2] != 0 && q[3] != 0)
		{
			cp = ((uint32_t)(q[0] & 0x07) << 18) |
			     ((uint32_t)(q[1] & 0x3F) << 12) |
			     ((uint32_t)(q[2] & 0x3F) << 6) | (q[3] & 0x3F); q += 4;
		}
		else
		{
			cp = '?'; q += 1;
		}
		if (cp < 0x10000)
		{
			p[o++] = (uint8_t)cp;
			p[o++] = (uint8_t)(cp >> 8);
			n++;
		}
		else
		{
			uint32_t v = cp - 0x10000;
			uint16_t hi = (uint16_t)(0xD800 + (v >> 10));
			uint16_t lo = (uint16_t)(0xDC00 + (v & 0x3FF));
			p[o++] = (uint8_t)hi; p[o++] = (uint8_t)(hi >> 8); n++;
			p[o++] = (uint8_t)lo; p[o++] = (uint8_t)(lo >> 8); n++;
		}
	}
	p[0] = (uint8_t)(n + 1);      /* chars incl null terminator */
	p[o++] = 0;
	p[o++] = 0;
	return o;
}

 /* 把 MTP 字符串读入 ASCII 缓冲区 */
static int mtp_read_str(const uint8_t *buf, uint32_t off, uint32_t cap,
						char *out, uint32_t out_size)
{
	uint32_t lenb, n, i, j = 0;
	if (off >= cap)
		return -1;
	lenb = buf[off];
	if (lenb == 0)
		return -1;
	n = lenb - 1;                       /* chars excluding the null terminator */
	if (off + 1 + 2 * (lenb - 1) + 2 > cap)
		return -1;
	for (i = 0; i < n; i++)
	{
		uint16_t ch = (uint16_t)((uint16_t)buf[off + 1 + 2 * i] |
		                        ((uint16_t)buf[off + 2 + 2 * i] << 8));
		if (ch < 0x80)
		{
			if (j + 1 >= out_size)
				break;
			out[j++] = (char)ch;
		}
		else if (ch < 0x800)
		{
			if (j + 2 >= out_size)
				break;
			out[j++] = (char)(0xC0 | (ch >> 6));
			out[j++] = (char)(0x80 | (ch & 0x3F));
		}
		else
		{
			if (j + 3 >= out_size)
				break;
			out[j++] = (char)(0xE0 | (ch >> 12));
			out[j++] = (char)(0x80 | ((ch >> 6) & 0x3F));
			out[j++] = (char)(0x80 | (ch & 0x3F));
		}
	}
	out[j] = 0;
	return 0;
}

 /* ------------------------- LittleFS 辅助函数 ------------------------- */
static lfs_t *mtp_lfs(void)
{
	return storage_lfs_get();
}

static void mtp_path_join(const char *dir, const char *name, char *out, uint32_t cap)
{
	if (dir[0] == 0 || strcmp(dir, "/") == 0)
		snprintf(out, cap, "/%s", name);
	else
		snprintf(out, cap, "%s/%s", dir, name);
}

static const char *mtp_basename(const char *path)
{
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

static bool mtp_is_dotdot(const char *name)
{
	return name[0] == '.' &&
	       (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

static void mtp_parent_path(const char *path, char *out, uint32_t cap)
{
	const char *s = strrchr(path, '/');
	if (s == NULL || s == path)
	{
		strncpy(out, "/", cap);
		out[cap - 1] = 0;
		return;
	}
	{
		uint32_t n = (uint32_t)(s - path);
		if (n >= cap)
			n = cap - 1;
		memcpy(out, path, n);
		out[n] = 0;
	}
}

static uint32_t mtp_obj_handle(const char *path);

static uint32_t mtp_parent_handle(const char *path)
{
	char parent[MTP_PATH_MAX];
	mtp_parent_path(path, parent, sizeof(parent));
	if (strcmp(parent, "/") == 0)
		return 0;
	return mtp_obj_handle(parent);
}

 /* 稳定的对象句柄 = 完整路径的 FNV-1a 哈希（根目录为 0）。 */
static uint32_t mtp_obj_handle(const char *name)
{
	uint32_t h = 2166136261U;
	const uint8_t *p = (const uint8_t *)name;
	while (*p)
	{
		h ^= *p++;
		h *= 16777619U;
	}
	if (h == 0 || h == 0xFFFFFFFFUL)
		h = 1;
	return h;
}

 /* 递归扫描全树，按"完整路径哈希"解析句柄（深度上限 5 层）。
  * 调用方必须已持有 storage 锁（锁不可重入）。 */
static char mtp_scan_path[MTP_PATH_MAX];

static int mtp_scan_find_locked(const char *dir, uint32_t handle, uint32_t *size,
                                bool *is_dir, int depth)
{
	lfs_dir_t d;
	struct lfs_info info;
	int err;

	if (depth > 5)
		return -1;
	err = lfs_dir_open(mtp_lfs(), &d, dir);
	if (err < 0)
		return -1;
	while (lfs_dir_read(mtp_lfs(), &d, &info) > 0)
	{
		char child[MTP_PATH_MAX];
		if (info.name[0] == '~' || mtp_is_dotdot(info.name))
			continue;
		mtp_path_join(dir, info.name, child, sizeof(child));
		/* 顺带暖缓存：一次漏扫后，其余对象的查询不再走全树扫描 */
		mtp_cache_add(mtp_obj_handle(child), child, (uint32_t)info.size,
		              info.type == LFS_TYPE_DIR);
		if (mtp_obj_handle(child) == handle)
		{
			*size = (uint32_t)info.size;
			*is_dir = (info.type == LFS_TYPE_DIR);
			strncpy(mtp_scan_path, child, MTP_PATH_MAX - 1);
			mtp_scan_path[MTP_PATH_MAX - 1] = 0;
			lfs_dir_close(mtp_lfs(), &d);
			return 0;
		}
		if (info.type == LFS_TYPE_DIR &&
		    mtp_scan_find_locked(child, handle, size, is_dir, depth + 1) == 0)
		{
			lfs_dir_close(mtp_lfs(), &d);
			return 0;
		}
	}
	lfs_dir_close(mtp_lfs(), &d);
	return -1;
}

 /* 把 MTP 对象句柄解析为完整路径/大小/类型。
  * 先查 recent（Windows 上传后自分配的句柄）与 pending（上传中）。 */
static int mtp_obj_find(uint32_t handle, char *path, uint32_t path_cap,
                        uint32_t *size, bool *is_dir)
{
	uint32_t i;
	if (handle == 0 || handle == 0xFFFFFFFFUL)
		return -1;
	for (i = 0; i < MTP_RECENT_MAX; i++)
	{
		if (mtp_recent[i].used && mtp_recent[i].handle == handle)
		{
			strncpy(path, mtp_recent[i].name, path_cap - 1);
			path[path_cap - 1] = 0;
			*size = mtp_recent[i].size;
			*is_dir = mtp_recent[i].is_dir != 0;
			return 0;
		}
	}
	for (i = 0; i < MTP_PENDING_MAX; i++)
	{
		if (mtp_pending[i].used && mtp_pending[i].handle == handle)
		{
			strncpy(path, mtp_pending[i].name, path_cap - 1);
			path[path_cap - 1] = 0;
			*size = mtp_pending[i].size;
			*is_dir = false;
			return 0;
		}
	}
	if (mtp_cache_find(handle, path, path_cap, size, is_dir) == 0)
		return 0;
	if (mtp_rename_find(handle, path, path_cap, size, is_dir) == 0)
		return 0;
	storage_lfs_lock();
	{
		int r = mtp_scan_find_locked("/", handle, size, is_dir, 0);
		storage_lfs_unlock();
		if (r == 0)
		{
			strncpy(path, mtp_scan_path, path_cap - 1);
			path[path_cap - 1] = 0;
			return 0;
		}
	}
	return -1;
}

 /* 枚举 dir 下对象句柄写入 buf（自带计数，偏移 0 为数量），按 MTP format 过滤：
  * 0 = 全部，0x3001 = 仅文件夹，其它 = 仅文件。 */
static uint32_t mtp_dir_handles(const char *dir, uint32_t format_filter,
                                uint8_t *buf, uint32_t cap, uint32_t *count_out)
{
	lfs_dir_t d;
	struct lfs_info info;
	uint32_t o = 4, count = 0;

	mtp_cache_clear();
	storage_lfs_lock();
	if (lfs_dir_open(mtp_lfs(), &d, dir) < 0)
	{
		storage_lfs_unlock();
		*count_out = 0;
		return o;
	}
	while (lfs_dir_read(mtp_lfs(), &d, &info) > 0)
	{
		char path[MTP_PATH_MAX];
		bool is_dir;
		if (info.name[0] == '~' || mtp_is_dotdot(info.name))
			continue;
		is_dir = (info.type == LFS_TYPE_DIR);
		if (format_filter == FORMAT_ASSOC && !is_dir)
			continue;
		if (format_filter != 0 && format_filter != FORMAT_ASSOC && is_dir)
			continue;
		mtp_path_join(dir, info.name, path, sizeof(path));
		if (o + 4 <= cap)
		{
			mtp_cache_add(mtp_obj_handle(path), path, (uint32_t)info.size, is_dir);
			mtp_put32(buf, o, mtp_obj_handle(path));
			o += 4;
			count++;
		}
	}
	lfs_dir_close(mtp_lfs(), &d);
	storage_lfs_unlock();
	*count_out = count;
	return o;
}

static uint32_t mtp_dir_count(const char *dir)
{
	lfs_dir_t d;
	struct lfs_info info;
	uint32_t count = 0;

	storage_lfs_lock();
	if (lfs_dir_open(mtp_lfs(), &d, dir) >= 0)
	{
	while (lfs_dir_read(mtp_lfs(), &d, &info) > 0)
		if (info.name[0] != '~' && !mtp_is_dotdot(info.name))
			count++;
		lfs_dir_close(mtp_lfs(), &d);
	}
	storage_lfs_unlock();
	return count;
}

 /* 递归删除目录内容后删除目录本身；调用方必须已持有 storage 锁。 */
static int mtp_remove_recursive_locked(const char *path)
{
	lfs_dir_t d;
	struct lfs_info info;
	int err;

	err = lfs_dir_open(mtp_lfs(), &d, path);
	if (err < 0)
		return err;
	while (lfs_dir_read(mtp_lfs(), &d, &info) > 0)
	{
		char child[MTP_PATH_MAX];
		if (info.name[0] == '~' || mtp_is_dotdot(info.name))
			continue;
		mtp_path_join(path, info.name, child, sizeof(child));
		if (info.type == LFS_TYPE_DIR)
			mtp_remove_recursive_locked(child);
		else
			lfs_remove(mtp_lfs(), child);
	}
	lfs_dir_close(mtp_lfs(), &d);
	return lfs_remove(mtp_lfs(), path);
}

 /* 复制单个文件；调用方必须已持有 storage 锁。 */
static int mtp_copy_file_locked(const char *src, const char *dst)
{
	lfs_file_t in, out;
	uint8_t buf[MTP_STREAM_SIZE];
	int n = 0;

	if (lfs_file_open(mtp_lfs(), &in, src, LFS_O_RDONLY) < 0)
		return -1;
	if (lfs_file_open(mtp_lfs(), &out, dst,
	                  LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0)
	{
		lfs_file_close(mtp_lfs(), &in);
		return -1;
	}
	while ((n = lfs_file_read(mtp_lfs(), &in, buf, sizeof(buf))) > 0)
	{
		if (lfs_file_write(mtp_lfs(), &out, buf, (lfs_size_t)n) != n)
		{
			n = -1;
			break;
		}
	}
	lfs_file_close(mtp_lfs(), &in);
	lfs_file_close(mtp_lfs(), &out);
	return n < 0 ? -1 : 0;
}

 /* 递归复制目录（目标目录必须已创建）；调用方必须已持有 storage 锁。 */
static int mtp_copy_dir_locked(const char *src, const char *dst)
{
	lfs_dir_t d;
	struct lfs_info info;
	int err;

	err = lfs_dir_open(mtp_lfs(), &d, src);
	if (err < 0)
		return err;
	while (lfs_dir_read(mtp_lfs(), &d, &info) > 0)
	{
		char s[MTP_PATH_MAX], t[MTP_PATH_MAX];
		if (info.name[0] == '~' || mtp_is_dotdot(info.name))
			continue;
		mtp_path_join(src, info.name, s, sizeof(s));
		mtp_path_join(dst, info.name, t, sizeof(t));
		if (info.type == LFS_TYPE_DIR)
		{
			if (lfs_mkdir(mtp_lfs(), t) < 0 ||
			    mtp_copy_dir_locked(s, t) < 0)
			{
				lfs_dir_close(mtp_lfs(), &d);
				return -1;
			}
		}
		else if (mtp_copy_file_locked(s, t) < 0)
		{
			lfs_dir_close(mtp_lfs(), &d);
			return -1;
		}
	}
	lfs_dir_close(mtp_lfs(), &d);
	return 0;
}

static int mtp_pending_alloc(const char *path, uint32_t size, uint32_t *handle)
{
	uint32_t i;
	for (i = 0; i < MTP_PENDING_MAX; i++)
	{
		if (!mtp_pending[i].used)
		{
			mtp_pending[i].used = 1;
			mtp_pending[i].handle = mtp_obj_handle(path);
			mtp_pending[i].size = size;
			strncpy(mtp_pending[i].name, path, MTP_PATH_MAX - 1);
			mtp_pending[i].name[MTP_PATH_MAX - 1] = 0;
 			*handle = mtp_pending[i].handle;
			return 0;
		}
	}
	return -1;
}

static void mtp_pending_free(uint32_t handle)
{
	uint32_t i;
	for (i = 0; i < MTP_PENDING_MAX; i++)
	{
		if (mtp_pending[i].used && mtp_pending[i].handle == handle)
		{
			mtp_pending[i].used = 0;
			return;
		}
	}
}

static void mtp_pending_reset(void)
{
	uint32_t i;
	for (i = 0; i < MTP_PENDING_MAX; i++)
		mtp_pending[i].used = 0;
	for (i = 0; i < MTP_RECENT_MAX; i++)
		mtp_recent[i].used = 0;
}

 /* 记住 Windows 使用的对象句柄（例如 0x20005188）对应的对象，
  * 使上传后的 GetObjectPropValue/GetObjectInfo 查询能够解析。 */
static void mtp_recent_add(uint32_t handle, const char *path, uint32_t size,
                           bool is_dir)
{
	uint32_t i, oldest = 0;
	for (i = 0; i < MTP_RECENT_MAX; i++)
	{
		if (!mtp_recent[i].used)
		{
			mtp_recent[i].used = 1;
			mtp_recent[i].handle = handle;
			mtp_recent[i].size = size;
			mtp_recent[i].is_dir = is_dir ? 1 : 0;
			strncpy(mtp_recent[i].name, path, MTP_PATH_MAX - 1);
			mtp_recent[i].name[MTP_PATH_MAX - 1] = 0;
			return;
		}
		if (mtp_recent[i].handle == handle)
		{
			mtp_recent[i].size = size;
			mtp_recent[i].is_dir = is_dir ? 1 : 0;
			strncpy(mtp_recent[i].name, path, MTP_PATH_MAX - 1);
			mtp_recent[i].name[MTP_PATH_MAX - 1] = 0;
			return;
		}
	}
 	/* 环形缓冲：覆盖最旧的条目 */
	for (i = 1; i < MTP_RECENT_MAX; i++)
		if (mtp_recent[i].used && mtp_recent[i].handle < mtp_recent[oldest].handle)
			oldest = i;
	mtp_recent[oldest].handle = handle;
	mtp_recent[oldest].size = size;
	mtp_recent[oldest].is_dir = is_dir ? 1 : 0;
	strncpy(mtp_recent[oldest].name, path, MTP_PATH_MAX - 1);
	mtp_recent[oldest].name[MTP_PATH_MAX - 1] = 0;
}

static void mtp_rename_clear(void)
{
	uint32_t i;
	for (i = 0; i < MTP_RENAME_MAX; i++)
		mtp_rename[i].used = 0;
}

static void mtp_rename_add(uint32_t handle, const char *path, uint32_t size,
                           bool is_dir)
{
	uint32_t i;
	for (i = 0; i < MTP_RENAME_MAX; i++)
	{
		if (!mtp_rename[i].used)
		{
			mtp_rename[i].used = 1;
			mtp_rename[i].handle = handle;
			mtp_rename[i].size = size;
			mtp_rename[i].is_dir = is_dir ? 1 : 0;
			strncpy(mtp_rename[i].name, path, MTP_PATH_MAX - 1);
			mtp_rename[i].name[MTP_PATH_MAX - 1] = 0;
			return;
		}
	}
}

static int mtp_rename_find(uint32_t handle, char *path, uint32_t path_cap,
                           uint32_t *size, bool *is_dir)
{
	uint32_t i;
	for (i = 0; i < MTP_RENAME_MAX; i++)
	{
		if (mtp_rename[i].used && mtp_rename[i].handle == handle)
		{
			strncpy(path, mtp_rename[i].name, path_cap - 1);
			path[path_cap - 1] = 0;
			*size = mtp_rename[i].size;
			*is_dir = mtp_rename[i].is_dir != 0;
			mtp_rename[i].used = 0;   /* consume: free slot for next rename */
			return 0;
		}
	}
	return -1;
}

static void mtp_cache_clear(void)
{
	mtp_cache_used = 0;
	mtp_cache_pool_used = 0;
}

static void mtp_cache_add(uint32_t handle, const char *path, uint32_t size,
                          bool is_dir)
{
	uint32_t i, len;
	for (i = 0; i < mtp_cache_used; i++)
	{
		if (mtp_cache[i].handle == handle)
		{
			mtp_cache[i].size = size;
			mtp_cache[i].is_dir = is_dir ? 1 : 0;
			return;
		}
	}
	if (mtp_cache_used >= MTP_CACHE_MAX)
		return;
	len = (uint32_t)strlen(path) + 1;
	if (mtp_cache_pool_used + len > MTP_CACHE_POOL)
		return;
	mtp_cache[mtp_cache_used].handle = handle;
	mtp_cache[mtp_cache_used].path_off = mtp_cache_pool_used;
	mtp_cache[mtp_cache_used].size = size;
	mtp_cache[mtp_cache_used].is_dir = is_dir ? 1 : 0;
	memcpy(mtp_cache_pool + mtp_cache_pool_used, path, len);
	mtp_cache_pool_used += len;
	mtp_cache_used++;
}

static int mtp_cache_find(uint32_t handle, char *path, uint32_t path_cap,
                          uint32_t *size, bool *is_dir)
{
	uint32_t i;
	for (i = 0; i < mtp_cache_used; i++)
	{
		if (mtp_cache[i].handle == handle)
		{
			strncpy(path, mtp_cache_pool + mtp_cache[i].path_off, path_cap - 1);
			path[path_cap - 1] = 0;
			*size = mtp_cache[i].size;
			*is_dir = mtp_cache[i].is_dir != 0;
			return 0;
		}
	}
	return -1;
}

static void mtp_date_str(char *out)
{
	rtc_date_time_t t;
	memset(&t, 0, sizeof(t));
	rtc_get_time(&t);
	if (t.year >= 2000)
		sprintf(out, "%04u%02u%02uT%02u%02u%02u",
		        (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
		        (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
	else
		strcpy(out, "20260807T120000");
}

 /* ------------------------- 事件队列 / 等待 ------------------------- */
static void usb_evt_post_from_isr(uint8_t type, uint8_t ep, uint32_t evt)
{
	BaseType_t hp = pdFALSE;
	usb_evt_t e;
	if (usb_evt_queue == NULL)
		return;
	e.type = type;
	e.ep = ep;
	e.evt = evt;
	xQueueSendFromISR(usb_evt_queue, &e, &hp);
	portYIELD_FROM_ISR(hp);
}

 /* 固定 32 位十六进制 MTP 序列号。E88E...01 曾因 5741 节点首次 SETUP 卡死（v8，优先级修复前）而被污染；
  * 使用新值让 Windows 建立全新设备节点并干净地完成初始化。 */
static void usb_serial_build(void)
{
	strcpy(usb_str_serial, "E8900000000000000000000000000001");
}

static void usb_dev_evt_cb(uint32_t evt)
{
	usb_evt_post_from_isr(0, 0, evt);
}

static void usb_ep_evt_cb(uint8_t ep, uint32_t evt)
{
	usb_evt_post_from_isr(1, ep, evt);
}

static void ctrl_handle_setup(void);

/* 等待指定端点事件。新的 EP0 SETUP 会被内联处理（它会中止当前传输）。成功返回 0，超时返回 -1。 */
static int usb_wait_ep(uint8_t ep, uint32_t mask, uint32_t timeout_ms)
{
	uint32_t start = (uint32_t)xTaskGetTickCount();
	for (;;)
	{
		usb_evt_t e;
		if (xQueueReceive(usb_evt_queue, &e, pdMS_TO_TICKS(50)) != pdTRUE)
		{
			if (timeout_ms != 0 &&
			    ((uint32_t)(xTaskGetTickCount() - start)) >= pdMS_TO_TICKS(timeout_ms))
				return -1;
			continue;
		}
		if (e.type == 1U && e.ep == ep && (e.evt & mask) != 0U)
			return 0;
 		/* 新的 SETUP 中止当前传输；立即处理它 */
		if (e.type == 1U && e.ep == 0x00U && (e.evt & ARM_USBD_EVENT_SETUP) != 0U)
			ctrl_handle_setup();
	}
}

 /* ------------------------- 控制端点 ------------------------- */
static int ctrl_send_in(const uint8_t *data, uint32_t len)
{
	int r;
	if (Driver_USBD0.EndpointTransfer(0x80, (uint8_t *)data, len) != ARM_DRIVER_OK)
	{
		printf("[USB] ep0in busy\n");
		return -1;
	}
	r = usb_wait_ep(0x80, ARM_USBD_EVENT_IN, 2000);
	return r;
}

 /* 响应控制读请求。IN 数据完成的那一刻，中断里已挂起 EP0 OUT 用于状态阶段 ZLP；
  * 这里只需等待它完成。 */
static void ctrl_read_respond(const uint8_t *data, uint32_t len, uint16_t wLen)
{
	uint32_t n = (wLen < len) ? wLen : len;
	ctrl_send_in(data, n);
	usb_wait_ep(0x00, ARM_USBD_EVENT_OUT, 2000);
}

static int ctrl_status_out(void)
{
	if (Driver_USBD0.EndpointTransfer(0x00, NULL, 0) != ARM_DRIVER_OK)
		return -1;
	return usb_wait_ep(0x00, ARM_USBD_EVENT_OUT, 2000);
}

 /* 重新挂起 EP0 OUT（STUPCNT=3）以接收下一个 SETUP 包。
  * 总线复位后以及每次控制传输后都需要执行。 */
static void ctrl_ep0_arm_setup(void)
{
	Driver_USBD0.EndpointTransfer(0x00, NULL, 0);
}

static int ctrl_status_in(void)
{
	if (Driver_USBD0.EndpointTransfer(0x80, NULL, 0) != ARM_DRIVER_OK)
		return -1;
	if (usb_wait_ep(0x80, ARM_USBD_EVENT_IN, 2000) != 0)
		return -1;
	ctrl_ep0_arm_setup();
	return 0;
}

static void ctrl_stall(void)
{
	Driver_USBD0.EndpointStall(0x00, true);
	Driver_USBD0.EndpointStall(0x80, true);
	ctrl_ep0_arm_setup();
}

static uint32_t mtp_string_desc_build(uint8_t *buf, const char *s)
{
	uint32_t n = (uint32_t)strlen(s);
	uint32_t i;
	buf[0] = (uint8_t)(2 + 2 * n);
	buf[1] = 0x03;
	for (i = 0; i < n; i++)
	{
		buf[2 + 2 * i] = (uint8_t)s[i];
		buf[3 + 2 * i] = 0;
	}
	return 2 + 2 * n;
}

static const uint8_t *mtp_desc_get(uint8_t type, uint8_t index, uint32_t *len)
{
	static uint8_t strbuf[64];
	switch (type)
	{
	case 0x01:
 		*len = sizeof(usb_dev_desc);
		return usb_dev_desc;
	case 0x02:
 		*len = sizeof(usb_cfg_desc);
		return usb_cfg_desc;
	case 0x03:
		if (index == 0)
		{
			strbuf[0] = 4;
			strbuf[1] = 0x03;
			strbuf[2] = 0x09;
			strbuf[3] = 0x04;
 			*len = 4;
		}
		else if (index == 1)
 			*len = mtp_string_desc_build(strbuf, usb_str_manufacturer);
		else if (index == 2)
 			*len = mtp_string_desc_build(strbuf, usb_str_product);
		else if (index == 3)
 			*len = mtp_string_desc_build(strbuf, usb_str_serial);
		else if (index == 4)
 			*len = mtp_string_desc_build(strbuf, usb_str_interface);
		else
			return NULL;
		return strbuf;
	default:
		return NULL;
	}
}

static void usb_ep_configure(void)
{
	Driver_USBD0.EndpointConfigure(EP_BULK_OUT, ARM_USB_ENDPOINT_BULK, 64);
	Driver_USBD0.EndpointConfigure(EP_BULK_IN,  ARM_USB_ENDPOINT_BULK, 64);
	Driver_USBD0.EndpointConfigure(EP_INTR_IN,  ARM_USB_ENDPOINT_INTERRUPT, 16);
	usb_configured = 1;
	Driver_USBD0.EndpointTransfer(EP_BULK_OUT, mtp_rx_buf, 64);
	printf("[USB] configured (MTP)\n");
}

static void ctrl_handle_setup(void)
{
	uint8_t setup[8];
	uint8_t bm, req;
	uint16_t wValue, wIndex, wLen;

	if (Driver_USBD0.ReadSetupPacket(setup) != ARM_DRIVER_OK)
		return;

	Driver_USBD0.EndpointStall(0x00, false);
	Driver_USBD0.EndpointStall(0x80, false);

	bm = setup[0];
	req = setup[1];
	wValue = (uint16_t)(setup[2] | ((uint16_t)setup[3] << 8));
	wIndex = (uint16_t)(setup[4] | ((uint16_t)setup[5] << 8));
	wLen   = (uint16_t)(setup[6] | ((uint16_t)setup[7] << 8));
	if ((bm & 0x80U) != 0U)
	{
 		/* 设备到主机的控制传输 */
		switch (req)
		{
		case 0x06:  /* GET_DESCRIPTOR */
		{
			const uint8_t *desc;
			uint32_t dlen;
			desc = mtp_desc_get((uint8_t)(wValue >> 8), (uint8_t)wValue, &dlen);
			if (desc == NULL)
			{
				ctrl_stall();
				return;
			}
			if (wLen == 0)
			{
				ctrl_status_out();
				return;
			}
			ctrl_read_respond(desc, dlen, wLen);
			return;
		}
		case 0x00:  /* GET_STATUS */
		{
			uint8_t st[2] = { 0, 0 };
			ctrl_read_respond(st, 2, wLen);
			return;
		}
		case 0x08:  /* GET_CONFIGURATION */
		{
			uint8_t cfg = usb_configured ? 1 : 0;
			ctrl_read_respond(&cfg, 1, wLen);
			return;
		}
		case 0x0A:  /* GET_INTERFACE */
		{
			uint8_t itf = 0;
			ctrl_read_respond(&itf, 1, wLen);
			return;
		}
		case 0x04:  /* GET_DEVICE_STATUS (PIMA 15740:2000 legacy code) */
		case 0x67:  /* GET_DEVICE_STATUS (PIMA 15740 current) */
		{
			uint8_t st[4];
			uint16_t code = mtp_busy ? 0x2019 : 0x2001;
			if ((bm & 0x60U) != 0x20U)
			{
				ctrl_stall();
				return;
			}
			if (wIndex != 0)
			{
				ctrl_stall();
				return;
			}
 			/* PIMA 15740：状态是 UINT16 PIMA 响应码：
 			 * 0x2001 = 就绪/OK，0x2019 = 设备忙（操作进行中）。
 			 * 主机在传输期间轮询该状态，只要设备报告忙就持续等待。
 			 * 载荷是 4 字节结构：UINT16 长度（=4）+ UINT16 状态码（USBX PIMA 类，
 			 * Windows MTP 驱动会解析这两个字段；只发 2 字节会让 Windows 把状态码当作长度
 			 * 从而永远重试 GET_DEVICE_STATUS）。 */
			st[0] = 0x04; st[1] = 0x00;      /* structure length */
			st[2] = (uint8_t)code;
			st[3] = (uint8_t)(code >> 8);
			ctrl_read_respond(st, sizeof(st), wLen);
			return;
		}
		case 0x02:  /* GET_EXTENDED_EVENT_DATA (legacy) - no events queued */
		case 0x65:  /* GET_EXTENDED_EVENT_DATA (current) - no events queued */
			ctrl_stall();
			return;
		default:
			ctrl_stall();
			return;
		}
	}
	else
	{
 		/* 主机到设备的控制传输 */
		if ((bm & 0x60U) == 0x20U)
		{
 			/* MTP 类专属请求（PIMA 15740）：Cancel（0x01）和设备复位（0x03）。
 			 * 它们与标准 CLEAR_FEATURE/SET_FEATURE 数字相同，必须依据 bmRequestType 类型位区分。 */
			switch (req)
			{
			case 0x01:  /* CANCEL (PIMA 15740:2000 legacy code) */
			case 0x64:  /* CANCEL_REQUEST (PIMA 15740 current) */
				if (wLen > 0)
				{
/* 排空主机到设备的数据阶段（例如 Windows 随 CANCEL 发送 6 字节载荷） */
					uint8_t drain[64];
					uint32_t n = (wLen > sizeof(drain)) ? sizeof(drain) : wLen;
					Driver_USBD0.EndpointTransfer(0x00, drain, n);
					usb_wait_ep(0x00, ARM_USBD_EVENT_OUT, 2000);
				}
				mtp_busy = 0;
				ctrl_status_in();
				return;
			case 0x03:  /* DEVICE_RESET (PIMA 15740:2000 legacy code) */
			case 0x66:  /* DEVICE_RESET_REQUEST (PIMA 15740 current) */
/* 与 USBX 一致：复位会话和 pending 传输状态，让主机侧恢复流程可以开启新会话。 */
				mtp_busy = 0;
				mtp_pending_reset();
				mtp_cache_clear();
				ctrl_status_in();
				return;
			default:
				ctrl_stall();
				return;
			}
		}
		switch (req)
		{
		case 0x05:  /* SET_ADDRESS */
			Driver_USBD0.DeviceSetAddress(setup[2]);
			ctrl_status_in();
			return;
		case 0x09:  /* SET_CONFIGURATION */
			if (wValue == 1)
				usb_ep_configure();
			ctrl_status_in();
			return;
		case 0x01:  /* CLEAR_FEATURE */
		case 0x03:  /* SET_FEATURE */
			if (wValue == 0 && (wIndex == EP_BULK_OUT || wIndex == EP_BULK_IN))
				Driver_USBD0.EndpointStall((uint8_t)wIndex, (req == 0x03));
			ctrl_status_in();
			return;
		case 0x0B:  /* SET_INTERFACE */
			ctrl_status_in();
			return;
		default:
			if (wLen != 0)
			{
				ctrl_stall();
				return;
			}
			ctrl_status_in();
			return;
		}
	}
}

 /* ------------------------- MTP 发送辅助函数 ------------------------- */
static int mtp_tx_raw(const uint8_t *buf, uint32_t len)
{
 	/* 用一次 EndpointTransfer 调用发送整个数据集。CMSIS-Driver 会在每次包（XFRC）后
 	 * 从中断里用同一缓冲区重新填充 TX FIFO，和 Windows 已能正确接收的 66 字节 EP0 字符串描述符一样。
 	 * 把数据拆成多次 64 字节传输并夹杂任务调度/NAK 间隙的做法从未被主机接受，
 	 * 所以不要改回逐包发送。 */
	uint32_t t0 = (uint32_t)xTaskGetTickCount();
	int r;

	if (Driver_USBD0.EndpointTransfer(EP_BULK_IN, (uint8_t *)buf, len) != ARM_DRIVER_OK)
	{
		printf("[MTP] tx busy len=%lu\n", (unsigned long)len);
		return -1;
	}
	r = usb_wait_ep(EP_BULK_IN, ARM_USBD_EVENT_IN, 5000);
	return r;
}

static void mtp_send_response(uint16_t code, const uint32_t *params, uint8_t n)
{
	uint8_t buf[32];
	uint32_t len = 12 + (uint32_t)n * 4;
	uint32_t i;
	if (code != RSP_OK)
		printf("[MTP] rsp err 0x%04X\n", code);
	mtp_put32(buf, 0, len);
	mtp_put16(buf, 4, MTP_TYPE_RESPONSE);
	mtp_put16(buf, 6, code);
	mtp_put32(buf, 8, mtp_trans_id);
	for (i = 0; i < n; i++)
		mtp_put32(buf, 12 + 4 * i, params[i]);
	mtp_tx_raw(buf, len);
}

static void mtp_send_ok0(void)
{
	mtp_send_response(RSP_OK, NULL, 0);
}

 /* 发送数据阶段（头部 + 载荷），然后发送 OK 响应 */
static int mtp_tx_data(const uint8_t *payload, uint32_t len)
{
	uint8_t *p = mtp_prop;
	uint32_t total = 12 + len;
 	/* MTP 数据阶段必须是连续的一次 USB 传输：12 字节容器头 + 载荷。
 	 * 把头单独作为短包发送会让 Windows 误认为数据阶段已结束。
 	 * 头与载荷必须连续拼接后一次发出。 */
	if (len > 0)
		memmove(p + 12, payload, len);
	mtp_put32(p, 0, total);
	mtp_put16(p, 4, MTP_TYPE_DATA);
	mtp_put16(p, 6, mtp_current_op);
	mtp_put32(p, 8, mtp_trans_id);
	return mtp_tx_raw(p, total);
}

static int mtp_tx_data_then_ok(const uint8_t *payload, uint32_t len)
{
	if (mtp_tx_data(payload, len) != 0)
		return -1;
	mtp_send_ok0();
	return 0;
}

 /* ------------------------- MTP 接收辅助函数 ------------------------- */
 /* 挂起 bulk OUT 端点以接收下一条命令。做短暂重试：前一次 OUT 传输刚完成时端点可能仍报忙，
  * 单次挂起失败会静默丢掉下一条命令（Windows 随后超时并中止设备初始化）。 */
static void mtp_rx_arm(void)
{
	uint32_t i;
	for (i = 0; i < 20; i++)
	{
		if (Driver_USBD0.EndpointTransfer(EP_BULK_OUT, mtp_rx_buf, 64) == ARM_DRIVER_OK)
			return;
		vTaskDelay(pdMS_TO_TICKS(5));
	}
	printf("[MTP] rx arm failed\n");
}

 /* 把一条 MTP 容器（头 + 体）接收到 buf，返回总长度 */
static int mtp_rx_container(uint8_t *buf, uint32_t cap, uint32_t *out_len)
{
	uint32_t got, total;

	Driver_USBD0.EndpointTransfer(EP_BULK_OUT, buf, 64);
	if (usb_wait_ep(EP_BULK_OUT, ARM_USBD_EVENT_OUT, 10000) != 0)
		return -1;
	got = Driver_USBD0.EndpointTransferGetResult(EP_BULK_OUT);
	if (got < 12)
		return -1;
	total = mtp_get32(buf, 0);
	if (total < 12 || total > cap)
		return -1;
	if (total > got)
	{
		Driver_USBD0.EndpointTransfer(EP_BULK_OUT, buf + got, total - got);
		if (usb_wait_ep(EP_BULK_OUT, ARM_USBD_EVENT_OUT, 10000) != 0)
			return -1;
	}
 	*out_len = total;
	return 0;
}

/* 为传入的数据阶段挂起 EP_BULK_OUT。若端点仍在结束前一次传输则短暂重试。 */
static int mtp_rx_arm_data(uint8_t *buf, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < 20; i++)
	{
		if (Driver_USBD0.EndpointTransfer(EP_BULK_OUT, buf, len) == ARM_DRIVER_OK)
			return 0;
		vTaskDelay(pdMS_TO_TICKS(5));
	}
	printf("[MTP] rx arm failed len=%lu\n", (unsigned long)len);
	return -1;
}

 /* 接收 total 字节载荷并写入已打开的 LittleFS 文件。
  * 双缓冲：一个缓冲区写入 SPI flash 时，另一个保持挂在 bulk OUT 端点上，
  * 让主机始终看到端点处于活动状态（否则 Windows 会认为设备卡死，
  * 用 DEVICE_RESET/CANCEL 中止传输）。 */
static int mtp_rx_stream(lfs_file_t *f, uint32_t total)
{
	uint32_t remain = total;
	uint8_t *bufs[2] = { mtp_stream, mtp_stream2 };
	uint32_t lens[2] = { 0, 0 };
	int armed = 0;           /* index of the buffer currently armed */
	uint32_t chunk_cnt = 0;

	if (total == 0)
		return 0;

	lens[armed] = (remain > MTP_STREAM_SIZE) ? MTP_STREAM_SIZE : remain;
	if (mtp_rx_arm_data(bufs[armed], lens[armed]) != 0)
		return -1;

	while (remain > 0)
	{
		uint32_t got;
		int w;
		int cur = armed;
		int other = cur ^ 1;

		if (usb_wait_ep(EP_BULK_OUT, ARM_USBD_EVENT_OUT, 10000) != 0)
		{
			printf("[MTP] rx_stream: timeout remain=%lu t=%lu\n",
			       (unsigned long)remain, (unsigned long)xTaskGetTickCount());
			return -1;
		}
		got = Driver_USBD0.EndpointTransferGetResult(EP_BULK_OUT);
		if (got == 0 || got > lens[cur])
		{
			printf("[MTP] rx_stream: bad got=%lu chunk=%lu\n",
			       (unsigned long)got, (unsigned long)lens[cur]);
			return -1;
		}
		remain -= got;
		chunk_cnt++;

 		/* 在写 flash 之前先挂起另一块缓冲区，使端点在本缓冲区写入期间持续接收数据
 		 * 使端点在本缓冲区写入期间持续接收数据 */
		if (remain > 0)
		{
			lens[other] = (remain > MTP_STREAM_SIZE) ? MTP_STREAM_SIZE : remain;
			if (mtp_rx_arm_data(bufs[other], lens[other]) != 0)
				return -1;
			armed = other;
		}

		storage_lfs_lock();
		w = lfs_file_write(mtp_lfs(), f, bufs[cur], got);
		storage_lfs_unlock();
		if (w != (int)got)
		{
			printf("[MTP] rx_stream: write fail got=%lu\n",
			       (unsigned long)got);
			return -1;
		}
	}
	return 0;
}

 /* ------------------------- MTP 操作 ------------------------- */
static void op_get_device_info(void)
{
	static const uint16_t ops[] =
	{
		OP_GET_DEVICE_INFO, OP_OPEN_SESSION, OP_CLOSE_SESSION,
		OP_GET_STORAGE_IDS, OP_GET_STORAGE_INFO, OP_GET_NUM_OBJECTS,
		OP_GET_OBJECT_HANDLES, OP_GET_OBJECT_INFO, OP_GET_OBJECT,
		OP_GET_THUMB, OP_DELETE_OBJECT, OP_SEND_OBJECT_INFO,
		OP_SEND_OBJECT, OP_INITIATE_CAPTURE,
		OP_COPY_OBJECT, OP_MOVE_OBJECT,
		OP_GET_OBJECT_PROPS_SUPPORT, OP_GET_OBJECT_PROP_DESC,
		OP_GET_OBJECT_PROP_VALUE,
		OP_GET_OBJECT_PROP_LIST,
		OP_SET_OBJECT_PROP_LIST,
		OP_GET_OBJECT_REFS,
		OP_GET_DEVICE_PROP_DESC, OP_GET_DEVICE_PROP_VALUE
	};
	static const uint16_t props[] = { 0xD401, 0xD402 };
	uint8_t *p = mtp_prop;
	uint32_t o = 0, i;
	uint32_t ops_n = (uint32_t)(sizeof(ops) / sizeof(ops[0]));
	uint32_t props_n = (uint32_t)(sizeof(props) / sizeof(props[0]));

 	/* MTP DeviceInfo 数据集（PIMA 15740 / MTP 规范 5.1.1 节）。
 	 * Windows WPD/MTP 驱动在设备初始化时检查这些字段：
 	 *  - VendorExtensionID 必须是 0x00000006（MTP），不能是 0xFFFFFFFF
 	 *  - OperationsSupported 必须列出我们实现的操作
 	 *  - SerialNumber 必须是 32 字符十六进制字符串
 	 * 数据集为空或格式错误时，Windows 只在便携设备里显示一个通用
 	 * “MTP USB Device”，且永远不挂载盘符。 */
	mtp_put16(p, o, 0x0064); o += 2;              /* StandardVersion 1.0 */
	mtp_put32(p, o, 0x00000006UL); o += 4;        /* VendorExtensionID: MTP */
	mtp_put16(p, o, 0x0064); o += 2;              /* VendorExtensionVersion 1.0 */
	o += mtp_put_str(p + o, "microsoft.com: 1.0;");
	mtp_put16(p, o, 0x0001); o += 2;              /* FunctionalMode: 1 = standard (MTP spec / KurtE) */
	mtp_put32(p, o, ops_n); o += 4;               /* OperationsSupported */
	for (i = 0; i < ops_n; i++)
	{
		mtp_put16(p, o, ops[i]);
		o += 2;
	}
	mtp_put32(p, o, 0); o += 4;                   /* EventsSupported: none */
	mtp_put32(p, o, props_n); o += 4;             /* DevicePropertiesSupported */
	for (i = 0; i < props_n; i++)
	{
		mtp_put16(p, o, props[i]);
		o += 2;
	}
	mtp_put32(p, o, 0); o += 4;                   /* CaptureFormats: none */
	mtp_put32(p, o, 2); o += 4;                   /* PlaybackFormats: 0x3000 + 0x3001 (KurtE) */
	mtp_put16(p, o, 0x3000); o += 2;
	mtp_put16(p, o, 0x3001); o += 2;
	o += mtp_put_str(p + o, "STM32");
	o += mtp_put_str(p + o, "Eightyzhang MTP");
	o += mtp_put_str(p + o, "1.0");
	o += mtp_put_str(p + o, usb_str_serial);
	mtp_tx_data_then_ok(p, o);
}

static void op_open_session(const uint32_t *params)
{
	uint32_t rsp[1];
 	/* 清理上次中断传输残留的临时文件 */
	storage_lfs_lock();
	lfs_remove(mtp_lfs(), MTP_TMP_NAME);
	storage_lfs_unlock();
	mtp_pending_reset();
	mtp_rename_clear();
	mtp_cache_clear();
	rsp[0] = params[0];
	mtp_send_response(RSP_OK, rsp, 1);
}

static void op_close_session(void)
{
	mtp_pending_reset();
	mtp_send_ok0();
}

static void op_get_storage_ids(void)
{
	uint8_t *p = mtp_prop;
	uint32_t o = 0;
	mtp_put32(p, o, 1); o += 4;
	mtp_put32(p, o, MTP_STORAGE_ID); o += 4;
	mtp_tx_data_then_ok(p, o);
}

static void op_get_storage_info(void)
{
	const struct lfs_config *cfg = storage_lfs_config();
	uint8_t *p = mtp_prop;
	uint32_t o = 0;
	uint32_t used_blocks = 0;
	uint64_t free_bytes;
	int used;

	storage_lfs_lock();
	used = lfs_fs_size(mtp_lfs());
	storage_lfs_unlock();
	if (used > 0)
		used_blocks = (uint32_t)used;
	free_bytes = (uint64_t)(cfg->block_count - used_blocks) * cfg->block_size;

	mtp_put16(p, o, 0x0003); o += 2;              /* Removable RAM */
	mtp_put16(p, o, 0x0002); o += 2;              /* generic hierarchical FS */
	mtp_put16(p, o, 0x0000); o += 2;              /* read/write */
	mtp_put64(p, o, (uint64_t)cfg->block_count * cfg->block_size); o += 8;
	mtp_put64(p, o, free_bytes); o += 8;
 	/* 空闲对象数：上报真实估算值而不是 0xFFFFFFFF（“未知”），
 	 * 某些 Windows 版本会把“未知”当成存储已满。 */
	{
		uint64_t free_objs = free_bytes / cfg->block_size;
		mtp_put32(p, o, (free_objs > 0xFFFFFFFEULL) ? 0xFFFFFFFEUL : (uint32_t)free_objs);
		o += 4;
	}
	o += mtp_put_str(p + o, "W25Q128-2");
	o += mtp_put_str(p + o, "LFS");
	mtp_tx_data_then_ok(p, o);
}

static void op_get_num_objects(const uint32_t *params)
{
	uint32_t rsp[1];
	uint32_t count = 0;
	if (params[2] == 0 || params[2] == 0xFFFFFFFFUL)
		count = mtp_dir_count("/");
	else
	{
		char dir[MTP_PATH_MAX];
		uint32_t size;
		bool is_dir;
		if (mtp_obj_find(params[2], dir, sizeof(dir), &size, &is_dir) == 0 &&
		    is_dir)
			count = mtp_dir_count(dir);
	}
	rsp[0] = count;
	mtp_send_response(RSP_OK, rsp, 1);
}

static void op_get_object_handles(const uint32_t *params)
{
	uint8_t *p = mtp_prop;
	uint32_t o, count = 0;
	char dir[MTP_PATH_MAX];

	if (params[2] == 0 || params[2] == 0xFFFFFFFFUL)
		strcpy(dir, "/");
	else
	{
		uint32_t size;
		bool is_dir;
		if (mtp_obj_find(params[2], dir, sizeof(dir), &size, &is_dir) != 0 ||
		    !is_dir)
		{
			/* 未知/非目录 parent：返回空列表 */
			mtp_put32(p, 0, 0);
			mtp_tx_data(p, 4);
			mtp_send_ok0();
			return;
		}
	}
	/* ponytail: 0xFFFFFFFF 按根目录处理（不递归全树），Windows 浏览按文件夹查询足够 */
	/* 留 16B 给 mtp_tx_data 的 12B 容器头 + 余量，防止 memmove 越界 */
	o = mtp_dir_handles(dir, params[1], p, MTP_PROP_SIZE - 16, &count);
	mtp_put32(p, 0, count);
	mtp_tx_data(p, o);
 	/* MTP 规范：GetObjectHandles 响应不带参数；
 	 * 对象句柄数组（自带计数）就是数据阶段。 */
	mtp_send_ok0();
}

static void op_get_object_info(const uint32_t *params)
{
	char path[MTP_PATH_MAX];
	uint32_t size;
	char date[17];
	uint8_t *p = mtp_prop;
	uint32_t o = 0;
	bool is_dir;

	if (mtp_obj_find(params[0], path, sizeof(path), &size, &is_dir) != 0)
	{
		mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
		return;
	}
	mtp_date_str(date);
	mtp_put32(p, o, MTP_STORAGE_ID); o += 4;  /* storage ID (first field!) */
	mtp_put16(p, o, is_dir ? FORMAT_ASSOC : 0x3000); o += 2;  /* 文件用 0x3000，与属性查询一致 */
	mtp_put16(p, o, 0x0000); o += 2;              /* protection: none */
	mtp_put32(p, o, size); o += 4;
	mtp_put16(p, o, 0x0000); o += 2;              /* thumb format */
	mtp_put32(p, o, 0); o += 4;                   /* thumb size */
	mtp_put32(p, o, 0); o += 4;                   /* thumb width */
	mtp_put32(p, o, 0); o += 4;                   /* thumb height */
	mtp_put32(p, o, 0); o += 4;                   /* image width */
	mtp_put32(p, o, 0); o += 4;                   /* image height */
	mtp_put32(p, o, 0); o += 4;                   /* bit depth */
	mtp_put32(p, o, mtp_parent_handle(path)); o += 4;  /* parent object handle */
	mtp_put16(p, o, is_dir ? 0x0001 : 0x0000); o += 2;  /* association type: 1 = 文件夹 */
	mtp_put32(p, o, 0); o += 4;                   /* association desc */
	mtp_put32(p, o, 0); o += 4;                   /* sequence number */
	o += mtp_put_str(p + o, mtp_basename(path));
	o += mtp_put_str(p + o, date);
	o += mtp_put_str(p + o, date);
	o += mtp_put_str(p + o, "");
	mtp_tx_data_then_ok(p, o);
}

static void op_get_object(const uint32_t *params)
{
	char path[MTP_PATH_MAX];
	uint32_t size;
	lfs_file_t f;
	uint8_t hdr[12];
	uint32_t remain, chunk;
	int err;
	bool is_dir;

	if (mtp_obj_find(params[0], path, sizeof(path), &size, &is_dir) != 0 ||
	    is_dir)
	{
		mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
		return;
	}

	storage_lfs_lock();
	err = lfs_file_open(mtp_lfs(), &f, path, LFS_O_RDONLY);
	storage_lfs_unlock();
	if (err < 0)
	{
		mtp_send_response(RSP_GENERAL_ERROR, NULL, 0);
		return;
	}

	mtp_put32(hdr, 0, 12 + size);
	mtp_put16(hdr, 4, MTP_TYPE_DATA);
	mtp_put16(hdr, 6, OP_GET_OBJECT);
	mtp_put32(hdr, 8, mtp_trans_id);

	remain = size;
 	/* 把 12 字节容器头与第一块数据一起发送，使数据阶段成为一次连续 USB 传输
 	 * （避免中途短包让 Windows 误认为数据结束）。 */
	chunk = (remain > MTP_STREAM_SIZE - 12) ? MTP_STREAM_SIZE - 12 : remain;
	if (chunk > 0)
	{
		storage_lfs_lock();
		chunk = (uint32_t)lfs_file_read(mtp_lfs(), &f, mtp_stream + 12, chunk);
		storage_lfs_unlock();
		if ((int32_t)chunk <= 0)
			chunk = 0;
	}
	memcpy(mtp_stream, hdr, 12);
	if (mtp_tx_raw(mtp_stream, 12 + chunk) != 0)
	{
		storage_lfs_lock();
		lfs_file_close(mtp_lfs(), &f);
		storage_lfs_unlock();
		return;
	}
	remain -= chunk;
	while (remain > 0)
	{
		int r;
		chunk = (remain > MTP_STREAM_SIZE) ? MTP_STREAM_SIZE : remain;
		storage_lfs_lock();
		r = lfs_file_read(mtp_lfs(), &f, mtp_stream, chunk);
		storage_lfs_unlock();
		if (r <= 0)
			break;
		if (mtp_tx_raw(mtp_stream, (uint32_t)r) != 0)
			break;
		remain -= (uint32_t)r;
	}

	storage_lfs_lock();
	lfs_file_close(mtp_lfs(), &f);
	storage_lfs_unlock();

	if (remain != 0)
		return;
	mtp_send_ok0();
}

static void op_get_thumb(void)
{
	mtp_send_response(RSP_NO_THUMBNAIL, NULL, 0);
}

static void op_delete_object(const uint32_t *params)
{
	char path[MTP_PATH_MAX];
	uint32_t size;
	uint32_t i;
	bool is_dir;
	int err;

	if (mtp_obj_find(params[0], path, sizeof(path), &size, &is_dir) != 0)
	{
		mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
		return;
	}
 	/* 只有仍在进行中的 pending 槽（上传尚未结束）才通过释放 pending 条目来取消。
 	 * 已完成的上传是 LittleFS 上的真实文件，即使句柄位于 pending/基址范围内，
 	 * 也必须按普通文件删除。 */
	for (i = 0; i < MTP_PENDING_MAX; i++)
	{
		if (mtp_pending[i].used && mtp_pending[i].handle == params[0])
		{
			mtp_pending_free(params[0]);
			mtp_send_ok0();
			return;
		}
	}
 	/* 已完成上传：删除 recent 映射，然后删除文件 */
	for (i = 0; i < MTP_RECENT_MAX; i++)
	{
		if (mtp_recent[i].used && mtp_recent[i].handle == params[0])
			mtp_recent[i].used = 0;
	}
	storage_lfs_lock();
	if (is_dir)
		err = mtp_remove_recursive_locked(path);
	else
		err = lfs_remove(mtp_lfs(), path);
	storage_lfs_unlock();
	if (err < 0)
	{
		mtp_send_response(RSP_GENERAL_ERROR, NULL, 0);
		return;
	}
	mtp_cache_clear();
	mtp_send_ok0();
}

static void op_send_object_info(const uint32_t *params)
{
	char name[MTP_NAME_MAX];
	char dir[MTP_PATH_MAX];
	char path[MTP_PATH_MAX];
	uint32_t dlen, size, handle;
	uint16_t format;
	uint32_t rsp[3];
	uint32_t i;

	if (mtp_rx_container(mtp_prop, MTP_PROP_SIZE, &dlen) != 0)
	{
		mtp_send_response(RSP_INCOMPLETE_TRANSFER, NULL, 0);
		return;
	}
	if (mtp_get16(mtp_prop, 4) != MTP_TYPE_DATA)
	{
		mtp_send_response(RSP_INVALID_PARAMETER, NULL, 0);
		return;
	}
 	/* mtp_prop 保存整个容器（12 字节头 + 载荷）。
 	 * ObjectInfo 载荷中，15 个固定字段（52 字节）位于文件名字符串之前；
 	 * ObjectCompressedSize 在载荷偏移 8 处，ObjectFormat 在偏移 4 处，
 	 * ParentObject 在偏移 38 处。 */
	format = mtp_get16(mtp_prop, 12 + 4);
	if (mtp_read_str(mtp_prop, 12 + 52, dlen, name, sizeof(name)) != 0 ||
	    name[0] == 0)
	{
		printf("[MTP] sendinfo: bad filename\n");
		mtp_send_response(RSP_INVALID_PARAMETER, NULL, 0);
		return;
	}
	for (i = 0; name[i] != 0; i++)
	{
		if (name[i] == '/' || name[i] == '\\')
		{
			mtp_send_response(RSP_INVALID_PARAMETER, NULL, 0);
			return;
		}
	}

	if (params[1] == 0 || params[1] == 0xFFFFFFFFUL)
		strcpy(dir, "/");
	else
	{
		uint32_t psize;
		bool pdir;
		if (mtp_obj_find(params[1], dir, sizeof(dir), &psize, &pdir) != 0 ||
		    !pdir)
		{
			mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
			return;
		}
	}
	mtp_path_join(dir, name, path, sizeof(path));

	if (format == FORMAT_ASSOC)
	{
		/* 新建文件夹：SendObjectInfo 之后没有 SendObject 数据阶段 */
		int err;
		storage_lfs_lock();
		err = lfs_mkdir(mtp_lfs(), path);
		storage_lfs_unlock();
		if (err < 0)
		{
			mtp_send_response(RSP_GENERAL_ERROR, NULL, 0);
			return;
		}
		handle = mtp_obj_handle(path);
		mtp_recent_add(handle, path, 0, true);
		mtp_cache_clear();
		rsp[0] = params[0];
		rsp[1] = params[1];
		rsp[2] = handle;
		mtp_send_response(RSP_OK, rsp, 3);
		return;
	}

	size = mtp_get32(mtp_prop, 12 + 8); /* ObjectCompressedSize */
	if (mtp_pending_alloc(path, size, &handle) != 0)
	{
		mtp_send_response(RSP_STORE_FULL, NULL, 0);
		return;
	}
	rsp[0] = params[0];          /* storage */
	rsp[1] = params[1];          /* parent */
	rsp[2] = handle;
	mtp_send_response(RSP_OK, rsp, 3);
}

static void op_send_object(const uint32_t *params)
{
	uint32_t handle = params[0];
	mtp_pending_t *pe = NULL;
	uint32_t i, got, total, payload, first, remain;
	uint16_t type;
	lfs_file_t f;
	uint32_t rsp[1];
	int err;

	for (i = 0; i < MTP_PENDING_MAX; i++)
	{
		if (mtp_pending[i].used && mtp_pending[i].handle == handle)
		{
			pe = &mtp_pending[i];
			break;
		}
	}
	if (pe == NULL)
	{
 		/* 有些主机不回显分配的句柄；若恰好只有一个 pending 对象则无歧义，回退到它
 		 * 若恰好只有一个 pending 对象则无歧义，回退到它 */
		{
			uint32_t k, np = 0, last = 0;
			for (k = 0; k < MTP_PENDING_MAX; k++)
			{
				if (mtp_pending[k].used)
				{
					np++;
					last = k;
				}
			}
			if (np == 1)
			{
				pe = &mtp_pending[last];
			}
			else
			{
				mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
				return;
			}
		}
	}

 	/* 接收数据阶段容器头 */
	if (mtp_rx_arm_data(mtp_stream, 64) != 0)
	{
		mtp_pending_free(pe->handle);
		mtp_send_response(RSP_INCOMPLETE_TRANSFER, NULL, 0);
		return;
	}
	if (usb_wait_ep(EP_BULK_OUT, ARM_USBD_EVENT_OUT, 10000) != 0)
	{
		printf("[MTP] sendobj: data header timeout\n");
		mtp_pending_free(pe->handle);
		mtp_send_response(RSP_INCOMPLETE_TRANSFER, NULL, 0);
		return;
	}
	got = Driver_USBD0.EndpointTransferGetResult(EP_BULK_OUT);
	if (got < 12)
	{
		mtp_pending_free(pe->handle);
		mtp_send_response(RSP_INCOMPLETE_TRANSFER, NULL, 0);
		return;
	}
	total = mtp_get32(mtp_stream, 0);
	type = mtp_get16(mtp_stream, 4);
	if (type != MTP_TYPE_DATA || total < 12 ||
	    (total - 12) > FLASH_LITTLEFS_SIZE)
	{
		mtp_pending_free(pe->handle);
		mtp_send_response(RSP_INVALID_PARAMETER, NULL, 0);
		return;
	}
	payload = total - 12;
	first = got - 12;
	if (first > payload)
		first = payload;

	storage_lfs_lock();
	err = lfs_file_open(mtp_lfs(), &f, MTP_TMP_NAME,
	                    LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
	storage_lfs_unlock();
	if (err < 0)
	{
		mtp_pending_free(pe->handle);
		mtp_send_response(RSP_GENERAL_ERROR, NULL, 0);
		return;
	}

	if (first > 0)
	{
		int w;
		storage_lfs_lock();
		w = lfs_file_write(mtp_lfs(), &f, mtp_stream + 12, first);
		storage_lfs_unlock();
		if (w != (int)first)
			goto send_fail;
	}
	remain = payload - first;
	if (remain > 0 && mtp_rx_stream(&f, remain) != 0)
		goto send_fail;

	storage_lfs_lock();
	err = lfs_file_close(mtp_lfs(), &f);
	if (err >= 0)
		err = lfs_rename(mtp_lfs(), MTP_TMP_NAME, pe->name);
	storage_lfs_unlock();
	if (err < 0)
	{
		storage_lfs_lock();
		lfs_remove(mtp_lfs(), MTP_TMP_NAME);
		storage_lfs_unlock();
		mtp_pending_free(pe->handle);
		mtp_send_response(RSP_GENERAL_ERROR, NULL, 0);
		return;
	}
 	/* Windows 查询的是 SendObjectInfo 返回的句柄
 	 * （pe->handle，例如 0xF000），而不是 SendObject 的参数。 */
	mtp_recent_add(pe->handle, pe->name, payload, false);
	mtp_pending_free(pe->handle);
	mtp_cache_clear();
	rsp[0] = handle;
	mtp_send_response(RSP_OK, rsp, 1);
	return;

send_fail:
	storage_lfs_lock();
	lfs_file_close(mtp_lfs(), &f);
	lfs_remove(mtp_lfs(), MTP_TMP_NAME);
	storage_lfs_unlock();
	mtp_pending_free(pe->handle);
	mtp_send_response(RSP_INCOMPLETE_TRANSFER, NULL, 0);
}

static void op_get_object_props_supported(void)
{
	static const uint16_t props[] =
	{
		PROP_STORAGE_ID, PROP_OBJECT_FORMAT, PROP_PROTECTION_STATUS,
		PROP_OBJECT_SIZE, PROP_OBJECT_FILE_NAME, PROP_DATE_CREATED,
		PROP_DATE_MODIFIED, PROP_PARENT_OBJECT, PROP_PERSISTANT_UID,
		PROP_NAME
	};
	uint8_t *p = mtp_prop;
	uint32_t o = 0, i;
	mtp_put32(p, o, (uint32_t)(sizeof(props) / sizeof(props[0]))); o += 4;
	for (i = 0; i < sizeof(props) / sizeof(props[0]); i++)
	{
		mtp_put16(p, o, props[i]);
		o += 2;
	}
	mtp_tx_data_then_ok(p, o);
}

 /* 追加一条属性条目；返回新偏移，溢出时返回 0xFFFFFFFF */
static uint32_t mtp_prop_list_add(uint8_t *buf, uint32_t off, uint32_t cap,
								  uint32_t handle, uint16_t prop, uint16_t dtype,
								  const uint8_t *val, uint32_t vlen)
{
	uint32_t need = 8 + vlen;   /* handle + prop + dtype + value */
	if (off + need > cap)
		return 0xFFFFFFFFUL;
	mtp_put32(buf, off, handle); off += 4;
	mtp_put16(buf, off, prop); off += 2;
	mtp_put16(buf, off, dtype); off += 2;
	if (vlen)
		memcpy(buf + off, val, vlen);
	off += vlen;
	return off;
}

static uint32_t mtp_prop_list_object(uint8_t *buf, uint32_t off, uint32_t cap,
									 uint32_t handle, const char *path,
									 uint32_t size, bool is_dir,
									 uint32_t parent, uint16_t filter,
									 uint32_t *cnt)
{
	char date[17];
	uint8_t tmp[200];
	const char *name = mtp_basename(path);
	uint32_t n;

	mtp_date_str(date);
	if (filter == 0 || filter == PROP_STORAGE_ID)
	{
		n = 0; mtp_put32(tmp, n, MTP_STORAGE_ID); n += 4;
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_STORAGE_ID, DTYPE_U32, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_OBJECT_FORMAT)
	{
		n = 0; mtp_put16(tmp, n, is_dir ? FORMAT_ASSOC : 0x3000); n += 2;
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_OBJECT_FORMAT, DTYPE_U16, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_PROTECTION_STATUS)
	{
		n = 0; mtp_put16(tmp, n, 0x0000); n += 2;
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_PROTECTION_STATUS, DTYPE_U16, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_OBJECT_SIZE)
	{
		n = 0; mtp_put64(tmp, n, size); n += 8;
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_OBJECT_SIZE, DTYPE_U64, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_OBJECT_FILE_NAME)
	{
		n = mtp_put_str(tmp, name);
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_OBJECT_FILE_NAME, DTYPE_STR, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_DATE_CREATED)
	{
		n = mtp_put_str(tmp, date);
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_DATE_CREATED, DTYPE_STR, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_DATE_MODIFIED)
	{
		n = mtp_put_str(tmp, date);
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_DATE_MODIFIED, DTYPE_STR, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_PARENT_OBJECT)
	{
		n = 0; mtp_put32(tmp, n, parent); n += 4;
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_PARENT_OBJECT, DTYPE_U32, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_PERSISTANT_UID)
	{
 		/* MTP 规范：PersistantUniqueObjectIdentifier 是 UINT128（16 字节） */
		n = 0;
		mtp_put32(tmp, n, MTP_STORAGE_ID); n += 4;
		mtp_put32(tmp, n, handle); n += 4;
		memset(tmp + n, 0, 8); n += 8;
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_PERSISTANT_UID, DTYPE_U128, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	if (filter == 0 || filter == PROP_NAME)
	{
		n = mtp_put_str(tmp, name);
		off = mtp_prop_list_add(buf, off, cap, handle, PROP_NAME, DTYPE_STR, tmp, n);
		if (off == 0xFFFFFFFFUL) return off;
		(*cnt)++;
	}
	return off;
}

 /* 枚举 dir 下所有对象的属性列表（偏移 0 为条目数） */
static uint32_t mtp_dir_props(const char *dir, uint8_t *buf, uint32_t cap,
                              uint16_t filter, uint32_t *count)
{
	lfs_dir_t d;
	struct lfs_info info;
	uint32_t o = 4, cnt = 0;

	mtp_cache_clear();
	storage_lfs_lock();
	if (lfs_dir_open(mtp_lfs(), &d, dir) >= 0)
	{
	while (lfs_dir_read(mtp_lfs(), &d, &info) > 0)
		{
			char path[MTP_PATH_MAX];
			uint32_t ec = 0, no;
			if (info.name[0] == '~' || mtp_is_dotdot(info.name))
				continue;
			mtp_path_join(dir, info.name, path, sizeof(path));
			mtp_cache_add(mtp_obj_handle(path), path, (uint32_t)info.size,
			              info.type == LFS_TYPE_DIR);
			no = mtp_prop_list_object(buf, o, cap - 16, mtp_obj_handle(path),
			                          path, (uint32_t)info.size,
			                          info.type == LFS_TYPE_DIR,
			                          mtp_parent_handle(path),
			                          filter, &ec);
			if (no == 0xFFFFFFFFUL)
				break;
			o = no;
			cnt += ec;
		}
		lfs_dir_close(mtp_lfs(), &d);
	}
	storage_lfs_unlock();
	*count = cnt;
	return o;
}

static void op_get_object_prop_list(const uint32_t *params)
{
	uint32_t handle = params[0];
	uint16_t filter = (uint16_t)params[1];   /* Param2 = PropertyCode */
	uint8_t *p = mtp_prop;
	uint32_t o = 4;               /* reserve count field */
	uint32_t count = 0;

	if (handle == 0xFFFFFFFFUL)
	{
 		/* 设备属性：友好名称 + 同步伙伴 */
		uint8_t tmp[80];
		uint32_t n;
		if (filter == 0 || filter == 0xD401)
		{
			n = mtp_put_str(tmp, usb_str_product);
			o = mtp_prop_list_add(p, o, MTP_PROP_SIZE - 16,
			                      0xFFFFFFFFUL, 0xD401, DTYPE_STR, tmp, n);
			if (o != 0xFFFFFFFFUL)
				count++;
		}
		if (filter == 0 || filter == 0xD402)
		{
			n = mtp_put_str(tmp, usb_str_product);
			o = mtp_prop_list_add(p, o, MTP_PROP_SIZE - 16,
			                      0xFFFFFFFFUL, 0xD402, DTYPE_STR, tmp, n);
			if (o != 0xFFFFFFFFUL)
				count++;
		}
		if (o == 0xFFFFFFFFUL)
			o = 4;
		mtp_put32(p, 0, count);
		if (mtp_tx_data(p, o) == 0)
			mtp_send_ok0();
		return;
	}
	if (handle == 0)
	{
		o = mtp_dir_props("/", p, MTP_PROP_SIZE, filter, &count);
	}
	else
	{
		char path[MTP_PATH_MAX];
		uint32_t size;
		bool is_dir;
		if (mtp_obj_find(handle, path, sizeof(path), &size, &is_dir) == 0)
		{
			uint32_t no = mtp_prop_list_object(p, o, MTP_PROP_SIZE - 16,
			                                   handle, path, size, is_dir,
			                                   mtp_parent_handle(path),
			                                   filter, &count);
			if (no != 0xFFFFFFFFUL)
				o = no;
		}
	}
	mtp_put32(p, 0, count);
	if (mtp_tx_data(p, o) == 0)
		mtp_send_ok0();
}

static void op_get_object_prop_value(const uint32_t *params)
{
	uint32_t handle = params[0];
	uint16_t prop = (uint16_t)params[1];
	char path[MTP_PATH_MAX];
	uint32_t size;
	uint8_t *p = mtp_prop;
	uint32_t o = 0;
	uint16_t dtype;
	uint32_t rsp[1];
	char date[17];
	bool is_dir;

	if (mtp_obj_find(handle, path, sizeof(path), &size, &is_dir) != 0)
	{
		printf("[MTP] propval h=%08lX prop=%04X -> no such handle\n",
		       (unsigned long)handle, (unsigned)prop);
		mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
		return;
	}
	mtp_date_str(date);
	switch (prop)
	{
	case PROP_STORAGE_ID:
		dtype = DTYPE_U32;
		mtp_put32(p, o, MTP_STORAGE_ID);
		o += 4;
		break;
	case PROP_OBJECT_FORMAT:
		dtype = DTYPE_U16;
		mtp_put16(p, o, is_dir ? FORMAT_ASSOC : 0x3000);
		o += 2;
		break;
	case PROP_PROTECTION_STATUS:
		dtype = DTYPE_U16;
		mtp_put16(p, o, 0x0000);
		o += 2;
		break;
	case PROP_OBJECT_SIZE:
		dtype = DTYPE_U64;
		mtp_put64(p, o, size);
		o += 8;
		break;
	case PROP_OBJECT_FILE_NAME:
	case PROP_NAME:
		dtype = DTYPE_STR;
		o += mtp_put_str(p + o, mtp_basename(path));
		break;
	case PROP_DATE_CREATED:
	case PROP_DATE_MODIFIED:
		dtype = DTYPE_STR;
		o += mtp_put_str(p + o, date);
		break;
	case PROP_PARENT_OBJECT:
		dtype = DTYPE_U32;
		mtp_put32(p, o, mtp_parent_handle(path));
		o += 4;
		break;
	case PROP_PERSISTANT_UID:
		dtype = DTYPE_U128;
		mtp_put32(p, o, MTP_STORAGE_ID); o += 4;
		mtp_put32(p, o, handle); o += 4;
		memset(p + o, 0, 8); o += 8;
		break;
	default:
		mtp_send_response(RSP_OPERATION_NOT_SUPPORTED, NULL, 0);
		return;
	}
	if (mtp_tx_data(mtp_prop, o) == 0)
	{
		rsp[0] = dtype;
		mtp_send_response(RSP_OK, rsp, 1);
	}
}

static void op_get_object_prop_desc(const uint32_t *params)
{
	uint16_t prop = (uint16_t)params[0];
	uint8_t *p = mtp_prop;
	uint32_t o = 0;
	uint16_t dtype;
	uint8_t getset;

	switch (prop)
	{
	case PROP_STORAGE_ID:      dtype = DTYPE_U32; getset = 1; break;
	case PROP_OBJECT_FORMAT:   dtype = DTYPE_U16; getset = 1; break;
	case PROP_PROTECTION_STATUS: dtype = DTYPE_U16; getset = 1; break;
	case PROP_OBJECT_SIZE:     dtype = DTYPE_U64; getset = 1; break;
	case PROP_OBJECT_FILE_NAME: dtype = DTYPE_STR; getset = 1; break;  /* GET/SET */
	case PROP_DATE_CREATED:    dtype = DTYPE_STR; getset = 1; break;
	case PROP_DATE_MODIFIED:   dtype = DTYPE_STR; getset = 1; break;
	case PROP_PARENT_OBJECT:   dtype = DTYPE_U32; getset = 1; break;
	case PROP_PERSISTANT_UID:  dtype = DTYPE_U128; getset = 1; break;
	case PROP_NAME:            dtype = DTYPE_STR; getset = 1; break;  /* GET/SET */
	default:
		mtp_send_response(RSP_OPERATION_NOT_SUPPORTED, NULL, 0);
		return;
	}
	mtp_put16(p, o, prop); o += 2;
	mtp_put16(p, o, dtype); o += 2;
	p[o++] = getset;
	if (dtype == DTYPE_STR)
		o += mtp_put_str(p + o, "");
	else if (dtype == DTYPE_U64)
	{
		mtp_put64(p, o, 0);
		o += 8;
	}
	else if (dtype == DTYPE_U32)
	{
		mtp_put32(p, o, 0);
		o += 4;
	}
	else if (dtype == DTYPE_U16)
	{
		mtp_put16(p, o, 0);
		o += 2;
	}
	else if (dtype == DTYPE_U128)
	{
		memset(p + o, 0, 16);
		o += 16;
	}
	else
	{
		p[o++] = 0;
	}
	mtp_put32(p, o, 0); o += 4;   /* group code */
	p[o++] = 0x00;                 /* form flag: none */
	mtp_tx_data_then_ok(p, o);
}

static void op_get_device_prop_desc(const uint32_t *params)
{
	uint16_t prop = (uint16_t)params[0];
	uint8_t *p = mtp_prop;
	uint32_t o = 0;

	if (prop == 0xD401)           /* Device Friendly Name (string) */
	{
		mtp_put16(p, o, prop); o += 2;
		mtp_put16(p, o, DTYPE_STR); o += 2;
		p[o++] = 0x01;                       /* get only */
		o += mtp_put_str(p + o, usb_str_product);  /* factory default */
		o += mtp_put_str(p + o, usb_str_product);  /* current value */
		p[o++] = 0x00;
	}
	else if (prop == 0xD402)      /* Synchronization Partner (string) */
	{
		mtp_put16(p, o, prop); o += 2;
		mtp_put16(p, o, DTYPE_STR); o += 2;
		p[o++] = 0x01;                       /* get only */
		o += mtp_put_str(p + o, usb_str_product);  /* WPD reads 0xD402 as friendly name too */
		o += mtp_put_str(p + o, usb_str_product);
		p[o++] = 0x00;
	}
	else if (prop == 0xD407)      /* Perceived Device Type (UINT16) */
	{
		mtp_put16(p, o, prop); o += 2;
		mtp_put16(p, o, DTYPE_U16); o += 2;
		p[o++] = 0x01;                       /* get only */
		mtp_put16(p, o, 0x0000); o += 2;     /* factory default */
		mtp_put16(p, o, 0x0000); o += 2;     /* current value */
		p[o++] = 0x00;
	}
	else
	{
		mtp_send_response(RSP_DEVICE_PROP_NOT_SUPPORT, NULL, 0);
		return;
	}
	mtp_tx_data_then_ok(p, o);
}

static void op_get_device_prop_value(const uint32_t *params)
{
	uint16_t prop = (uint16_t)params[0];
	uint8_t *p = mtp_prop;
	uint32_t rsp[1];
	uint32_t o = 0;

	if (prop == 0xD401)
	{
		o += mtp_put_str(p + o, usb_str_product);
		if (mtp_tx_data(p, o) == 0)
		{
			rsp[0] = DTYPE_STR;
			mtp_send_response(RSP_OK, rsp, 1);
		}
	}
	else if (prop == 0xD402)
	{
		o += mtp_put_str(p + o, usb_str_product);
		if (mtp_tx_data(p, o) == 0)
		{
			rsp[0] = DTYPE_STR;
			mtp_send_response(RSP_OK, rsp, 1);
		}
	}
	else if (prop == 0xD407)
	{
		mtp_put16(p, o, 0x0000);
		o += 2;
		if (mtp_tx_data(p, o) == 0)
		{
			rsp[0] = DTYPE_U16;
			mtp_send_response(RSP_OK, rsp, 1);
		}
	}
	else
	{
		mtp_send_response(RSP_DEVICE_PROP_NOT_SUPPORT, NULL, 0);
	}
}

static void op_get_object_refs(void)
{
	uint8_t *p = mtp_prop;
	mtp_put32(p, 0, 0);
	mtp_tx_data_then_ok(p, 4);
}

static void op_set_object_prop_list(const uint32_t *params)
{
	uint32_t dlen;
 	/* 排空传入的属性列表；若存在重命名则执行 */
	if (mtp_rx_container(mtp_prop, MTP_PROP_SIZE, &dlen) != 0)
	{
		mtp_send_response(RSP_INCOMPLETE_TRANSFER, NULL, 0);
		return;
	}
	if (dlen >= 16)
	{
 		/* 属性列表载荷从 12 字节容器头之后开始 */
		uint32_t count = mtp_get32(mtp_prop, 12);
		uint32_t off = 16;
		uint32_t i;
		for (i = 0; i < count && off + 8 <= dlen; i++)
		{
			uint32_t handle = mtp_get32(mtp_prop, off);
			uint16_t prop = mtp_get16(mtp_prop, off + 4);
			uint16_t dtype = mtp_get16(mtp_prop, off + 6);
			off += 8;
			if (prop == PROP_OBJECT_FILE_NAME && dtype == DTYPE_STR && off < dlen)
			{
				char newname[MTP_NAME_MAX];
				char oldpath[MTP_PATH_MAX];
				char newpath[MTP_PATH_MAX];
				uint32_t size;
				bool is_dir;
				uint32_t k;
				if (mtp_read_str(mtp_prop, off, dlen, newname, sizeof(newname)) != 0 ||
				    newname[0] == 0)
				{
					printf("[MTP] setprop: bad name\n");
				}
				else if (mtp_obj_find(handle, oldpath, sizeof(oldpath),
				                      &size, &is_dir) != 0)
				{
					printf("[MTP] setprop: unknown handle %08lX\n",
					       (unsigned long)handle);
				}
				else
				{
					for (k = 0; newname[k] != 0; k++)
						if (newname[k] == '/' || newname[k] == '\\')
							break;
					if (newname[k] != 0)
					{
						printf("[MTP] setprop: bad chars in name\n");
					}
					else
					{
						char parent[MTP_PATH_MAX];
						int r;
						mtp_parent_path(oldpath, parent, sizeof(parent));
						mtp_path_join(parent, newname, newpath, sizeof(newpath));
						storage_lfs_lock();
						r = lfs_rename(mtp_lfs(), oldpath, newpath);
						storage_lfs_unlock();
						if (r == 0)
						{
							mtp_rename_add(handle, newpath, size, is_dir);
							mtp_cache_clear();
						}
					}
				}
			}
 			/* 跳过值 */
			if (dtype == DTYPE_STR && off < dlen)
				off += 1 + 2 * mtp_prop[off];   /* len byte + chars incl null */
			else if (dtype == DTYPE_U64)
				off += 8;
			else if (dtype == DTYPE_U32 || dtype == DTYPE_U16 || dtype == DTYPE_U8)
				off += (dtype == DTYPE_U32) ? 4 : ((dtype == DTYPE_U16) ? 2 : 1);
			else
				off = dlen;   /* unknown, stop */
		}
	}
	mtp_send_ok0();
}

static void op_initiate_capture(void)
{
	mtp_send_ok0();
}

 /* CopyObject/MoveObject: 设备内复制/移动（文件与文件夹）。
  * 参数: ObjectHandle, StorageID, ParentObject。 */
static void op_copy_move_object(const uint32_t *params, bool move)
{
	char src[MTP_PATH_MAX], dir[MTP_PATH_MAX], dst[MTP_PATH_MAX];
	uint32_t size, psize;
	bool is_dir, pdir;
	uint32_t rsp[1];
	int err;

	if (mtp_obj_find(params[0], src, sizeof(src), &size, &is_dir) != 0)
	{
		mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
		return;
	}
	if (params[1] == 0 || params[1] == 0xFFFFFFFFUL)
		strcpy(dir, "/");
	else if (mtp_obj_find(params[1], dir, sizeof(dir), &psize, &pdir) != 0 ||
	         !pdir)
	{
		mtp_send_response(RSP_INVALID_OBJECT_HANDLE, NULL, 0);
		return;
	}
	mtp_path_join(dir, mtp_basename(src), dst, sizeof(dst));

	if (strcmp(src, dst) == 0)
	{
		/* 同路径：不做任何事，防止复制时 TRUNC 清空源文件 */
		rsp[0] = mtp_obj_handle(dst);
		mtp_send_response(RSP_OK, rsp, 1);
		return;
	}

	storage_lfs_lock();
	if (move)
		err = lfs_rename(mtp_lfs(), src, dst);
	else if (is_dir)
	{
		err = lfs_mkdir(mtp_lfs(), dst);
		if (err >= 0)
			err = mtp_copy_dir_locked(src, dst);
	}
	else
		err = mtp_copy_file_locked(src, dst);
	storage_lfs_unlock();

	if (err < 0)
	{
		mtp_send_response(RSP_GENERAL_ERROR, NULL, 0);
		return;
	}
	mtp_cache_clear();
	rsp[0] = mtp_obj_handle(dst);
	mtp_send_response(RSP_OK, rsp, 1);
}

 /* ------------------------- 分发 ------------------------- */
static void mtp_dispatch(uint16_t code, const uint32_t *params)
{
	mtp_current_op = code;
	switch (code)
	{
	case OP_GET_DEVICE_INFO:
		op_get_device_info();
		break;
	case OP_OPEN_SESSION:
		op_open_session(params);
		break;
	case OP_CLOSE_SESSION:
		op_close_session();
		break;
	case OP_GET_STORAGE_IDS:
		op_get_storage_ids();
		break;
	case OP_GET_STORAGE_INFO:
		op_get_storage_info();
		break;
	case OP_GET_NUM_OBJECTS:
		op_get_num_objects(params);
		break;
	case OP_GET_OBJECT_HANDLES:
		op_get_object_handles(params);
		break;
	case OP_GET_OBJECT_INFO:
		op_get_object_info(params);
		break;
	case OP_GET_OBJECT:
		op_get_object(params);
		break;
	case OP_GET_THUMB:
		op_get_thumb();
		break;
	case OP_DELETE_OBJECT:
		op_delete_object(params);
		break;
	case OP_SEND_OBJECT_INFO:
		op_send_object_info(params);
		break;
	case OP_SEND_OBJECT:
		mtp_busy = 1;
		op_send_object(params);
		mtp_busy = 0;
		break;
	case OP_INITIATE_CAPTURE:
		op_initiate_capture();
		break;
	case OP_COPY_OBJECT:
		op_copy_move_object(params, false);
		break;
	case OP_MOVE_OBJECT:
		op_copy_move_object(params, true);
		break;
	case OP_GET_OBJECT_PROPS_SUPPORT:
		op_get_object_props_supported();
		break;
	case OP_GET_DEVICE_PROP_DESC:
		op_get_device_prop_desc(params);
		break;
	case OP_GET_DEVICE_PROP_VALUE:
		op_get_device_prop_value(params);
		break;
	case OP_GET_OBJECT_PROP_VALUE:
		op_get_object_prop_value(params);
		break;
	case OP_GET_OBJECT_PROP_LIST:
		op_get_object_prop_list(params);
		break;
	case OP_SET_OBJECT_PROP_LIST:
		op_set_object_prop_list(params);
		break;
	case OP_GET_OBJECT_PROP_DESC:
		op_get_object_prop_desc(params);
		break;
	case OP_GET_OBJECT_REFS:
		op_get_object_refs();
		break;
	default:
		printf("[MTP] unsupported op 0x%04X\n", code);
		mtp_send_response(RSP_OPERATION_NOT_SUPPORTED, NULL, 0);
		break;
	}
}

static void mtp_process_command(void)
{
	uint32_t len, trans;
	uint16_t type, code;
	uint32_t params[5];
	uint32_t i, nparams;

	len = mtp_get32(mtp_rx_buf, 0);
	type = mtp_get16(mtp_rx_buf, 4);
	code = mtp_get16(mtp_rx_buf, 6);
	trans = mtp_get32(mtp_rx_buf, 8);
	if (type != MTP_TYPE_COMMAND || len < 12 || len > 64)
	{
		printf("[MTP] bad container len=%lu type=%u\n",
		       (unsigned long)len, (unsigned)type);
		mtp_rx_arm();
		return;
	}
	nparams = (len - 12) / 4;
	if (nparams > 5)
		nparams = 5;
	for (i = 0; i < nparams; i++)
		params[i] = mtp_get32(mtp_rx_buf, 12 + 4 * i);

	mtp_trans_id = trans;
	mtp_dispatch(code, params);
	mtp_rx_arm();
}

 /* ------------------------- 任务 / 初始化 ------------------------- */
static void usb_dispatch(usb_evt_t *e)
{
	if (e->type == 0)
	{
		if ((e->evt & ARM_USBD_EVENT_RESET) != 0U)
		{
			printf("[USB] reset\n");
			usb_configured = 0;
			mtp_pending_reset();
 			/* EP0 必须在主机的第一个 GET_DESCRIPTOR 之前配置好（最大包 64 字节），
 			 * 否则 EP0 IN 传输长度会被算成 0，发出的将是 ZLP 而不是描述符。 */
			Driver_USBD0.EndpointConfigure(0x00, ARM_USB_ENDPOINT_CONTROL, 64);
			Driver_USBD0.EndpointConfigure(0x80, ARM_USB_ENDPOINT_CONTROL, 64);
			ctrl_ep0_arm_setup();
		}
		return;
	}
	switch (e->ep)
	{
	case 0x00:
		ctrl_handle_setup();
		break;
	case 0x80:
	case EP_INTR_IN:
		break;
	case EP_BULK_OUT:
		mtp_process_command();
		break;
	case EP_BULK_IN:
		break;
	default:
		break;
	}
}

static void mtp_task(void *param)
{
	uint32_t last_irq_seen = 0;
	uint32_t kick_time = 0;
	(void)param;
	for (;;)
	{
		usb_evt_t e;
		uint32_t now = (uint32_t)xTaskGetTickCount();
/* 若发生了复位但枚举始终未完成，在静默期后重新触发主机（重连踢一脚） */
		if (usb_configured == 0)
		{
			if (usb_irq_count != last_irq_seen)
			{
				last_irq_seen = usb_irq_count;
				kick_time = now;
			}
			else if (usb_irq_count > 0 && (now - kick_time) >= pdMS_TO_TICKS(15000))
			{
				kick_time = now;
				printf("[USB] reconnect kick\n");
				Driver_USBD0.DeviceDisconnect();
				vTaskDelay(pdMS_TO_TICKS(100));
				Driver_USBD0.DeviceConnect();
			}
		}
		if (xQueueReceive(usb_evt_queue, &e, pdMS_TO_TICKS(50)) == pdTRUE)
			usb_dispatch(&e);
	}
}

bool usb_mtp_init(void)
{
	GPIO_InitTypeDef g;

	usb_serial_build();
	printf("[USB] serial %s\n", usb_str_serial);

 	/* PA11（USB_D-）/ PA12（USB_D+）复用为 OTG_FS */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource11, GPIO_AF_OTG_FS);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF_OTG_FS);
	GPIO_StructInit(&g);
	g.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
	g.GPIO_Mode = GPIO_Mode_AF;
	g.GPIO_OType = GPIO_OType_PP;
	g.GPIO_Speed = GPIO_High_Speed;
	g.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &g);

 	/* OTG_FS 中断会在 ISR 中调用 FreeRTOS：优先级必须 >= configMAX_SYSCALL_INTERRUPT_PRIORITY。
 	 * 注意：本工程旧版 CMSIS NVIC_SetPriority() 会左移 (8 - __NVIC_PRIO_BITS) 位，
 	 * 所以这里传原始抢占优先级（0..15），而不是编码后的值。 */
	NVIC_SetPriority(OTG_FS_IRQn, 5);

	usb_evt_queue = xQueueCreate(32, sizeof(usb_evt_t));
	if (usb_evt_queue == NULL)
	{
		printf("[USB] event queue failed\n");
		return false;
	}

	if (Driver_USBD0.Initialize(usb_dev_evt_cb, usb_ep_evt_cb) != ARM_DRIVER_OK)
	{
		printf("[USB] driver init failed\n");
		return false;
	}
	if (Driver_USBD0.PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK)
	{
		printf("[USB] power control failed\n");
		return false;
	}
	if (Driver_USBD0.DeviceConnect() != ARM_DRIVER_OK)
	{
		printf("[USB] connect failed\n");
		return false;
	}
 	/* 提前配置 EP0（总线复位时也会再做一次） */
	Driver_USBD0.EndpointConfigure(0x00, ARM_USB_ENDPOINT_CONTROL, 64);
	Driver_USBD0.EndpointConfigure(0x80, ARM_USB_ENDPOINT_CONTROL, 64);

 	/* ponytail：MTP 必须与 main_init 同处最高优先级（9），
 	 * main_init 在 WiFi AT 命令中最多阻塞约 15 秒（信号量等待）；
 	 * 若用优先级 4，Windows 会因等待 GetStorageIDs 超时而中止设备初始化。
 	 * 上限：与 init/定时器任务共享优先级 9，因此如果 USB 延迟成为问题，
 	 * 需要把 WiFi 移出阻塞式初始化路径。 */
	xTaskCreate(mtp_task, "mtp", 1024, NULL, 9, NULL);
	printf("[USB] MTP ready prio=9 (PA12/PA11 -> CH334 D3)\n");
	return true;
}
