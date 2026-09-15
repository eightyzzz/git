#ifndef __SPI_FLASH_H__
#define __SPI_FLASH_H__

#include <stdbool.h>
#include <stdint.h>

//板载两颗华邦 W25Q128 (16MB, JEDEC: EF 40 18), 共用 SPI1 总线
//SPI1: SCK=PA5, MISO=PA6, MOSI=PA7
//TX: DMA2_Stream3_Ch3, RX: DMA2_Stream0_Ch3
//CS: PE13 = 片1(多分区: Boot/App/资源/FlashDB), PE14 = 片2(LittleFS)

typedef enum
{
	SPI_FLASH_1 = 0,	//PE13
	SPI_FLASH_2 = 1,	//PE14
	SPI_FLASH_NUM
} spi_flash_t;

bool spi_flash_init(void);
bool spi_flash_read_id(spi_flash_t dev, uint8_t id[3]);			//返回 JEDEC ID, 期望 EF 40 18
bool spi_flash_read(spi_flash_t dev, uint32_t addr, uint8_t *buf, uint32_t len);
bool spi_flash_write(spi_flash_t dev, uint32_t addr, const uint8_t *buf, uint32_t len);	//自动按256字节页拆分
bool spi_flash_erase_sector(spi_flash_t dev, uint32_t addr);		//4KB 扇区擦除
bool spi_flash_erase_block32(spi_flash_t dev, uint32_t addr);	//32KB 块擦除
bool spi_flash_erase_block64(spi_flash_t dev, uint32_t addr);	//64KB 块擦除
bool spi_flash_erase_chip(spi_flash_t dev);						//全片擦除(耗时较长)

#endif
