/*
 * FreeRTOSConfig.h  -  STM32F103C8T6 watch project (rewritten from scratch)
 *
 * Design decisions:
 *   - STATIC allocation only (configSUPPORT_STATIC_ALLOCATION = 1,
 *     configSUPPORT_DYNAMIC_ALLOCATION = 0).  There is no RTOS heap at all,
 *     so "xTaskCreate silently returned NULL" simply cannot happen any more
 *     and the RAM usage is fixed and provable at link time.
 *   - The 1 Hz clock tick runs in a FreeRTOS software timer.  Its task
 *     priority (7) is ABOVE the application tasks (6), otherwise the timer
 *     task never runs and the time never advances.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
  #include <stdint.h>
  extern uint32_t SystemCoreClock;
#endif

/*-----------------------------------------------------------
 * Scheduler
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       ( SystemCoreClock )
#define configTICK_RATE_HZ                       ( (TickType_t) 1000 )
#define configMAX_PRIORITIES                     ( 8 )
#define configMINIMAL_STACK_SIZE                 ( (uint16_t) 96 )
#define configMAX_TASK_NAME_LEN                  ( 12 )
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configQUEUE_REGISTRY_SIZE                0
#define configUSE_QUEUE_SETS                     0
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TRACE_FACILITY                 0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0

/*-----------------------------------------------------------
 * Memory allocation - static only
 *----------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                    ( (size_t) 0 )
#define configAPPLICATION_ALLOCATED_HEAP         0

/*-----------------------------------------------------------
 * Software timers
 *----------------------------------------------------------*/
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                ( 7 )
#define configTIMER_QUEUE_LENGTH                 8
#define configTIMER_TASK_STACK_DEPTH             ( configMINIMAL_STACK_SIZE )

/*-----------------------------------------------------------
 * Optional API
 *----------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTimerGetTimerDaemonTaskHandle   0
#define INCLUDE_xTaskAbortDelay                  0

/*-----------------------------------------------------------
 * Run time checks
 *----------------------------------------------------------*/
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             0
#define configASSERT( x )  if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/*-----------------------------------------------------------
 * Cortex-M3 interrupt priorities (4 implemented priority bits)
 *----------------------------------------------------------*/
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS                         __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS                         4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5

#define configKERNEL_INTERRUPT_PRIORITY \
        ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
        ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/*-----------------------------------------------------------
 * Map the FreeRTOS port handlers onto the CMSIS names
 *----------------------------------------------------------*/
#define vPortSVCHandler        SVC_Handler
#define xPortPendSVHandler     PendSV_Handler
#define xPortSysTickHandler    SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
