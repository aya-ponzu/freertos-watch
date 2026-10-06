#ifndef __DATA_H__
#define __DATA_H__

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "u8g2.h"

typedef struct UI{
const char name[20];
uint8_t num;
const uint8_t data[128];
int32_t x;
int32_t y;
int32_t w;
int32_t h;
}ui;

typedef struct Images{
	int x;
	int y;
	int w;
	int h;
}Image;

typedef struct Key_data{
	uint8_t rdata;
	uint8_t ldata;
	uint8_t updata;
	uint8_t exdata;
}Key_data;

typedef struct str1{
	char *string;
	struct str1 *next_str;
}str1, *p_str;

/* the one and only u8g2 object (it owns the 1024 byte display buffer,
   never create a local u8g2_t on a task stack) */
extern u8g2_t u8g2;

/* a key event with all four flags cleared - used to build queue items so that
   the unused flags can never carry garbage */
extern const Key_data s_emptyKey;

/* key interrupt handler (called from HAL_GPIO_EXTI_Callback) */
void Key_Scan_Handler(uint16_t GPIO_Pin);

/* Leave the running application and give the hand back to the menu.
   It flushes the key queue first: without this the menu - or the app it
   resumes - would re-execute the very key event that has just been handled
   and the two screens would ping-pong. */
void App_ExitToMenu(void);

void u8g2_config(void);
void ui_run(int* a, int* a_trg, int b);
void ui_left(int32_t* a, int b);
void ui_right(int32_t* a, int b);
void ui_up(int32_t* a, int b);
void ui_down(int32_t* a, int b);

extern const uint8_t ShowPower[30];
extern const uint8_t ShowGame[30];
extern const uint8_t LeftMove[30];
extern const uint8_t RightMove[30];
extern const uint8_t BigNum[10][120];
extern const uint8_t light[];
extern const uint8_t Num_6x8[12][8];
extern const uint8_t wooden_flsh[2][384];
extern const uint8_t gongde[64]; 
extern const uint8_t hammer[40];

extern ui cleder;
extern ui torch;
extern ui hum;
extern ui clock;
extern ui setting;

#endif
