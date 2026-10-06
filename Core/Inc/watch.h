/*
 * watch.h  -  STM32F103C8T6 watch, public definitions
 *
 * Hardware used (pins are the same as the original project):
 *   OLED SSD1306 128x64  I2C1   : PB6 = SCL, PB7 = SDA   (address 0x78)
 *   key RIGHT                    : PB11   active low, internal pull-up
 *   key ENTER                    : PB1    active low, internal pull-up
 *   key LEFT                     : PB10   active low, internal pull-up
 *   key EXIT                     : PB0    active low, internal pull-up
 *   DHT11 data                   : PA1    open drain + pull-up
 *   status LED                   : PC13   active low  (LOW = lit)
 *   passive buzzer               : PA8    TIM1_CH1 PWM
 *   debug UART                   : PA9/PA10 (USART1, not used by the app)
 *   HSE crystal                  : PD0/PD1 (8 MHz)
 */
#ifndef WATCH_H
#define WATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

/*----------------------------------------------------------------------------
 * Peripheral handles (defined in main.c)
 *--------------------------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim1;
extern ADC_HandleTypeDef hadc1;

/*----------------------------------------------------------------------------
 * u8g2 hardware callbacks (defined in bsp.c / u8g2_d_setup.c / u8x8_byte.c)
 *--------------------------------------------------------------------------*/
struct u8x8_struct;
uint8_t u8x8_byte_hw_i2c(struct u8x8_struct *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8g2_stm32_delay(struct u8x8_struct *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

/*----------------------------------------------------------------------------
 * Pins
 *--------------------------------------------------------------------------*/
#define WATCH_KEY_PORT          GPIOB
#define WATCH_KEY_RIGHT_PIN     GPIO_PIN_11
#define WATCH_KEY_ENTER_PIN     GPIO_PIN_1
#define WATCH_KEY_LEFT_PIN      GPIO_PIN_10
#define WATCH_KEY_EXIT_PIN      GPIO_PIN_0
#define WATCH_KEY_IRQn          EXTI15_10_IRQn     /* PB0..PB15 share these   */

#define WATCH_LED_PORT          GPIOC
#define WATCH_LED_PIN           GPIO_PIN_13

#define WATCH_DHT11_PORT        GPIOA
#define WATCH_DHT11_PIN         GPIO_PIN_1

#define WATCH_BUZZER_PORT       GPIOA
#define WATCH_BUZZER_PIN        GPIO_PIN_8

/* key codes pushed into the key queue.
   K1 = PB0 = UP, K2 = PB1 = DOWN, K3 = PB10 = ENTER, K4 = PB11 = EXIT */
typedef enum
{
	KEY_NONE  = 0,
	KEY_UP    = 1,
	KEY_DOWN  = 2,
	KEY_ENTER = 3,
	KEY_EXIT  = 4
} key_code_t;

/*----------------------------------------------------------------------------
 * delay.c  (DWT cycle counter based, works before and after the scheduler)
 *--------------------------------------------------------------------------*/
void     Delay_Init(void);
void     Delay_Us(uint32_t us);
void     Delay_Ms(uint32_t ms);
uint32_t Delay_Ms_Get(void);          /* free running millisecond counter */

/*----------------------------------------------------------------------------
 * bsp.c
 *--------------------------------------------------------------------------*/
void Bsp_Clock_Init(void);            /* 8 MHz HSE -> PLL x9 -> 72 MHz       */
void Bsp_Gpio_Init(void);
void Bsp_I2c_Init(void);              /* I2C1 @ 400 kHz, PB6/PB7             */
void Bsp_Buzzer_Init(void);           /* TIM1_CH1 PWM on PA8                 */
void Bsp_Buzzer_Beep(uint16_t freq_hz, uint16_t time_ms);
void Bsp_Buzzer_Poll(void);           /* call regularly to stop the beep    */
void PassiveBuzzer_Silence(void);     /* stop immediately                   */
/*----------------------------------------------------------------------------
 * oled.c
 *--------------------------------------------------------------------------*/
void    Oled_Init(void);
uint8_t Oled_Flush(void);      /* 1 = frame sent OK, 0 = I2C problem */
uint8_t Oled_Recover(void);    /* reset a stuck bus + re-init OLED   */
extern volatile uint32_t g_i2cErrorCount;
extern volatile uint8_t  g_i2cLastStatus;
void Bsp_Led_Write(uint8_t on);
void Bsp_Led_Toggle(void);

/*----------------------------------------------------------------------------
 * app.c  (the tasks)
 *--------------------------------------------------------------------------*/
void App_Init(void);                  /* creates the FreeRTOS objects        */
void App_UiTask(void *argument);
void App_KeyTask(void *argument);

extern volatile uint32_t g_bootStage; /* boot progress, inspect with a debugger */

#ifdef __cplusplus
}
#endif

#endif /* WATCH_H */
