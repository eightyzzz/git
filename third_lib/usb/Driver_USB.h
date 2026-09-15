/* -----------------------------------------------------------------------------
 * Driver_USB.h - USB Driver common definitions (CMSIS-Driver API)
 *
 * Minimal definitions required by Driver_USBD.h / USBD_FS_STM32F4xx.c.
 * Values follow the ARM CMSIS-Driver specification.
 * -------------------------------------------------------------------------- */
#ifndef DRIVER_USB_H_
#define DRIVER_USB_H_

#include "Driver_Common.h"

/****** USB Speed *****/
#define ARM_USB_SPEED_LOW                0
#define ARM_USB_SPEED_FULL               1
#define ARM_USB_SPEED_HIGH               2

/****** USB Role *****/
#define ARM_USB_ROLE_NONE                0
#define ARM_USB_ROLE_HOST                1
#define ARM_USB_ROLE_DEVICE              2

/****** USB Endpoint Type *****/
#define ARM_USB_ENDPOINT_CONTROL         0
#define ARM_USB_ENDPOINT_ISOCHRONOUS     1
#define ARM_USB_ENDPOINT_BULK            2
#define ARM_USB_ENDPOINT_INTERRUPT       3

/****** USB Endpoint Address *****/
#define ARM_USB_ENDPOINT_NUMBER_MASK     0x0F
#define ARM_USB_ENDPOINT_DIRECTION_MASK  0x80
#define ARM_USB_ENDPOINT_MAX_PACKET_SIZE_MASK 0x7FF

/****** USB Pins *****/
#define ARM_USB_PIN_DP                   0x01
#define ARM_USB_PIN_DM                   0x02
#define ARM_USB_PIN_VBUS                 0x04
#define ARM_USB_PIN_OC                   0x08

#endif /* DRIVER_USB_H_ */
