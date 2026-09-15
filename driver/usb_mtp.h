#ifndef __USB_MTP_H__
#define __USB_MTP_H__

#include <stdbool.h>

/*
 * USB MTP (Media Transfer Protocol) over STM32F407 OTG_FS (PA11/PA12),
 * connected to a CH334 hub downstream port (CH334_D3+/D3-).
 * The LittleFS on W25Q128_2 is exposed to the PC as an MTP storage.
 *
 * Must be called after storage_init() (LittleFS mounted).
 */
bool usb_mtp_init(void);

#endif /* __USB_MTP_H__ */
