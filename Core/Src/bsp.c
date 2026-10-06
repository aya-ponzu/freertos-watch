/*
 * bsp.c  -  board support: clock, GPIO, I2C, buzzer, LED
 *
 * Replaces gpio.c / i2c.c / tim.c / the CubeMX clock code with one small file
 * whose only job is to bring the hardware up without ever being able to
 * block.  There is no HAL_I2C state machine involved in the display path any
 * more (u8g2 uses the HAL blocking transfer which always has a timeout).
 */
#include "watch.h"
#include "stm32f1xx_hal.h"
#include "u8g2.h"

/* the single display object lives in app.c and is declared in watch.h */
extern u8g2_t u8g2;

/*----------------------------------------------------------------------------
 * Peripheral handles owned by this file (declared extern in watch.h)
 *--------------------------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim1;
ADC_HandleTypeDef hadc1;

/*============================================================================
 * System clock: 8 MHz HSE -> PLL x9 -> 72 MHz
 *==========================================================================*/
void Bsp_Clock_Init(void)
{
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clk = {0};

	__HAL_RCC_AFIO_CLK_ENABLE();
	__HAL_RCC_PWR_CLK_ENABLE();

	osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
	osc.HSEState            = RCC_HSE_ON;
	osc.HSEPredivValue      = RCC_HSE_PREDIV_DIV1;
	osc.HSIState            = RCC_HSI_ON;
	osc.PLL.PLLState        = RCC_PLL_ON;
	osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
	osc.PLL.PLLMUL          = RCC_PLL_MUL9;

	/* If the crystal does not start we MUST NOT hang: fall back to HSI so the
	   board still runs (the clock display will be a little slow, but the
	   firmware is alive and the reason is visible on the LED). */
	if (HAL_RCC_OscConfig(&osc) != HAL_OK)
	{
		osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
		osc.HSEState       = RCC_HSE_OFF;
		osc.HSIState       = RCC_HSI_ON;
		osc.PLL.PLLState   = RCC_PLL_NONE;
		(void)HAL_RCC_OscConfig(&osc);
	}

	clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
	                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
	clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
	clk.APB1CLKDivider = RCC_HCLK_DIV2;
	clk.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
	{
		/* last resort: run directly from HSI, no PLL */
		clk.SYSCLKSource  = RCC_SYSCLKSOURCE_HSI;
		clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
		(void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
	}
}

/*============================================================================
 * GPIO
 *==========================================================================*/
void Bsp_Gpio_Init(void)
{
	GPIO_InitTypeDef io = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_AFIO_CLK_ENABLE();

	/* status LED, start with the LED off (active low) */
	HAL_GPIO_WritePin(WATCH_LED_PORT, WATCH_LED_PIN, GPIO_PIN_SET);
	io.Pin   = WATCH_LED_PIN;
	io.Mode  = GPIO_MODE_OUTPUT_PP;
	io.Pull  = GPIO_NOPULL;
	io.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(WATCH_LED_PORT, &io);

	/* 4 keys, active low with the internal pull-up - polled by App_KeyTask,
	   NO interrupt is used, so a bouncing or stuck key can never trap the
	   CPU inside an interrupt handler. */
	io.Pin  = WATCH_KEY_RIGHT_PIN | WATCH_KEY_ENTER_PIN |
	          WATCH_KEY_LEFT_PIN  | WATCH_KEY_EXIT_PIN;
	io.Mode = GPIO_MODE_INPUT;
	io.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(WATCH_KEY_PORT, &io);

	/* DHT11 data line: open drain with pull-up, so writing 1 releases it */
	HAL_GPIO_WritePin(WATCH_DHT11_PORT, WATCH_DHT11_PIN, GPIO_PIN_SET);
	io.Pin   = WATCH_DHT11_PIN;
	io.Mode  = GPIO_MODE_OUTPUT_OD;
	io.Pull  = GPIO_PULLUP;
	io.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(WATCH_DHT11_PORT, &io);
}

/*============================================================================
 * I2C1 for the SSD1306 (PB6 = SCL, PB7 = SDA) at 400 kHz
 *==========================================================================*/
void Bsp_I2c_Init(void)
{
	GPIO_InitTypeDef io = {0};

	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* --- bus recovery before enabling the peripheral -----------------------
	   If the display was reset while it was driving SDA low, the line stays
	   low and every transfer fails with BUSY.  Clocking SCL by hand releases
	   it.  (HAL_I2C_Init() checks for a stuck bus *before* enabling the
	   peripheral, so the recovery has to happen first.)                    */
	{
		uint8_t i;

		io.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
		io.Mode  = GPIO_MODE_OUTPUT_OD;
		io.Pull  = GPIO_PULLUP;
		io.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(GPIOB, &io);

		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
		for (i = 0; i < 18U; i++)
		{
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
			Delay_Us(5);
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
			Delay_Us(5);
		}
		/* a STOP condition: SDA low while SCL is high, then SDA high */
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
		Delay_Us(5);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
		Delay_Us(5);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
		Delay_Us(5);
	}

	/* now hand the pins over to the I2C peripheral */
	io.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
	io.Mode  = GPIO_MODE_AF_OD;
	io.Pull  = GPIO_NOPULL;
	io.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOB, &io);

	__HAL_RCC_I2C1_CLK_ENABLE();
	__HAL_RCC_I2C1_FORCE_RESET();
	__HAL_RCC_I2C1_RELEASE_RESET();

	hi2c1.Instance             = I2C1;
	hi2c1.Init.ClockSpeed      = 400000;
	hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
	hi2c1.Init.OwnAddress1     = 0;
	hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2     = 0;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
	(void)HAL_I2C_Init(&hi2c1);
}

/*============================================================================
 * Buzzer: TIM1_CH1 PWM on PA8 (passive buzzer)
 *==========================================================================*/
void Bsp_Buzzer_Init(void)
{
	GPIO_InitTypeDef    io  = {0};
	TIM_OC_InitTypeDef  oc  = {0};
	TIM_ClockConfigTypeDef clkcfg = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_TIM1_CLK_ENABLE();

	io.Pin   = WATCH_BUZZER_PIN;
	io.Mode  = GPIO_MODE_AF_PP;
	io.Pull  = GPIO_NOPULL;
	io.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(WATCH_BUZZER_PORT, &io);

	htim1.Instance               = TIM1;
	htim1.Init.Prescaler         = 71;          /* 72 MHz / 72 = 1 MHz        */
	htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
	htim1.Init.Period            = 999;         /* 1 kHz                      */
	htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim1.Init.RepetitionCounter = 0;
	htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_Base_Init(&htim1) != HAL_OK) return;

	clkcfg.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	(void)HAL_TIM_ConfigClockSource(&htim1, &clkcfg);

	oc.OCMode     = TIM_OCMODE_PWM1;
	oc.Pulse      = 500;                        /* 50 % duty                  */
	oc.OCPolarity = TIM_OCPOLARITY_HIGH;
	oc.OCFastMode = TIM_OCFAST_DISABLE;
	if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK) return;

	(void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
	PassiveBuzzer_Silence();
}

/* --------------------------------------------------------------------------
 * The buzzer needs a "play this frequency for N ms" service.  It is driven
 * from the UI task, not from an interrupt, and it never blocks: the note is
 * simply stopped again after the requested time has elapsed.
 * ------------------------------------------------------------------------*/
static uint16_t s_beepFreq    = 0;
static uint32_t s_beepStartMs = 0;
static uint16_t s_beepTimeMs  = 0;

void PassiveBuzzer_Silence(void)
{
	TIM_CCxChannelCmd(TIM1, TIM_CHANNEL_1, TIM_CCx_DISABLE);
	s_beepFreq   = 0;
	s_beepTimeMs = 0;
}

void Bsp_Buzzer_Beep(uint16_t freq_hz, uint16_t time_ms)
{
	if (freq_hz == 0U) freq_hz = 1000U;
	if (freq_hz < 100U) freq_hz = 100U;
	if (freq_hz > 20000U) freq_hz = 20000U;

	/* 1 MHz counter clock -> period in us */
	__HAL_TIM_SET_AUTORELOAD(&htim1, (1000000U / freq_hz) - 1U);
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (1000000U / freq_hz) / 2U);

	s_beepFreq    = freq_hz;
	s_beepTimeMs  = time_ms;
	s_beepStartMs = Delay_Ms_Get();

	TIM_CCxChannelCmd(TIM1, TIM_CHANNEL_1, TIM_CCx_ENABLE);
}

/* called from the UI task every loop, no interrupt involved */
void Bsp_Buzzer_Poll(void)
{
	if (s_beepFreq == 0U) return;
	if (s_beepTimeMs == 0U) { PassiveBuzzer_Silence(); return; }

	if ((Delay_Ms_Get() - s_beepStartMs) >= s_beepTimeMs)
	{
		PassiveBuzzer_Silence();
	}
}

/*============================================================================
 * LED helpers
 *==========================================================================*/
void Bsp_Led_Write(uint8_t on)
{
	HAL_GPIO_WritePin(WATCH_LED_PORT, WATCH_LED_PIN,
	                  (on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void Bsp_Led_Toggle(void)
{
	HAL_GPIO_TogglePin(WATCH_LED_PORT, WATCH_LED_PIN);
}

