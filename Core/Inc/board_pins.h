/**
 * @file    board_pins.h
 * @brief   Hardware pin definitions and peripheral mapping for STM32G030K8T6
 *          Target Board: Sensor Display Controller (PCB Rev: 02 May 2025)
 *
 * All MCU pin assignments are traced directly from schematic sens_display_02may25_1947.pdf.
 */

#ifndef __BOARD_PINS_H
#define __BOARD_PINS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "STM32G030K8T_hal.h"

/* ========================================================================== */
/*                      DISPLAY PIN DEFINITIONS (ST7565R)                     */
/* ========================================================================== */
/*
 * Display Module: NHD-C12864LZ-FSW-FBW-3V3 (128x64 Graphic LCD)
 * Controller    : Sitronix ST7565R in 4-wire serial SPI mode (PS=0, write-only)
 */

/* LCD Chip Select (/CS1) - Active LOW (MCU Pin 18: PA8) */
#define LCD_CS_PORT                 GPIOA
#define LCD_CS_PIN                  GPIO_PIN_8

/* LCD Hardware Reset (/RES) - Active LOW (MCU Pin 19: PA9) */
#define LCD_RES_PORT                GPIOA
#define LCD_RES_PIN                 GPIO_PIN_9

/* LCD Command/Data Select (A0) - LOW=Command, HIGH=Data (MCU Pin 21: PA10) */
#define LCD_A0_PORT                 GPIOA
#define LCD_A0_PIN                  GPIO_PIN_10

/* LCD Serial Clock (SCLK / DB6) - Hardware SPI1 SCK, AF0 (MCU Pin 8: PA1) */
#define LCD_SCK_PORT                GPIOA
#define LCD_SCK_PIN                 GPIO_PIN_1

/* LCD Serial Data Input (MOSI / DB7) - Hardware SPI1 MOSI, AF0 (MCU Pin 9: PA2) */
#define LCD_MOSI_PORT               GPIOA
#define LCD_MOSI_PIN                GPIO_PIN_2

/* LCD Backlight Enable (DISP_LED_EN) - Active HIGH via AD8591 SD (MCU Pin 20: PC6) */
#define LCD_LED_EN_PORT             GPIOC
#define LCD_LED_EN_PIN              GPIO_PIN_6

/*
 * LCD Parallel Control Signals connected to MCU pins:
 * In 4-wire serial mode, /RD and /WR should be kept HIGH.
 * Setting these pins as HIGH outputs prevents floating input noise on the LCD controller.
 */
#define LCD_RW_PORT                 GPIOA
#define LCD_RW_PIN                  GPIO_PIN_11   /* MCU Pin 22: PA11 */

#define LCD_RD_PORT                 GPIOA
#define LCD_RD_PIN                  GPIO_PIN_12   /* MCU Pin 23: PA12 */


/* ========================================================================== */
/*                          RELAY OUTPUT DEFINITIONS                          */
/* ========================================================================== */
/*
 * Relays LS1 & LS2 are driven via optocouplers ISO1 & ISO2 (EL817).
 * Logic HIGH energizes the coil; Logic LOW keeps the relay completely OFF.
 * MUST be initialized LOW at boot.
 */
#define RELAY_1_PORT                GPIOA
#define RELAY_1_PIN                 GPIO_PIN_3    /* MCU Pin 10: PA3 -> ISO1 */

#define RELAY_2_PORT                GPIOA
#define RELAY_2_PIN                 GPIO_PIN_4    /* MCU Pin 11: PA4 -> ISO2 */


/* ========================================================================== */
/*                        PUSH BUTTON DEFINITIONS                             */
/* ========================================================================== */
/*
 * Tactile switches SW1..SW4 with 10k hardware pull-ups (R30-R33) to 3.3V
 * and 0.1uF debounce capacitors (C30-C33) to GND.
 * Logic LOW = Pressed, Logic HIGH = Released.
 *
 * Physical Front Panel Layout (Left to Right):
 *   1st (far left)  : SW4 (MCU Pin 28: PB4) = MENU / Long-Press Return
 *   2nd             : SW3 (MCU Pin 27: PB3) = DOWN / Sub-bar Mode Toggle
 *   3rd             : SW2 (MCU Pin 17: PB2) = RIGHT / Measure Type Cycle
 *   4th (far right) : SW1 (MCU Pin 16: PB1) = ENTER / Status Screen
 */
#define SW4_PORT                    GPIOB
#define SW4_PIN                     GPIO_PIN_4    /* MCU Pin 28: PB4 (SW4: MENU)  */

#define SW3_PORT                    GPIOB
#define SW3_PIN                     GPIO_PIN_3    /* MCU Pin 27: PB3 (SW3: DOWN)  */

#define SW2_PORT                    GPIOB
#define SW2_PIN                     GPIO_PIN_2    /* MCU Pin 17: PB2 (SW2: RIGHT) */

#define SW1_PORT                    GPIOB
#define SW1_PIN                     GPIO_PIN_1    /* MCU Pin 16: PB1 (SW1: ENTER) */


/* ========================================================================== */
/*                       RS-485 TRANSCEIVER (THVD1450DR)                      */
/* ========================================================================== */
/*
 * PB5 = RE/DE control (LOW = Receive mode, HIGH = Transmit mode)
 * PB6 = USART1_TX (AF0)
 * PB7 = USART1_RX (AF0)
 */
#define RS485_REDE_PORT             GPIOB
#define RS485_REDE_PIN              GPIO_PIN_5    /* MCU Pin 29: PB5 */

#define RS485_TX_PORT               GPIOB
#define RS485_TX_PIN                GPIO_PIN_6    /* MCU Pin 30: PB6 */

#define RS485_RX_PORT               GPIOB
#define RS485_RX_PIN                GPIO_PIN_7    /* MCU Pin 31: PB7 */


/* ========================================================================== */
/*                     INA226 VOLTAGE/CURRENT SENSOR (I2C1)                   */
/* ========================================================================== */
/*
 * U5 INA226: A0=GND, A1=GND -> 7-bit I2C Address = 0x40 (8-bit Write = 0x80)
 * PB8 = I2C1_SCL (AF6)
 * PB9 = I2C1_SDA (AF6)
 * PB0 = ALERT Interrupt
 */
#define INA226_ALERT_PORT           GPIOB
#define INA226_ALERT_PIN            GPIO_PIN_0    /* MCU Pin 15: PB0 */

#define INA226_SCL_PORT             GPIOB
#define INA226_SCL_PIN              GPIO_PIN_8    /* MCU Pin 32: PB8 */

#define INA226_SDA_PORT             GPIOB
#define INA226_SDA_PIN              GPIO_PIN_9    /* MCU Pin 1:  PB9 */

#define INA226_I2C_ADDR_7BIT        0x40
#define INA226_I2C_ADDR_8BIT        (0x40 << 1) //Result in 0x80 means left by 1


/* ========================================================================== */
/*                           SWD DEBUG PINS                                   */
/* ========================================================================== */
/*
 * J3 Header (5-pin SWD):
 * Pin 1 = 3.3V, Pin 2 = PA13 (SWDIO), Pin 3 = PA14 (SWCLK), Pin 4 = GND, Pin 5 = NRST
 */
#define SWD_SWDIO_PORT              GPIOA
#define SWD_SWDIO_PIN               GPIO_PIN_13   /* MCU Pin 24: PA13 */

#define SWD_SWCLK_PORT              GPIOA
#define SWD_SWCLK_PIN               GPIO_PIN_14   /* MCU Pin 25: PA14 */


/* ========================================================================== */
/*                     CONVENIENCE CONTROL MACROS                             */
/* ========================================================================== */
#define BOARD_RELAY1_OFF()          HAL_GPIO_WritePin(RELAY_1_PORT, RELAY_1_PIN, GPIO_PIN_RESET)
#define BOARD_RELAY1_ON()           HAL_GPIO_WritePin(RELAY_1_PORT, RELAY_1_PIN, GPIO_PIN_SET)

#define BOARD_RELAY2_OFF()          HAL_GPIO_WritePin(RELAY_2_PORT, RELAY_2_PIN, GPIO_PIN_RESET)
#define BOARD_RELAY2_ON()           HAL_GPIO_WritePin(RELAY_2_PORT, RELAY_2_PIN, GPIO_PIN_SET)

#define BOARD_RS485_RX_MODE()       HAL_GPIO_WritePin(RS485_REDE_PORT, RS485_REDE_PIN, GPIO_PIN_RESET)
#define BOARD_RS485_TX_MODE()       HAL_GPIO_WritePin(RS485_REDE_PORT, RS485_REDE_PIN, GPIO_PIN_SET)

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_PINS_H */
