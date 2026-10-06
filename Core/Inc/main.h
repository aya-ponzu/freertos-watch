/* main.h - common defines for the STM32F103C8T6 watch project */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* peripheral handles (defined in bsp.c) */
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim1;

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
