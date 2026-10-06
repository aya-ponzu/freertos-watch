/*
 * main.c  -  STM32F103C8T6 + FreeRTOS + u8g2 watch
 *
 * Boot sequence is deliberately linear and every step is recorded in
 * g_bootStage so a hang can be located with a debugger:
 *   0x01 HAL_Init done
 *   0x02 system clock done       (falls back to HSI if the crystal fails)
 *   0x03 GPIO done
 *   0x04 I2C1 done
 *   0x05 buzzer done
 *   0x06 RTOS objects created
 *   0x07 scheduler started
 *   0x20 display initialised inside the UI task
 */
#include "main.h"
#include "watch.h"
#include "FreeRTOS.h"
#include "task.h"

/* defined in app.c, kept here so it starts at 0 before any C code runs */
extern volatile uint32_t g_bootStage;

int main(void)
{
	HAL_Init();
	g_bootStage = 0x01;

	Bsp_Clock_Init();
	g_bootStage = 0x02;

	Delay_Init();

	Bsp_Gpio_Init();
	g_bootStage = 0x03;

	Bsp_I2c_Init();
	g_bootStage = 0x04;

	Bsp_Buzzer_Init();
	g_bootStage = 0x05;

	App_Init();
	g_bootStage = 0x06;

	g_bootStage = 0x07;
	vTaskStartScheduler();

	/*如果到了这里说明调度器没能启动（静态分配下几乎不可能）*/
	Bsp_Led_Write(1);
	for (;;)
	{
	}
}

void Error_Handler(void)
{
	__disable_irq();
	for (;;)
	{
	}
}
