#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "stm32f4xx.h"
#include "tim_delay.h"
#include "spi_flash.h"

//W25Q128 指令
#define FLASH_CMD_WRITE_ENABLE		0x06
#define FLASH_CMD_WRITE_DISABLE		0x04
#define FLASH_CMD_READ_STATUS		0x05
#define FLASH_CMD_READ_DATA			0x03
#define FLASH_CMD_PAGE_PROGRAM		0x02
#define FLASH_CMD_SECTOR_ERASE		0x20	//4KB
#define FLASH_CMD_BLOCK_ERASE_32K	0x52
#define FLASH_CMD_BLOCK_ERASE_64K	0xD8
#define FLASH_CMD_CHIP_ERASE		0xC7
#define FLASH_CMD_JEDEC_ID			0x9F

#define FLASH_STATUS_WIP			0x01
#define FLASH_STATUS_WEL			0x02

#define FLASH_PAGE_SIZE				256
#define FLASH_SECTOR_SIZE			4096
#define FLASH_CHIP_SIZE				0x1000000	//16MB

#define FLASH_CS_PIN_1				GPIO_Pin_13
#define FLASH_CS_PIN_2				GPIO_Pin_14

static uint8_t spi_dummy = 0xFF;

static void spi1_gpio_init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

	GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);	//SCK
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);	//MISO
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);	//MOSI

	GPIO_InitTypeDef g;
	GPIO_StructInit(&g);
	g.GPIO_Mode = GPIO_Mode_AF;
	g.GPIO_OType = GPIO_OType_PP;
	g.GPIO_Speed = GPIO_High_Speed;
	g.GPIO_PuPd = GPIO_PuPd_NOPULL;
	g.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_Init(GPIOA, &g);

	//两路片选: 默认拉高
	GPIO_StructInit(&g);
	g.GPIO_Mode = GPIO_Mode_OUT;
	g.GPIO_OType = GPIO_OType_PP;
	g.GPIO_Speed = GPIO_High_Speed;
	g.GPIO_Pin = FLASH_CS_PIN_1 | FLASH_CS_PIN_2;
	GPIO_Init(GPIOE, &g);
	GPIO_SetBits(GPIOE, FLASH_CS_PIN_1 | FLASH_CS_PIN_2);
}

static void spi1_dma_init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

	DMA_InitTypeDef d;
	DMA_StructInit(&d);
	d.DMA_Channel = DMA_Channel_3;
	d.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
	d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	d.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	d.DMA_Mode = DMA_Mode_Normal;
	d.DMA_Priority = DMA_Priority_High;
	d.DMA_FIFOMode = DMA_FIFOMode_Disable;

	//TX: DMA2_Stream3_Ch3
	d.DMA_DIR = DMA_DIR_MemoryToPeripheral;
	d.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_Init(DMA2_Stream3, &d);

	//RX: DMA2_Stream0_Ch3
	d.DMA_DIR = DMA_DIR_PeripheralToMemory;
	d.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_Init(DMA2_Stream0, &d);
}

static void spi1_init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

	SPI_InitTypeDef s;
	SPI_StructInit(&s);
	s.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	s.SPI_Mode = SPI_Mode_Master;
	s.SPI_DataSize = SPI_DataSize_8b;
	s.SPI_CPOL = SPI_CPOL_Low;
	s.SPI_CPHA = SPI_CPHA_1Edge;
	s.SPI_NSS = SPI_NSS_Soft;
	s.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;	//PCLK2=84MHz, /4=21MHz, 满足 <=25MHz
	s.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_Init(SPI1, &s);

	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, ENABLE);
	SPI_Cmd(SPI1, ENABLE);
}

//DMA 发送 len 字节 (内存递增)
static void spi_dma_tx(const uint8_t *data, uint32_t len)
{
	DMA2_Stream3->CR &= ~DMA_SxCR_EN;
	while (DMA2_Stream3->CR & DMA_SxCR_EN);
	DMA2_Stream3->M0AR = (uint32_t)data;
	DMA2_Stream3->NDTR = len;
	DMA2_Stream3->CR |= DMA_SxCR_MINC;
	DMA_ClearFlag(DMA2_Stream3, DMA_FLAG_TCIF3);
	DMA_Cmd(DMA2_Stream3, ENABLE);
	while (DMA_GetFlagStatus(DMA2_Stream3, DMA_FLAG_TCIF3) == RESET);
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
}

//DMA 接收 len 字节 (同时用单字节0xFF重复发送提供时钟)
static void spi_dma_rx(uint8_t *buf, uint32_t len)
{
	//关键: SPI 全双工, 之前 TX 阶段(MISO 侧)收到的残留字节会留在 DR 里且 RXNE 置位。
	//若不清掉, RX DMA 会把该旧字节当成第 1 个数据, 导致整包错位 (JEDEC/数据全乱)。
	if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == SET)
	{
		(void)SPI1->DR;		//读 DR 清除 RXNE
		(void)SPI1->SR;		//读 SR 清除 OVR (读 DR 后再读 SR)
	}

	DMA2_Stream0->CR &= ~DMA_SxCR_EN;
	while (DMA2_Stream0->CR & DMA_SxCR_EN);
	DMA2_Stream0->M0AR = (uint32_t)buf;
	DMA2_Stream0->NDTR = len;
	DMA2_Stream0->CR |= DMA_SxCR_MINC;
	DMA_ClearFlag(DMA2_Stream0, DMA_FLAG_TCIF0);

	DMA2_Stream3->CR &= ~DMA_SxCR_EN;
	while (DMA2_Stream3->CR & DMA_SxCR_EN);
	DMA2_Stream3->M0AR = (uint32_t)&spi_dummy;
	DMA2_Stream3->NDTR = len;
	DMA2_Stream3->CR &= ~DMA_SxCR_MINC;	//单字节重复发送
	DMA_ClearFlag(DMA2_Stream3, DMA_FLAG_TCIF3);

	DMA_Cmd(DMA2_Stream0, ENABLE);		//先启动RX
	DMA_Cmd(DMA2_Stream3, ENABLE);		//再启动TX产生时钟
	while (DMA_GetFlagStatus(DMA2_Stream0, DMA_FLAG_TCIF0) == RESET);
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
}

static void flash_select(spi_flash_t dev)
{
	GPIO_ResetBits(GPIOE, (dev == SPI_FLASH_1) ? FLASH_CS_PIN_1 : FLASH_CS_PIN_2);
}

static void flash_deselect(spi_flash_t dev)
{
	GPIO_SetBits(GPIOE, (dev == SPI_FLASH_1) ? FLASH_CS_PIN_1 : FLASH_CS_PIN_2);
}

static void flash_write_enable(spi_flash_t dev)
{
	uint8_t cmd = FLASH_CMD_WRITE_ENABLE;
	flash_select(dev);
	spi_dma_tx(&cmd, 1);
	flash_deselect(dev);
}

static bool flash_wait_busy(spi_flash_t dev, uint32_t timeout_ms)
{
	uint8_t cmd = FLASH_CMD_READ_STATUS;
	uint8_t status = 0;
	uint32_t waited = 0;

	do
	{
		flash_select(dev);
		spi_dma_tx(&cmd, 1);
		spi_dma_rx(&status, 1);
		flash_deselect(dev);
		if ((status & FLASH_STATUS_WIP) == 0)
			return true;
		tim_delay_ms(1);	//必须按真实时间等待, 否则扇区擦除(45~400ms)会被误判为超时
		waited++;
	} while (waited < timeout_ms);

	return false;
}

bool spi_flash_init(void)
{
	uint8_t id[3];

	spi1_gpio_init();
	spi1_dma_init();
	spi1_init();

	for (uint32_t i = 0; i < SPI_FLASH_NUM; i++)
	{
		if (!spi_flash_read_id((spi_flash_t)i, id))
			return false;
		if (id[0] != 0xEF || id[1] != 0x40 || id[2] != 0x18)
		{
			printf("[FLASH] chip %u JEDEC: %02X %02X %02X\n", i, id[0], id[1], id[2]);
			return false;
		}
	}
	printf("[FLASH] W25Q128 x2 ready\n");
	return true;
}

bool spi_flash_read_id(spi_flash_t dev, uint8_t id[3])
{
	uint8_t cmd = FLASH_CMD_JEDEC_ID;

	flash_select(dev);
	spi_dma_tx(&cmd, 1);
	spi_dma_rx(id, 3);
	flash_deselect(dev);
	return true;
}

bool spi_flash_read(spi_flash_t dev, uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint8_t hdr[4] = { FLASH_CMD_READ_DATA, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	flash_select(dev);
	spi_dma_tx(hdr, 4);
	spi_dma_rx(buf, len);
	flash_deselect(dev);
	return true;
}

bool spi_flash_write(spi_flash_t dev, uint32_t addr, const uint8_t *buf, uint32_t len)
{
	while (len > 0)
	{
		uint32_t page_left = FLASH_PAGE_SIZE - (addr % FLASH_PAGE_SIZE);
		uint32_t chunk = (len < page_left) ? len : page_left;
		uint8_t hdr[4] = { FLASH_CMD_PAGE_PROGRAM, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

		flash_write_enable(dev);
		flash_select(dev);
		spi_dma_tx(hdr, 4);
		spi_dma_tx(buf, chunk);
		flash_deselect(dev);
		if (!flash_wait_busy(dev, 1000))
			return false;

		addr += chunk;
		buf += chunk;
		len -= chunk;
	}
	return true;
}

static bool flash_erase(spi_flash_t dev, uint32_t addr, uint8_t cmd)
{
	uint8_t hdr[4] = { cmd, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };

	flash_write_enable(dev);
	flash_select(dev);
	spi_dma_tx(hdr, 4);
	flash_deselect(dev);
	return flash_wait_busy(dev, 5000);
}

bool spi_flash_erase_sector(spi_flash_t dev, uint32_t addr)
{
	return flash_erase(dev, addr, FLASH_CMD_SECTOR_ERASE);
}

bool spi_flash_erase_block32(spi_flash_t dev, uint32_t addr)
{
	return flash_erase(dev, addr, FLASH_CMD_BLOCK_ERASE_32K);
}

bool spi_flash_erase_block64(spi_flash_t dev, uint32_t addr)
{
	return flash_erase(dev, addr, FLASH_CMD_BLOCK_ERASE_64K);
}

bool spi_flash_erase_chip(spi_flash_t dev)
{
	uint8_t cmd = FLASH_CMD_CHIP_ERASE;

	flash_write_enable(dev);
	flash_select(dev);
	spi_dma_tx(&cmd, 1);
	flash_deselect(dev);
	return flash_wait_busy(dev, 120000);	//全片擦除最长约100s
}
