/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   Target: STM32G030K8T6 (Sensor Display Board)
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "STM32G030K8T_hal.h"
#include "board_pins.h"
#include "lcd_st7565.h"
#include "buttons.h"

/* Ensure FLASH_LATENCY_0 is defined if HAL flash header is bypassed */
#ifndef FLASH_LATENCY_0
#define FLASH_LATENCY_0                 0x00000000UL
#endif

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
