/*
 * oled.c  -  SSD1306 over I2C1 for u8g2
 *
 * This file owns everything that can go wrong on the I2C bus:
 *   - the number of failed transfers is counted (see u8x8_byte_hw_i2c())
 *   - Oled_Flush() reports whether the frame really went out
 *   - Oled_Recover() resets a stuck bus and re-initialises the controller
 *
 * A hung I2C bus is the classic reason for an OLED that "freezes": the
 * display keeps its last content while the MCU keeps running.  With the
 * counter below the application notices it and recovers automatically.
 */
#include "watch.h"
#include "u8g2.h"

extern u8g2_t u8g2;                 /* the single display object (app.c) */

volatile uint32_t g_i2cErrorCount = 0;   /* incremented by u8x8_byte_hw_i2c */
volatile uint8_t  g_i2cLastStatus = 1;   /* HAL_OK of the last transfer      */

void Oled_Init(void)
{
	u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
	                                      u8x8_byte_hw_i2c, u8g2_stm32_delay);
	u8g2_InitDisplay(&u8g2);
	u8g2_SetPowerSave(&u8g2, 0);
	u8g2_ClearBuffer(&u8g2);
	u8g2_SendBuffer(&u8g2);
}

/* returns 1 when the frame was transmitted without an I2C error */
uint8_t Oled_Flush(void)
{
	uint32_t before = g_i2cErrorCount;

	u8g2_SendBuffer(&u8g2);

	if (g_i2cErrorCount != before) return 0U;
	if (hi2c1.ErrorCode != HAL_I2C_ERROR_NONE) return 0U;
	return 1U;
}

uint8_t Oled_Recover(void)
{
	GPIO_InitTypeDef io = {0};
	uint8_t i;

	(void)HAL_I2C_DeInit(&hi2c1);

	/* clock the bus by hand with SDA released, then send a STOP */
	io.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
	io.Mode  = GPIO_MODE_OUTPUT_OD;
	io.Pull  = GPIO_PULLUP;
	io.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOB, &io);

	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
	for (i = 0; i < 9U; i++)
	{
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
		Delay_Us(5);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
		Delay_Us(5);
	}
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
	Delay_Us(5);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
	Delay_Us(5);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
	Delay_Us(5);

	/* hand the pins back to the peripheral and re-init it */
	io.Mode = GPIO_MODE_AF_OD;
	HAL_GPIO_Init(GPIOB, &io);

	hi2c1.ErrorCode = HAL_I2C_ERROR_NONE;
	__HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);

	if (HAL_I2C_Init(&hi2c1) != HAL_OK) return 0U;

	/* the controller may have lost its configuration, set it up again */
	u8g2_InitDisplay(&u8g2);
	u8g2_SetPowerSave(&u8g2, 0);

	g_i2cErrorCount = 0;
	g_i2cLastStatus = 1;

	u8g2_ClearBuffer(&u8g2);
	u8g2_SendBuffer(&u8g2);
	return 1U;
}
