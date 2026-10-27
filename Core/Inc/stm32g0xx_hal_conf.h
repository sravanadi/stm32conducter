/**
  ******************************************************************************
  * @file    stm32g0xx_hal_conf.h
  * @brief   HAL configuration file.
  ******************************************************************************
  */

#ifndef __STM32G0xx_HAL_CONF_H
#define __STM32G0xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED

/* ########################## Oscillator Values ############################# */
#if !defined  (HSI_VALUE)
  #define HSI_VALUE    (16000000UL) /*!< Value of the Internal oscillator in Hz */
#endif

#if !defined  (HSE_VALUE)
  #define HSE_VALUE    (8000000UL)  /*!< Value of the External oscillator in Hz */
#endif

#if !defined  (LSI_VALUE)
  #define LSI_VALUE    (32000UL)    /*!< Value of the Internal Low Speed oscillator in Hz */
#endif

#if !defined  (LSE_VALUE)
  #define LSE_VALUE    (32768UL)    /*!< Value of the External Low Speed oscillator in Hz */
#endif

#if !defined  (VDD_VALUE)
  #define VDD_VALUE    (3300UL)     /*!< Value of VDD in mv */
#endif

#if !defined  (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT   (100UL)   /*!< Time out for HSE start up, in ms */
#endif

#if !defined  (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT   (5000UL)  /*!< Time out for LSE start up, in ms */
#endif

#if !defined  (EXTERNAL_I2S1_CLOCK_VALUE)
  #define EXTERNAL_I2S1_CLOCK_VALUE (48000000UL) /*!< Value of the External I2S1 clock in Hz */
#endif

#define  TICK_INT_PRIORITY            0U /*!< tick interrupt priority */
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE              1U
#define  INSTRUCTION_CACHE_ENABLE     1U

/* ########################## Includes ###################################### */
#ifdef HAL_RCC_MODULE_ENABLED
 #include "stm32g0xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
 #include "stm32g0xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32g0xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
 #include "stm32g0xx_hal_cortex.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
 #include "stm32g0xx_hal_pwr.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
 #include "stm32g0xx_hal_spi.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
 #include "stm32g0xx_hal_flash.h"
#endif

/* Exported macro ------------------------------------------------------------*/
#ifdef  USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32G0xx_HAL_CONF_H */
