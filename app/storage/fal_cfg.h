#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fal.h"

/* ============ Flash 设备表 ============ */
extern struct fal_flash_dev w25q128_dev1;
extern struct fal_flash_dev w25q128_dev2;

#define FAL_FLASH_DEV_TABLE \
{ \
	&w25q128_dev1, \
	&w25q128_dev2, \
}

/* ============ 分区表 (与 flash_partition.h 保持一致) ============ */
#define FAL_PART_HAS_TABLE_CFG
#define FAL_PART_TABLE \
{ \
	{FAL_PART_MAGIC_WORD, "boot",     "w25q128_1", 0x00000000, 0x00100000, 0}, \
	{FAL_PART_MAGIC_WORD, "app_a",    "w25q128_1", 0x00100000, 0x00100000, 0}, \
	{FAL_PART_MAGIC_WORD, "app_b",    "w25q128_1", 0x00200000, 0x00100000, 0}, \
	{FAL_PART_MAGIC_WORD, "resource", "w25q128_1", 0x00300000, 0x00400000, 0}, \
	{FAL_PART_MAGIC_WORD, "fdb",      "w25q128_1", 0x00700000, 0x00200000, 0}, \
	{FAL_PART_MAGIC_WORD, "lfs",      "w25q128_2", 0x00000000, 0x01000000, 0}, \
}

#endif /* _FAL_CFG_H_ */
