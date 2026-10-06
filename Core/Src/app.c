/*
 * app.c  -  the watch application
 *
 * Two tasks:
 *   App_UiTask  : owns the OLED, draws the screens, drives the 1 Hz clock
 *   App_KeyTask : polls the 4 keys with a real debounce state machine
 *
 * Everything is statically allocated - no RTOS heap is used.
 *
 * KEY MAP (K1..K4 = PB0, PB1, PB10, PB11)
 *   K1 = UP     : move the selection up / increase a value
 *   K2 = DOWN   : move the selection down / decrease a value
 *   K3 = ENTER  : open the selected entry / confirm
 *   K4 = EXIT   : go back one level
 *
 * SCREENS
 *   TIME    : clock + date, K3 opens the menu
 *   MENU    : Stopwatch / Clock+date / DHT11 / Battery / About
 *   SETTIME : set year-month-day and hour:minute:second
 *   STOPWATCH / DHT11 / BATTERY / ABOUT
 */
#include "watch.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "u8g2.h"
#include "driver_dht11.h"
#include <stdio.h>

/*============================================================================
 * Shared hardware objects
 *==========================================================================*/
u8g2_t u8g2;                       /* the single display object + its buffer */

/*============================================================================
 * FreeRTOS static objects
 *==========================================================================*/
#define UI_TASK_STACK_WORDS    448
#define KEY_TASK_STACK_WORDS   160

static StaticTask_t s_uiTcb;
static StackType_t  s_uiStack[UI_TASK_STACK_WORDS];
static StaticTask_t s_keyTcb;
static StackType_t  s_keyStack[KEY_TASK_STACK_WORDS];

static StaticTask_t s_idleTcb;
static StackType_t  s_idleStack[configMINIMAL_STACK_SIZE];
static StaticTask_t s_timerTcb;
static StackType_t  s_timerStack[configTIMER_TASK_STACK_DEPTH];

#define KEY_QUEUE_LEN   8
static StaticQueue_t s_keyQueueStruct;
static uint8_t       s_keyQueueStorage[KEY_QUEUE_LEN * sizeof(uint8_t)];
static QueueHandle_t s_keyQueue = NULL;

static StaticTimer_t s_clockTimerStruct;
static TimerHandle_t s_clockTimer = NULL;

static StaticTimer_t s_swTimerStruct;
static TimerHandle_t s_swTimer = NULL;

static TaskHandle_t  s_uiTask  = NULL;
static TaskHandle_t  s_keyTask = NULL;

/* boot progress, readable with a debugger (see watch.h) */
volatile uint32_t g_bootStage = 0;

/* Diagnostic counters - read them with the debugger:
     g_bootStage      : 0x20 = the UI task is running
     g_uiLoopCount    : increments every UI loop (~20 per second)
     g_i2cErrorCount  : failed I2C transfers since boot
     g_keyTaskRuns    : increments every 10 ms while the key task is alive
     g_keyTaskPress   : number of accepted key presses since boot
     g_keyTaskLevel   : bit0..3 = raw level of K1..K4, bit set = released
   If the LED blinks but a key does nothing, watch g_keyTaskPress and
   g_keyTaskLevel while pressing it: no change -> the pin is not moving. */
volatile uint32_t g_uiLoopCount   = 0;

/*============================================================================
 * Required by FreeRTOS when configSUPPORT_STATIC_ALLOCATION == 1
 *==========================================================================*/
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
	*ppxIdleTaskTCBBuffer   = &s_idleTcb;
	*ppxIdleTaskStackBuffer = s_idleStack;
	*pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
	*ppxTimerTaskTCBBuffer   = &s_timerTcb;
	*ppxTimerTaskStackBuffer = s_timerStack;
	*pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	(void)xTask;
	(void)pcTaskName;
	taskDISABLE_INTERRUPTS();
	for (;;)
	{
	}
}

/*============================================================================
 * Date / time
 *==========================================================================*/
static volatile uint16_t s_year  = 2024;
static volatile uint8_t  s_month = 1;
static volatile uint8_t  s_day   = 1;
static volatile uint8_t  s_hour  = 0;
static volatile uint8_t  s_min   = 0;
static volatile uint8_t  s_sec   = 0;
static volatile uint8_t  s_tenth = 0;      /* 1/10 s, drives the little bar */

static const uint8_t s_daysInMonth[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static uint8_t DaysInMonth(uint16_t year, uint8_t month)
{
	uint8_t d = s_daysInMonth[(month <= 12U) ? month : 1U];

	if ((month == 2U) && (((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U))))
	{
		d = 29U;
	}
	return d;
}

static void ClockTick(void)
{
	s_tenth++;
	if (s_tenth > 9U)
	{
		s_tenth = 0U;
		if (++s_sec >= 60U)
		{
			s_sec = 0U;
			if (++s_min >= 60U)
			{
				s_min = 0U;
				if (++s_hour >= 24U)
				{
					s_hour = 0U;
					if (++s_day > DaysInMonth(s_year, s_month))
					{
						s_day = 1U;
						if (++s_month > 12U)
						{
							s_month = 1U;
							s_year++;
						}
					}
				}
			}
		}
	}
}

static void ClockTimerCallback(TimerHandle_t xTimer)
{
	(void)xTimer;
	ClockTick();
}

/* a little beep, never blocking */
static void Ui_Beep(void)
{
	Bsp_Buzzer_Beep(2500, 60);
}

/* key diagnostics, defined in App_KeyTask() below but read while drawing */
volatile uint8_t g_keyIdleLevel = 0;   /* set by App_KeyTask() at start-up */

/* battery state, filled in by Batt_Update() (defined further down) */
static uint16_t s_battMv = 0;
static uint8_t  s_battPercent = 0;
static void DrawBatteryIcon(u8g2_uint_t bx, u8g2_uint_t by, uint8_t percent);

/*============================================================================
 * Screens
 *==========================================================================*/
typedef enum
{
	SCREEN_TIME = 0,
	SCREEN_MENU,
	SCREEN_SETTIME,
	SCREEN_STOPWATCH,
	SCREEN_DHT11,
	SCREEN_BATT,
	SCREEN_ABOUT
} screen_t;

#define MENU_ITEMS   6
#define SET_FIELDS   6

static screen_t s_screen   = SCREEN_TIME;
static int8_t   s_menuSel  = 0;
static uint8_t  s_setField = 0;         /* 0..5 = y, m, d, h, min, s       */
static uint8_t  s_setEdit  = 0;         /* 0 = pick a field, 1 = change it */

static const char *const s_menuName[MENU_ITEMS] =
{
	"Stopwatch", "Clock / date", "DHT11", "Battery", "About"
};

/*============================================================================
 * drawing helpers
 *==========================================================================*/
static void DrawTitle(const char *title)
{
	u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
	u8g2_DrawStr(&u8g2, 0, 10, title);
	u8g2_DrawHLine(&u8g2, 0, 12, 128);
}

/* --- clock ----------------------------------------------------------------*/
static void DrawTimeScreen(void)
{
	char buf[24];
	uint8_t tenth = s_tenth;

	u8g2_SetFont(&u8g2, u8g2_font_logisoso28_tn);
	snprintf(buf, sizeof(buf), "%02u", (unsigned)s_hour);
	u8g2_DrawStr(&u8g2, 6, 36, buf);
	snprintf(buf, sizeof(buf), "%02u", (unsigned)s_min);
	u8g2_DrawStr(&u8g2, 74, 36, buf);

	u8g2_DrawBox(&u8g2, 58, 18, 6, 6);       /* the colon, as rectangles */
	u8g2_DrawBox(&u8g2, 58, 30, 6, 6);

	/* seconds bar - always moving, so the display is obviously alive */
	u8g2_DrawFrame(&u8g2, 4, 42, 120, 11);
	{
		uint8_t w = (uint8_t)(((uint16_t)(s_sec % 60U) * 116U) / 59U);
		u8g2_DrawBox(&u8g2, 6, 44, w, 7);
		u8g2_DrawBox(&u8g2, (uint8_t)(6U + ((uint16_t)tenth * 116U) / 9U), 44, 2, 7);
	}

	u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
	snprintf(buf, sizeof(buf), "%04u-%02u-%02u", (unsigned)s_year, (unsigned)s_month, (unsigned)s_day);
	u8g2_DrawStr(&u8g2, 4, 63, buf);

	/* battery gauge in the bottom right corner */
	DrawBatteryIcon(88, 53, s_battPercent);
}

/* --- menu -----------------------------------------------------------------*/
static void DrawMenuScreen(void)
{
	uint8_t i, y = 25;

	DrawTitle("MENU");

	for (i = 0; i < MENU_ITEMS; i++)
	{
		if (i == (uint8_t)s_menuSel)
		{
			u8g2_DrawBox(&u8g2, 0, (uint8_t)(y - 9), 128, 12);
			u8g2_SetDrawColor(&u8g2, 0);
			u8g2_DrawStr(&u8g2, 4, y, s_menuName[i]);
			u8g2_SetDrawColor(&u8g2, 1);
		}
		else
		{
			u8g2_DrawStr(&u8g2, 4, y, s_menuName[i]);
		}
		y = (uint8_t)(y + 9);
	}
}

/* --- set date / time ------------------------------------------------------*/
static void DrawSetTimeScreen(void)
{
	char buf[24];
	uint8_t i;
	uint16_t values[SET_FIELDS];
	static const char *const names[SET_FIELDS] = { "Year", "Month", "Day", "Hour", "Min", "Sec" };

	values[0] = s_year; values[1] = s_month; values[2] = s_day;
	values[3] = s_hour; values[4] = s_min;   values[5] = s_sec;

	DrawTitle("SET DATE / TIME");

	for (i = 0; i < SET_FIELDS; i++)
	{
		uint8_t col = (uint8_t)(i / 3U);
		uint8_t row = (uint8_t)(i % 3U);
		uint8_t x   = (uint8_t)(col * 64U);
		uint8_t y   = (uint8_t)(28U + row * 13U);

		u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
		u8g2_DrawStr(&u8g2, x, y, names[i]);
		snprintf(buf, sizeof(buf), (i == 0) ? "%04u" : "%02u", (unsigned)values[i]);

		if (i == s_setField)
		{
			if (s_setEdit != 0U)
			{
				u8g2_DrawBox(&u8g2, (uint8_t)(x + 38U), (uint8_t)(y - 10U), 24, 12);
				u8g2_SetDrawColor(&u8g2, 0);
				u8g2_DrawStr(&u8g2, (uint8_t)(x + 40U), y, buf);
				u8g2_SetDrawColor(&u8g2, 1);
			}
			else
			{
				u8g2_DrawStr(&u8g2, (uint8_t)(x + 40U), y, buf);
				u8g2_DrawBox(&u8g2, x, (uint8_t)(y + 2U), 62, 1);
			}
		}
		else
		{
			u8g2_DrawStr(&u8g2, (uint8_t)(x + 40U), y, buf);
		}
	}
}

/* --- stopwatch ------------------------------------------------------------*/
static volatile uint8_t  s_swRunning = 0;
static volatile uint32_t s_swTenths  = 0;

static void StopwatchTimerCallback(TimerHandle_t xTimer)
{
	(void)xTimer;
	if (s_swRunning != 0U)
	{
		s_swTenths++;
	}
}

static void DrawStopwatchScreen(void)
{
	char buf[16];
	uint32_t t = s_swTenths;

	DrawTitle("STOPWATCH");

	u8g2_SetFont(&u8g2, u8g2_font_logisoso24_tn);
	snprintf(buf, sizeof(buf), "%lu.%lu", (unsigned long)(t / 10U), (unsigned long)(t % 10U));
	u8g2_DrawStr(&u8g2, 20, 45, buf);
}

/* --- DHT11 ----------------------------------------------------------------*/
static int32_t  s_temp10 = -1000;          /* temperature x10, -1000 = error */
static int32_t  s_hum10  = 0;

static void DrawDht11Screen(void)
{
	char buf[24];

	DrawTitle("DHT11");

	if (s_temp10 <= -1000)
	{
		u8g2_DrawStr(&u8g2, 20, 34, "sensor error");
	}
	else
	{
		u8g2_SetFont(&u8g2, u8g2_font_logisoso24_tn);
		snprintf(buf, sizeof(buf), "%ld.%ld", (long)(s_temp10 / 10), (long)(s_temp10 % 10));
		u8g2_DrawStr(&u8g2, 26, 34, buf);
		snprintf(buf, sizeof(buf), "%ld.%ld", (long)(s_hum10 / 10), (long)(s_hum10 % 10));
		u8g2_DrawStr(&u8g2, 26, 58, buf);

		u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
		u8g2_DrawStr(&u8g2, 0, 34, "T");
		u8g2_DrawStr(&u8g2, 0, 58, "H");
	}
	u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
	u8g2_DrawStr(&u8g2, 100, 10, "C / %");
}

/* --- about ----------------------------------------------------------------*/
static void DrawAboutScreen(void)
{
	DrawTitle("ABOUT");
	u8g2_DrawStr(&u8g2, 0, 28, "STM32F103C8T6");
	u8g2_DrawStr(&u8g2, 0, 40, "FreeRTOS + u8g2");
	u8g2_DrawStr(&u8g2, 0, 52, "OLED DHT11 4 keys");
}

/* --- battery --------------------------------------------------------------
 * The battery goes through a 1:1 divider onto PA0 (= ADC channel 0), so the
 * measured voltage is half of the battery voltage.  Change BATT_DIVIDER_NUM
 * if your board uses different resistors.
 * ------------------------------------------------------------------------*/
#define BATT_DIVIDER_NUM   2U          /* 1:1 divider -> multiply by 2 */


static void Batt_Update(void)
{
	ADC_ChannelConfTypeDef cfg = {0};
	uint32_t raw = 0;
	uint8_t  i;

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_ADC1_CLK_ENABLE();

	hadc1.Instance                   = ADC1;
	hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
	hadc1.Init.ContinuousConvMode    = DISABLE;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
	hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
	hadc1.Init.NbrOfConversion       = 1;
	if (HAL_ADC_Init(&hadc1) != HAL_OK) return;

	cfg.Channel      = ADC_CHANNEL_0;      /* PA0 */
	cfg.Rank         = ADC_REGULAR_RANK_1;
	cfg.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
	if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) return;

	for (i = 0; i < 8U; i++)
	{
		if (HAL_ADC_Start(&hadc1) != HAL_OK) break;
		if (HAL_ADC_PollForConversion(&hadc1, 5U) == HAL_OK)
		{
			raw += HAL_ADC_GetValue(&hadc1);
		}
		(void)HAL_ADC_Stop(&hadc1);
	}
	raw /= 8U;

	/* 12 bit, 3.3 V reference */
	s_battMv = (uint16_t)(((raw * 3300U) / 4095U) * BATT_DIVIDER_NUM);

	/* 4.2 V = 100 %, 3.3 V = 0 % (single cell Li-ion) */
	if (s_battMv >= 4200U)      s_battPercent = 100U;
	else if (s_battMv <= 3300U) s_battPercent = 0U;
	else s_battPercent = (uint8_t)(((uint32_t)(s_battMv - 3300U) * 100U) / 900U);
}

/* small battery symbol; bx,by = top left, percent 0..100 */
static void DrawBatteryIcon(u8g2_uint_t bx, u8g2_uint_t by, uint8_t percent)
{
	u8g2_uint_t w;
	u8g2_uint_t inner = 26;

	if (percent > 100U) percent = 100U;

	u8g2_DrawFrame(&u8g2, bx, by, inner + 6U, 11);
	u8g2_DrawBox(&u8g2, bx + inner + 6U, (u8g2_uint_t)(by + 3U), 3, 5);

	w = (u8g2_uint_t)(((uint32_t)percent * inner) / 100U);
	if (w > 0U) u8g2_DrawBox(&u8g2, (u8g2_uint_t)(bx + 3U), (u8g2_uint_t)(by + 3U), w, 5);
}

static void DrawBattScreen(void)
{
	char buf[24];

	Batt_Update();

	DrawTitle("BATTERY");
	DrawBatteryIcon(10, 26, s_battPercent);

	u8g2_SetFont(&u8g2, u8g2_font_logisoso16_tn);
	snprintf(buf, sizeof(buf), "%u%%", (unsigned)s_battPercent);
	u8g2_DrawStr(&u8g2, 18, 58, buf);

	u8g2_SetFont(&u8g2, u8g2_font_6x12_tf);
	snprintf(buf, sizeof(buf), "%u.%02uV", (unsigned)(s_battMv / 1000U),
	         (unsigned)((s_battMv % 1000U) / 10U));
	u8g2_DrawStr(&u8g2, 70, 58, buf);
}

/*============================================================================
 * UI task
 *==========================================================================*/
void App_UiTask(void *argument)
{
	uint32_t lastDhtMs   = 0;
	uint32_t battMs      = 0;
	uint32_t heartbeatMs = 0;
	uint8_t  i2cFailCount = 0;
	uint8_t  key;

	(void)argument;

	/* bring up the display ONCE (oled.c).  Every transfer has a timeout, so
	   even a missing OLED cannot block the boot. */
	Oled_Init();

	/* clear the I2C error flags left over from the start-up transfers */
	__HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
	hi2c1.ErrorCode = HAL_I2C_ERROR_NONE;
	g_i2cErrorCount = 0;

	Batt_Update();              /* first battery reading */
	g_bootStage = 0x20;

	for (;;)
	{
		/* ---- key events (never block: the screen must keep redrawing) -- */
		if (xQueueReceive(s_keyQueue, &key, 0) == pdPASS)
		{
			switch (s_screen)
			{
				case SCREEN_TIME:
					if (key == KEY_ENTER) { s_screen = SCREEN_MENU; Ui_Beep(); }
					break;

				case SCREEN_MENU:
					if (key == KEY_UP)
					{
						if (s_menuSel == 0) s_menuSel = MENU_ITEMS - 1; else s_menuSel--;
						Ui_Beep();
					}
					else if (key == KEY_DOWN)
					{
						s_menuSel++;
						if (s_menuSel >= MENU_ITEMS) s_menuSel = 0;
						Ui_Beep();
					}
					else if (key == KEY_ENTER)
					{
						Ui_Beep();
						switch (s_menuSel)
						{
							case 0: s_screen = SCREEN_STOPWATCH; s_swTenths = 0; s_swRunning = 0; break;
							case 1: s_screen = SCREEN_SETTIME;  s_setField = 0; s_setEdit = 0;   break;
							case 2: s_screen = SCREEN_DHT11;    s_temp10 = -1000; lastDhtMs = 0;  break;
							case 3: s_screen = SCREEN_BATT;     break;
							default: s_screen = SCREEN_ABOUT;   break;
						}
					}
					else if (key == KEY_EXIT)
					{
						Ui_Beep();
						s_screen = SCREEN_TIME;
					}
					break;

				case SCREEN_SETTIME:
					if ((key == KEY_UP) || (key == KEY_DOWN))
					{
						int8_t dir = (key == KEY_UP) ? 1 : -1;

						if (s_setEdit == 0U)
						{
							/* pick the next / previous field */
							int8_t f = (int8_t)s_setField + dir;
							if (f < 0) f = SET_FIELDS - 1;
							if (f >= SET_FIELDS) f = 0;
							s_setField = (uint8_t)f;
							Ui_Beep();
						}
						else
						{
							/* change the value, always kept inside its range */
							switch (s_setField)
							{
								case 0:
									if (dir > 0) { if (++s_year > 2099U) s_year = 2000U; }
									else         { if (s_year <= 2000U) s_year = 2099U; else s_year--; }
									break;
								case 1:
									if (dir > 0) { if (++s_month > 12U) s_month = 1U; }
									else         { if (s_month <= 1U) s_month = 12U; else s_month--; }
									break;
								case 2:
								{
									uint8_t dmax = DaysInMonth(s_year, s_month);
									if (dir > 0) { if (++s_day > dmax) s_day = 1U; }
									else         { if (s_day <= 1U) s_day = dmax; else s_day--; }
									break;
								}
								case 3:
									if (dir > 0) { if (++s_hour > 23U) s_hour = 0U; }
									else         { if (s_hour == 0U) s_hour = 23U; else s_hour--; }
									break;
								case 4:
									if (dir > 0) { if (++s_min > 59U) s_min = 0U; }
									else         { if (s_min == 0U) s_min = 59U; else s_min--; }
									break;
								default:
									if (dir > 0) { if (++s_sec > 59U) s_sec = 0U; }
									else         { if (s_sec == 0U) s_sec = 59U; else s_sec--; }
									break;
							}
							s_tenth = 0;
							Ui_Beep();
						}
					}
					else if (key == KEY_ENTER)
					{
						/* toggle between "pick a field" and "change the value" */
						s_setEdit = (uint8_t)!s_setEdit;
						Ui_Beep();
					}
					else if (key == KEY_EXIT)
					{
						Ui_Beep();
						if (s_setEdit != 0U) s_setEdit = 0U;      /* leave editing first */
						else                 s_screen = SCREEN_MENU;
					}
					break;

				case SCREEN_STOPWATCH:
					if (key == KEY_ENTER)      { s_swRunning = (uint8_t)!s_swRunning; Ui_Beep(); }
					else if (key == KEY_UP)    { s_swTenths = 0; Ui_Beep(); }
					else if (key == KEY_EXIT)  { Ui_Beep(); s_screen = SCREEN_MENU; }
					break;

				case SCREEN_DHT11:
					if (key == KEY_ENTER)     { s_temp10 = -1000; lastDhtMs = 0; Ui_Beep(); }
					else if (key == KEY_EXIT) { Ui_Beep(); s_screen = SCREEN_MENU; }
					break;

				case SCREEN_ABOUT:
					if (key == KEY_EXIT) { Ui_Beep(); s_screen = SCREEN_MENU; }
					break;

				default:
					s_screen = SCREEN_TIME;
					break;
			}
		}

		/* ---- battery: refresh every 10 s -------------------------------- */
		if ((Delay_Ms_Get() - battMs) >= 10000U)
		{
			battMs = Delay_Ms_Get();
			Batt_Update();
		}

		/* ---- periodic work -------------------------------------------- */
		if (s_screen == SCREEN_DHT11)
		{
			if ((Delay_Ms_Get() - lastDhtMs) >= 2000U)
			{
				int hum = 0, temp = 0;
				lastDhtMs = Delay_Ms_Get();
				if (DHT11_Read(&hum, &temp) == 0)
				{
					s_hum10  = (int32_t)hum  * 10;
					s_temp10 = (int32_t)temp * 10;
				}
				else
				{
					s_temp10 = -1000;
					DHT11_Init();
				}
			}
		}

		/* ---- draw ------------------------------------------------------ */
		u8g2_ClearBuffer(&u8g2);
		switch (s_screen)
		{
			case SCREEN_TIME:      DrawTimeScreen();      break;
			case SCREEN_MENU:      DrawMenuScreen();      break;
			case SCREEN_SETTIME:   DrawSetTimeScreen();   break;
			case SCREEN_STOPWATCH: DrawStopwatchScreen(); break;
			case SCREEN_DHT11:     DrawDht11Screen();     break;
			case SCREEN_BATT:      DrawBattScreen();      break;
			case SCREEN_ABOUT:     DrawAboutScreen();     break;
			default:               DrawTimeScreen();      break;
		}

		/* Oled_Flush() pushes the frame and tells us whether the I2C bus
		   really accepted it.  If it did not, the display would keep its old
		   content and look frozen while the CPU runs on, so after three
		   consecutive failures the bus is reset and the controller re-inited. */
		if (Oled_Flush() == 0U)
		{
			if (++i2cFailCount >= 3U)
			{
				i2cFailCount = 0;
				(void)Oled_Recover();
			}
		}
		else
		{
			i2cFailCount = 0;
		}

		/* ---- heartbeat -------------------------------------------------
		   1) the LED toggles every 500 ms while the UI task is alive
		   2) a small square in the top right corner moves every second, so a
		      frozen picture on the panel is immediately recognisable       */
		if ((Delay_Ms_Get() - heartbeatMs) >= 500U)
		{
			heartbeatMs = Delay_Ms_Get();
			Bsp_Led_Toggle();
		}
		{
			uint32_t up = Delay_Ms_Get();
			uint8_t  tick = (uint8_t)((up / 1000U) % 4U);

			u8g2_DrawBox(&u8g2, (u8g2_uint_t)(118U + tick * 2U), 2, 2, 2);
			if (((up / 500U) & 1U) == 0U)
			{
				u8g2_DrawBox(&u8g2, 110, 2, 2, 2);
			}
			/* push the two marks on top of the frame that was just drawn */
			u8g2_UpdateDisplay(&u8g2);
		}

		g_uiLoopCount++;
		Bsp_Buzzer_Poll();

		/* 50 ms: 20 full frames per second is plenty and halves the I2C load,
		   which makes the display noticeably more reliable */
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}

/*============================================================================
  * Key task: 5 ms poll, debounce by "3 equal samples" (~15 ms)
 *
 * The active level of the keys is detected automatically at start-up:
 * the level measured while nothing is pressed is the IDLE level, and a key is
 * pressed whenever its pin differs from that idle level.  Both wirings work:
 *   - key to GND  : idle = high, pressing pulls the pin low
 *   - key to VCC  : idle = low,  pressing pulls the pin high
 * (the original project used EXTI on the RISING edge, i.e. "press = high")
 *
 * Diagnostics readable with a debugger:
 *   g_keyIdleLevel : idle level of K1..K4 (detected at start-up)`r`n *   g_keyTaskRuns  : increments every 5 ms while the key task is alive
 *   g_keyTaskPress : number of accepted presses since boot
 *   g_keyTaskLevel : current raw level, bit set = pin is high
 * ==========================================================================*/
volatile uint32_t g_keyTaskRuns   = 0;
volatile uint32_t g_keyTaskPress  = 0;
volatile uint8_t  g_keyTaskLevel = 0;

void App_KeyTask(void *argument)
{
	typedef struct
	{
		uint16_t pin;
		uint8_t  code;
		uint8_t  idle;        /* level when released (detected at start-up) */
		uint8_t  stable;      /* 1 = currently held down                    */
		uint8_t  samples;
		uint32_t lastSendMs;
	} key_t;

	/* K1 = PB0 = UP, K2 = PB1 = DOWN, K3 = PB10 = ENTER, K4 = PB11 = EXIT
	   This is the ONLY place where the key wiring is described. */
	static key_t keys[4] =
	{
		{ GPIO_PIN_0,  KEY_UP,    0, 0, 0, 0 },
		{ GPIO_PIN_1,  KEY_DOWN,  0, 0, 0, 0 },
		{ GPIO_PIN_10, KEY_ENTER, 0, 0, 0, 0 },
		{ GPIO_PIN_11, KEY_EXIT,  0, 0, 0, 0 }
	};

	uint8_t  i;

	(void)argument;

	/* ---- work out the idle level of every key ----------------------------
	   Voting over 8 samples so that a single glitch cannot decide it. */
	{
		uint8_t n, high[4] = {0, 0, 0, 0};

		for (n = 0; n < 8U; n++)
		{
			for (i = 0; i < 4U; i++)
			{
				if (HAL_GPIO_ReadPin(WATCH_KEY_PORT, keys[i].pin) != GPIO_PIN_RESET)
				{
					high[i]++;
				}
			}
			Delay_Ms(2);
		}

		for (i = 0; i < 4U; i++)
		{
			keys[i].idle = (high[i] >= 4U) ? 1U : 0U;
			if (keys[i].idle != 0U) g_keyIdleLevel = (uint8_t)(g_keyIdleLevel | (1U << i));
		}
	}

	for (;;)
	{
		uint8_t level = 0;

		g_keyTaskRuns++;

		for (i = 0; i < 4U; i++)
		{
			uint8_t raw     = (HAL_GPIO_ReadPin(WATCH_KEY_PORT, keys[i].pin) != GPIO_PIN_RESET) ? 1U : 0U;
			uint8_t pressed = (raw != keys[i].idle) ? 1U : 0U;   /* same for both wirings */

			if (raw != 0U) level = (uint8_t)(level | (1U << i));

			if (pressed != keys[i].stable)
			{
				if (++keys[i].samples >= 3U)
				{
					keys[i].stable  = pressed;
					keys[i].samples = 0U;

					if (pressed != 0U)
					{
						/* report the press, drop it if the queue is full -
						   the key task must never block */
						(void)xQueueSend(s_keyQueue, &keys[i].code, 0);
						g_keyTaskPress++;
						keys[i].lastSendMs = Delay_Ms_Get();
					}
				}
			}
			else
			{
				keys[i].samples = 0U;

				/* key held down: repeat about every 250 ms, which makes
				   changing a value or walking a list much faster */
				if ((pressed != 0U) &&
				    ((Delay_Ms_Get() - keys[i].lastSendMs) >= 250U))
				{
					(void)xQueueSend(s_keyQueue, &keys[i].code, 0);
					g_keyTaskPress++;
					keys[i].lastSendMs = Delay_Ms_Get();
				}
			}
		}

		g_keyTaskLevel  = level;
		vTaskDelay(pdMS_TO_TICKS(5));
	}
}

/*============================================================================
 * Application start-up
 *==========================================================================*/
void App_Init(void)
{
	s_uiTask = xTaskCreateStatic(App_UiTask, "ui", UI_TASK_STACK_WORDS, NULL,
	                             (UBaseType_t)6, s_uiStack, &s_uiTcb);
	s_keyTask = xTaskCreateStatic(App_KeyTask, "key", KEY_TASK_STACK_WORDS, NULL,
	                              (UBaseType_t)5, s_keyStack, &s_keyTcb);

	s_keyQueue = xQueueCreateStatic(KEY_QUEUE_LEN, sizeof(uint8_t),
	                                s_keyQueueStorage, &s_keyQueueStruct);

	s_clockTimer = xTimerCreateStatic("rtc", pdMS_TO_TICKS(100), pdTRUE,
	                                  NULL, ClockTimerCallback, &s_clockTimerStruct);

	s_swTimer = xTimerCreateStatic("sw", pdMS_TO_TICKS(100), pdTRUE,
	                               NULL, StopwatchTimerCallback, &s_swTimerStruct);

	if ((s_uiTask == NULL) || (s_keyTask == NULL) || (s_keyQueue == NULL) ||
	    (s_clockTimer == NULL) || (s_swTimer == NULL))
	{
		Bsp_Led_Write(1);
		taskDISABLE_INTERRUPTS();
		for (;;) { }
	}

	(void)xTimerStart(s_clockTimer, 0);
	(void)xTimerStart(s_swTimer, 0);
	g_bootStage = 0x10;
}
