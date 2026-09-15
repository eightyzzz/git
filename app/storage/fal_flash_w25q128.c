#include "fal.h"
#include "spi_flash.h"

//FAL 底层: 把 W25Q128 两个片子的读写擦除接到 spi_flash 驱动

#define W25Q128_SIZE		0x01000000	//16MB
#define W25Q128_BLK_SIZE	4096		//扇区 4KB

static int w25q128_init(void)
{
	return 0;
}

static int w25q128_1_read(long offset, uint8_t *buf, size_t size)
{
	return spi_flash_read(SPI_FLASH_1, (uint32_t)offset, buf, size) ? 0 : -1;
}

static int w25q128_1_write(long offset, const uint8_t *buf, size_t size)
{
	return spi_flash_write(SPI_FLASH_1, (uint32_t)offset, buf, size) ? 0 : -1;
}

static int w25q128_1_erase(long offset, size_t size)
{
	for (size_t i = 0; i < size; i += W25Q128_BLK_SIZE)
	{
		if (!spi_flash_erase_sector(SPI_FLASH_1, (uint32_t)(offset + i)))
			return -1;
	}
	return 0;
}

static int w25q128_2_read(long offset, uint8_t *buf, size_t size)
{
	return spi_flash_read(SPI_FLASH_2, (uint32_t)offset, buf, size) ? 0 : -1;
}

static int w25q128_2_write(long offset, const uint8_t *buf, size_t size)
{
	return spi_flash_write(SPI_FLASH_2, (uint32_t)offset, buf, size) ? 0 : -1;
}

static int w25q128_2_erase(long offset, size_t size)
{
	for (size_t i = 0; i < size; i += W25Q128_BLK_SIZE)
	{
		if (!spi_flash_erase_sector(SPI_FLASH_2, (uint32_t)(offset + i)))
			return -1;
	}
	return 0;
}

struct fal_flash_dev w25q128_dev1 =
{
	.name = "w25q128_1",
	.addr = 0,
	.len = W25Q128_SIZE,
	.blk_size = W25Q128_BLK_SIZE,
	.ops = {w25q128_init, w25q128_1_read, w25q128_1_write, w25q128_1_erase},
	.write_gran = 1,
};

struct fal_flash_dev w25q128_dev2 =
{
	.name = "w25q128_2",
	.addr = 0,
	.len = W25Q128_SIZE,
	.blk_size = W25Q128_BLK_SIZE,
	.ops = {w25q128_init, w25q128_2_read, w25q128_2_write, w25q128_2_erase},
	.write_gran = 1,
};
