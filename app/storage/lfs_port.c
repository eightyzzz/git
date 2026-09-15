#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "lfs.h"
#include "spi_flash.h"
#include "flash_partition.h"
#include "lfs_port.h"

//片2 LittleFS 参数
#define LFS_BLOCK_SIZE		4096	//扇区擦除粒度
#define LFS_BLOCK_COUNT		(FLASH_LITTLEFS_SIZE / LFS_BLOCK_SIZE)

static lfs_t lfs;
static struct lfs_config lfs_cfg;
static SemaphoreHandle_t lfs_mutex;

/* LittleFS is shared by the LVGL file system driver and the USB MTP task;
 * serialize every lfs_* call with a mutex. */
void storage_lfs_lock_init(void)
{
	if (lfs_mutex == NULL)
		lfs_mutex = xSemaphoreCreateMutex();
}

void storage_lfs_lock(void)
{
	if (lfs_mutex != NULL)
		xSemaphoreTake(lfs_mutex, portMAX_DELAY);
}

void storage_lfs_unlock(void)
{
	if (lfs_mutex != NULL)
		xSemaphoreGive(lfs_mutex);
}

//ARMCC + Microlib: 提供断言符号 (LittleFS LFS_ASSERT 默认使用 assert)
void __aeabi_assert(const char *expr, const char *file, int line)
{
	printf("[ASSERT] %s:%d %s\n", file, line, expr);
	while (1);
}

static int lfs_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	if (spi_flash_read(SPI_FLASH_2, FLASH_LITTLEFS_ADDR + block * LFS_BLOCK_SIZE + off, buffer, size))
		return LFS_ERR_OK;
	return LFS_ERR_IO;
}

static int lfs_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	if (spi_flash_write(SPI_FLASH_2, FLASH_LITTLEFS_ADDR + block * LFS_BLOCK_SIZE + off, buffer, size))
		return LFS_ERR_OK;
	return LFS_ERR_IO;
}

static int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
	if (spi_flash_erase_sector(SPI_FLASH_2, FLASH_LITTLEFS_ADDR + block * LFS_BLOCK_SIZE))
		return LFS_ERR_OK;
	return LFS_ERR_IO;
}

static int lfs_flash_sync(const struct lfs_config *c)
{
	return LFS_ERR_OK;
}

lfs_t *storage_lfs_get(void)
{
	return &lfs;
}

const struct lfs_config *storage_lfs_config(void)
{
	if (lfs_cfg.block_size == 0)
	{
		memset(&lfs_cfg, 0, sizeof(lfs_cfg));
		lfs_cfg.read = lfs_flash_read;
		lfs_cfg.prog = lfs_flash_prog;
		lfs_cfg.erase = lfs_flash_erase;
		lfs_cfg.sync = lfs_flash_sync;
		lfs_cfg.read_size = 1;
		lfs_cfg.prog_size = 256;			//页编程大小
		lfs_cfg.block_size = LFS_BLOCK_SIZE;
		lfs_cfg.block_count = LFS_BLOCK_COUNT;
		lfs_cfg.cache_size = 256;
		lfs_cfg.lookahead_size = 16;
		lfs_cfg.block_cycles = 500;
	}
	return &lfs_cfg;
}
