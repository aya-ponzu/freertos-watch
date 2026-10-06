/*
 * delay.c  -  microsecond / millisecond delays and a millisecond counter
 *
 * Everything is derived from the Cortex-M3 DWT cycle counter (72 MHz).  This
 * is independent of the HAL tick, of SysTick and of interrupts, so it also
 * works inside an interrupt handler and before the scheduler is started -
 * exactly what the DHT11 bit banging needs.
 *
 * The 32 bit cycle counter wraps every ~59.6 s at 72 MHz.  The wrap is caught
 * by comparing with the previous value and folded into a 64 bit microsecond
 * counter, so Delay_Ms_Get() is a real, monotonic millisecond time base.
 */
#include "watch.h"

#define DELAY_CYCLES_PER_US   72U
#define DELAY_WRAP_US         59652323UL   /* 2^32 / 72, in whole us */

static volatile uint64_t s_usHigh  = 0;    /* accumulated full wraps in us */
static volatile uint32_t s_lastCyc = 0;

void Delay_Init(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

	s_usHigh  = 0;
	s_lastCyc = 0;
}

/* fold a possible cycle counter wrap into the 64 bit counter */
static void Delay_Update(void)
{
	uint32_t now = DWT->CYCCNT;

	if (now < s_lastCyc)                     /* the counter wrapped */
	{
		s_usHigh += (uint64_t)DELAY_WRAP_US;
	}
	s_lastCyc = now;
}

void Delay_Us(uint32_t us)
{
	uint32_t start;
	uint32_t target;

	if (us == 0U) return;
	if (us > 1000000U) us = 1000000U;        /* never wait more than a second */

	start  = DWT->CYCCNT;
	target = us * DELAY_CYCLES_PER_US;

	/* unsigned subtraction: correct across the 32 bit counter wrap */
	while ((DWT->CYCCNT - start) < target)
	{
		/* nothing to do */
	}
}

void Delay_Ms(uint32_t ms)
{
	while (ms-- != 0U)
	{
		Delay_Us(1000U);
	}
}

uint32_t Delay_Ms_Get(void)
{
	uint64_t us;

	Delay_Update();

	us = s_usHigh + (uint64_t)(DWT->CYCCNT / DELAY_CYCLES_PER_US);
	return (uint32_t)(us / 1000U);
}
