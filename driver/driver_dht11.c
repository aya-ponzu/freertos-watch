/*
 * driver_dht11.c  -  DHT11 temperature / humidity sensor
 *
 * Wiring: DATA on PA1, open drain output with the internal pull-up, so
 *         writing 1 releases the line and it can be read back at any time.
 *
 * The bit timing needs microsecond resolution.  It is provided by delay.c
 * (DWT cycle counter), NOT by the HAL tick, so a read works no matter what
 * the scheduler is doing.
 *
 * DHT11_Read() returns 0 on success and -1 on any error; there is no endless
 * wait anywhere: every loop has a timeout counter.
 */
#include "watch.h"
#include "driver_dht11.h"

/*----------------------------------------------------------------------------
 * Low level pin helpers
 *--------------------------------------------------------------------------*/
static void DHT11_LineRelease(void)
{
	HAL_GPIO_WritePin(WATCH_DHT11_PORT, WATCH_DHT11_PIN, GPIO_PIN_SET);
}

static void DHT11_LinePullLow(void)
{
	HAL_GPIO_WritePin(WATCH_DHT11_PORT, WATCH_DHT11_PIN, GPIO_PIN_RESET);
}

static uint8_t DHT11_LineRead(void)
{
	return (HAL_GPIO_ReadPin(WATCH_DHT11_PORT, WATCH_DHT11_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

/* wait until the line reaches the requested level, with a us timeout */
static int DHT11_WaitLevel(uint8_t level, uint32_t timeout_us)
{
	while (timeout_us-- != 0U)
	{
		if (DHT11_LineRead() == level) return 0;
		Delay_Us(1);
	}
	return -1;
}

/*----------------------------------------------------------------------------
 * Protocol
 *--------------------------------------------------------------------------*/
void DHT11_Init(void)
{
	/* output open drain + pull-up is already configured in Bsp_Gpio_Init() */
	DHT11_LineRelease();
	Delay_Ms(20);              /* the sensor needs > 1 s after power on */
}

static void DHT11_StartSignal(void)
{
	DHT11_LinePullLow();
	Delay_Ms(20);              /* the data sheet asks for at least 18 ms */
	DHT11_LineRelease();
	Delay_Us(30);              /* 20..40 us, then the sensor answers */
}

static int DHT11_WaitAck(void)
{
	if (DHT11_WaitLevel(0, 200) != 0) return -1;   /* sensor pulls low 80 us */
	if (DHT11_WaitLevel(1, 200) != 0) return -1;   /* then high 80 us        */
	if (DHT11_WaitLevel(0, 200) != 0) return -1;   /* first data bit starts  */
	return 0;
}

static int DHT11_ReadByte(uint8_t *out)
{
	uint8_t i, data = 0;

	for (i = 0; i < 8U; i++)
	{
		/* each bit: 50 us low, then 26..28 us high = 0, 70 us high = 1 */
		if (DHT11_WaitLevel(1, 200) != 0) return -1;   /* wait for the high    */
		Delay_Us(40);                                  /* sample in the middle */
		data <<= 1;
		if (DHT11_LineRead() != 0U) data |= 1U;
		if (DHT11_WaitLevel(0, 200) != 0) return -1;   /* wait for the next    */
	}

	*out = data;
	return 0;
}

int DHT11_Read(int *hum, int *temp)
{
	uint8_t hum_i, hum_d, temp_i, temp_d, check;

	DHT11_StartSignal();

	if (DHT11_WaitAck() != 0) return -1;

	if (DHT11_ReadByte(&hum_i)  != 0) return -1;
	if (DHT11_ReadByte(&hum_d)  != 0) return -1;
	if (DHT11_ReadByte(&temp_i) != 0) return -1;
	if (DHT11_ReadByte(&temp_d) != 0) return -1;
	if (DHT11_ReadByte(&check)  != 0) return -1;

	if ((uint8_t)(hum_i + hum_d + temp_i + temp_d) != check) return -1;

	if (hum  != NULL) *hum  = (int)hum_i;
	if (temp != NULL) *temp = (int)temp_i;

	return 0;
}
