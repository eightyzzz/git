#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ft6336.h"
#include "tim_delay.h"

//FT6336 电容触摸屏驱动
//
//SCL	-> PA8  (I2C3_SCL)
//SDA	-> PC9  (I2C3_SDA)
//INT	-> PE12 (EXTI12, 下降沿触发: 触摸时INT被拉低)
//RESET	-> PE15 (低电平复位; 板上没接该引脚时可以去掉复位操作)
//
//使用示例 (放在任务上下文里):
//	if (ft6336_irq_pending())
//	{
//		ft6336_touch_t touch;
//		if (ft6336_read_touch(&touch) && touch.count > 0)
//		{
//			ft6336_irq_clear();
//			// 处理 touch.x / touch.y
//		}
//	}

#define FT6336_I2C_ADDR_WRITE	0x70	//7位地址0x38左移一位
#define FT6336_I2C_ADDR_READ	0x71

#define FT6336_REG_DEV_MODE		0x00	//工作模式
#define FT6336_REG_TD_STATUS	0x02	//触摸状态: 低4位为当前触点数量
#define FT6336_REG_P1_XH		0x03	//触点1起始寄存器, 每个触点占6字节
#define FT6336_REG_CHIP_ID		0xA3	//芯片ID寄存器 (FT6x36系列)
#define FT6336_REG_GMODE		0xA4	//中断模式: 0=轮询, 1=中断触发
#define FT6336_REG_FIRMWARE_ID	0xA6	//固件版本
#define FT6336_REG_VENDOR_ID	0xA8	//厂商ID

#define FT6336_MAX_TOUCH		5		//最多支持5点触控

static volatile bool touch_irq_pending;	//中断标志, 在ISR里置位

static bool ft6336_write(uint8_t data[], uint32_t length);
static bool ft6336_read(uint8_t data[], uint32_t length);

static void ft6336_io_init(void)
{
	//SCL/SDA 复用为 I2C3
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_I2C3);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource9, GPIO_AF_I2C3);

	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_StructInit(&GPIO_InitStruct);
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;	//开漏+内部上拉, 兼容没有外部上拉的板子
	GPIO_InitStruct.GPIO_Speed = GPIO_Medium_Speed;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8;
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
	GPIO_Init(GPIOC, &GPIO_InitStruct);

	//INT 输入, 上拉
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_12;
	GPIO_Init(GPIOE, &GPIO_InitStruct);

	//RESET 输出 (低电平复位)
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStruct.GPIO_Speed = GPIO_High_Speed;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_15;
	GPIO_Init(GPIOE, &GPIO_InitStruct);
}

static void ft6336_i2c_init(void)
{
	I2C_InitTypeDef I2C_InitStruct;
	I2C_StructInit(&I2C_InitStruct);
	I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;
	I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_InitStruct.I2C_ClockSpeed = 100ul * 1000ul;		//100kHz
	I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
	I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
	I2C_InitStruct.I2C_OwnAddress1 = 0x00;
	I2C_Init(I2C3, &I2C_InitStruct);
}

static void ft6336_int_init(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOE, EXTI_PinSource12);

	EXTI_InitTypeDef EXTI_InitStructure;
	EXTI_StructInit(&EXTI_InitStructure);
	EXTI_InitStructure.EXTI_Line = EXTI_Line12;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	NVIC_InitTypeDef NVIC_InitStructure;
	memset(&NVIC_InitStructure, 0, sizeof(NVIC_InitStructure));
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
	NVIC_Init(&NVIC_InitStructure);
}

static void ft6336_reset(void)
{
	GPIO_ResetBits(GPIOE, GPIO_Pin_15);
	vTaskDelay(pdMS_TO_TICKS(10));
	GPIO_SetBits(GPIOE, GPIO_Pin_15);
	vTaskDelay(pdMS_TO_TICKS(50));
}

#define I2C_CHECK_EVENT(EVENT, TIMEOUT) \
	do { \
		uint32_t timeout = TIMEOUT; \
		while (!I2C_CheckEvent(I2C3, EVENT) && timeout > 0) { \
			tim_delay_us(10); \
			timeout -= 10; \
		} \
		if (timeout <= 0) \
			return false; \
	} while (0)

static bool ft6336_write(uint8_t data[], uint32_t length)
{
	I2C_AcknowledgeConfig(I2C3, ENABLE);
	I2C_GenerateSTART(I2C3, ENABLE);
	I2C_CHECK_EVENT(I2C_EVENT_MASTER_MODE_SELECT, 1000);
	I2C_Send7bitAddress(I2C3, FT6336_I2C_ADDR_WRITE, I2C_Direction_Transmitter);
	I2C_CHECK_EVENT(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, 1000);
	for (uint32_t i = 0; i < length; i++)
	{
		I2C_SendData(I2C3, data[i]);
		I2C_CHECK_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTING, 1000);
	}
	I2C_GenerateSTOP(I2C3, ENABLE);

	return true;
}

static bool ft6336_read(uint8_t data[], uint32_t length)
{
	I2C_AcknowledgeConfig(I2C3, ENABLE);
	I2C_GenerateSTART(I2C3, ENABLE);
	I2C_CHECK_EVENT(I2C_EVENT_MASTER_MODE_SELECT, 1000);
	I2C_Send7bitAddress(I2C3, FT6336_I2C_ADDR_READ, I2C_Direction_Receiver);
	I2C_CHECK_EVENT(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, 1000);
	for (uint32_t i = 0; i < length; i++)
	{
		if (i == length - 1)
			I2C_AcknowledgeConfig(I2C3, DISABLE);
		I2C_CHECK_EVENT(I2C_EVENT_MASTER_BYTE_RECEIVED, 1000);
		data[i] = I2C_ReceiveData(I2C3);
	}
	I2C_GenerateSTOP(I2C3, ENABLE);

	return true;
}

static bool ft6336_write_reg(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = { reg, value };
	return ft6336_write(data, 2);
}

static bool ft6336_read_regs(uint8_t reg, uint8_t data[], uint32_t length)
{
	if (!ft6336_write(&reg, 1))
		return false;
	return ft6336_read(data, length);
}

bool ft6336_init(void)
{
	ft6336_int_init();
	ft6336_i2c_init();
	ft6336_io_init();
	ft6336_reset();

	//复位后芯片需要约300ms才能就绪
	vTaskDelay(pdMS_TO_TICKS(300));

	//写入工作模式, 让芯片退出待机/监控状态 (与ESPHome等驱动初始化流程一致)
	ft6336_write_reg(FT6336_REG_DEV_MODE, 0x00);

	bool i2c_ok = false;
	for (uint32_t t = 0; t < 30; t++)
	{
		uint8_t chip_id = 0;
		if (ft6336_read_regs(FT6336_REG_CHIP_ID, &chip_id, 1))
		{
			printf("[FT6336] Chip ID: 0x%02X\n", chip_id);
			uint8_t vendor_id = 0;
			ft6336_read_regs(FT6336_REG_VENDOR_ID, &vendor_id, 1);
			printf("[FT6336] Vendor ID: 0x%02X\n", vendor_id);
			if (chip_id == 0)
				printf("[FT6336] warning: chip id is 0x00\n");
			i2c_ok = true;
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	if (!i2c_ok)
	{
		printf("[FT6336] init failed: I2C no ACK, check SCL/SDA wiring\n");
		return false;
	}

	//使能中断触发模式: 触摸时 INT 引脚拉低
	ft6336_write_reg(FT6336_REG_GMODE, 0x01);

	return true;
}

bool ft6336_read_touch(ft6336_touch_t *touch)
{
	uint8_t data[7];	//TD_STATUS + P1_XH/XL/YH/YL/WEIGHT/MISC

	if (touch == NULL)
		return false;

	if (!ft6336_read_regs(FT6336_REG_TD_STATUS, data, sizeof(data)))
		return false;

	touch->count = data[0] & 0x0F;
	if (touch->count == 0 || touch->count > FT6336_MAX_TOUCH)	//0xFF(无触摸)掩码后是15, 也会被过滤
	{
		touch->event = FT6336_EVENT_NO_EVENT;
		touch->x = 0;
		touch->y = 0;
		return true;
	}

	touch->event = (data[1] >> 6) & 0x03;
	touch->x = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
	touch->y = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];

	return true;
}

//INT 下降沿中断: 触摸时芯片拉低 INT, 读取 TD_STATUS 后自动释放
void EXTI15_10_IRQHandler(void)
{
	if (EXTI_GetITStatus(EXTI_Line12) == SET)
	{
		//I2C 通信不能放在中断里, 这里只置标志位, 由任务去读数据
		touch_irq_pending = true;
		EXTI_ClearITPendingBit(EXTI_Line12);
	}
}

bool ft6336_irq_pending(void)
{
	return touch_irq_pending;
}

void ft6336_irq_clear(void)
{
	touch_irq_pending = false;
}
