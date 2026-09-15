#ifndef __FLASH_PARTITION_H__
#define __FLASH_PARTITION_H__

#include <stdint.h>

//芯片1 (W25Q128_1, PE13) 多分区布局, 共16MB
//芯片2 (W25Q128_2, PE14) 整片作为 LittleFS
typedef enum
{
	FLASH_PART_BOOT = 0,	//BootLoader
	FLASH_PART_APP_A,		//应用程序
	FLASH_PART_APP_B,		//备份/回退版本
	FLASH_PART_RESOURCE,	//系统默认资源
	FLASH_PART_FDB,			//FlashDB 数据库
	FLASH_PART_MAX
} flash_part_t;

typedef struct
{
	uint32_t addr;
	uint32_t size;
} flash_part_info_t;

static const flash_part_info_t flash_part_table[FLASH_PART_MAX] =
{
	{0x00000000, 0x00100000},	//BOOT      1MB
	{0x00100000, 0x00100000},	//APP_A     1MB
	{0x00200000, 0x00100000},	//APP_B     1MB
	{0x00300000, 0x00400000},	//RESOURCE  4MB
	{0x00700000, 0x00200000},	//FDB       2MB
};

//芯片2 LittleFS 整片
#define FLASH_LITTLEFS_ADDR		0x00000000
#define FLASH_LITTLEFS_SIZE		0x01000000

static inline const flash_part_info_t *flash_part_get(flash_part_t part)
{
	return &flash_part_table[part];
}

#endif
