/**
 * @file    STM32G030K8T_hal.h
 * @brief   MCU-specific HAL wrapper header for STM32G030K8T6
 *          Binds the STM32G030xx part configuration to the STM32CubeG0 HAL library.
 */

#ifndef __STM32G030K8T_HAL_H
#define __STM32G030K8T_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Target MCU: STM32G030K8Tx (Cortex-M0+, 64KB Flash, 8KB RAM, LQFP32) */
#ifndef STM32G030xx
#define STM32G030xx
#endif

#ifndef USE_HAL_DRIVER
#define USE_HAL_DRIVER
#endif

/* Include standard STM32G0 family HAL library */
#include "stm32g0xx_hal.h"

#ifdef __cplusplus
}
#endif

#endif /* __STM32G030K8T_HAL_H */
