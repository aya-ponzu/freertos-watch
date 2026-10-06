/*
 * stm32f1xx_it.c  -  interrupt handlers
 *
 * Only three interrupts are used in this project:
 *   SysTick : FreeRTOS tick
 *   SVC/PendSV : FreeRTOS context switch (mapped in FreeRTOSConfig.h)
 *   TIM4    : HAL 1 ms time base for the blocking I2C transfers
 * The keys are polled, so there is no EXTI handler at all.
 */
#include "main.h"
#include "watch.h"

/* ---- Cortex-M3 core exceptions ------------------------------------------*/
void NMI_Handler(void)
{
	for (;;) { }
}

void HardFault_Handler(void)
{
	/* A hard fault is a real bug: blink the LED as fast as possible and store
	   the fault status so it can be read with a debugger. */
	volatile uint32_t *cfsr = &SCB->CFSR;
	volatile uint32_t dummy = *cfsr;
	(void)dummy;

	for (;;)
	{
		Bsp_Led_Toggle();
		for (volatile uint32_t i = 0; i < 200000U; i++) { }
	}
}

void MemManage_Handler(void)   { for (;;) { } }
void BusFault_Handler(void)    { for (;;) { } }
void UsageFault_Handler(void)  { for (;;) { } }
void DebugMon_Handler(void)    { }

/* ---- peripheral interrupts ----------------------------------------------*/
extern TIM_HandleTypeDef htim4;

void TIM4_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim4);
}
