/**
 * @file    serial_port_mointor.h
 * @brief   Real-time serial port monitor and telemetry interface on USART1
 *          Target MCU : STM32G030K8T6 (16 MHz HSI)
 *          Pins       : PB6 = USART1_TX (AF0), PB7 = USART1_RX (AF0)
 *                       PB5 = RS-485 Transceiver RE/DE Control (Active HIGH TX)
 *          Debug/GND  : J3 Header Pin 4 (GND), Pin 1 (3.3V)
 */

#ifndef __SERIAL_PORT_MOINTOR_H
#define __SERIAL_PORT_MOINTOR_H

#include "meter_core.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialize USART1 on PB6 (TX), PB7 (RX) with RS-485 DE on PB5.
 * @param  s: Pointer to persistent settings (Baud rate, Parity, Stop bits).
 */
void serial_monitor_init(const settings_t *s);

/**
 * @brief  Main non-blocking task: handles command inputs, auto-streaming,
 *         and Modbus RTU packet processing. Call every main loop iteration.
 * @param  r: Current live meter reading structure.
 * @param  s: Current settings structure.
 */
void serial_monitor_task(const meter_reading_t *r, const settings_t *s);

/**
 * @brief  USART1 interrupt handler callback for zero-loss RS-485 RX & instant DE/RE turnaround.
 */
void serial_monitor_usart1_irq(void);

/**
 * @brief  Transmit raw bytes over RS-485 (handles PB5 DE assert and TC wait).
 * @param  data: Pointer to buffer.
 * @param  len: Number of bytes to transmit.
 */
void serial_monitor_send_bytes(const uint8_t *data, uint16_t len);

/**
 * @brief  Transmit null-terminated ASCII string over RS-485.
 * @param  str: Null-terminated string.
 */
void serial_monitor_send_str(const char *str);

/**
 * @brief  Transmit instantaneous sensor reading in human-readable formatted text.
 * @param  r: Current reading.
 * @param  s: Current settings.
 */
void serial_monitor_send_reading(const meter_reading_t *r, const settings_t *s);

/**
 * @brief  Transmit instantaneous sensor reading in standard JSON format.
 * @param  r: Current reading.
 * @param  s: Current settings.
 */
void serial_monitor_send_json(const meter_reading_t *r, const settings_t *s);

/**
 * @brief  Toggle or set continuous real-time streaming mode.
 * @param  enable: true = streaming ON, false = streaming OFF.
 */
void serial_monitor_set_streaming(bool enable);

/**
 * @brief  Get current streaming status.
 * @retval true if streaming is active.
 */
bool serial_monitor_is_streaming(void);

/**
 * @brief  Set output mode to JSON or human-readable text.
 * @param  json_mode: true = JSON, false = Formatted Text.
 */
void serial_monitor_set_json_mode(bool json_mode);

/**
 * @brief  Get output mode.
 * @retval true if in JSON mode.
 */
bool serial_monitor_is_json_mode(void);

/**
 * @brief  Set streaming interval in milliseconds (e.g. 500 or 1000 ms).
 * @param  interval_ms: Interval in milliseconds.
 */
void serial_monitor_set_interval(uint32_t interval_ms);

/**
 * @brief  Get manual relay overrides set from serial monitor (-1 = auto).
 * @param  relay_index: 0 for Relay 1, 1 for Relay 2.
 * @retval -1 if auto, 0 if forced OFF, 1 if forced ON.
 */
int8_t serial_monitor_relay_override(uint8_t relay_index);

#ifdef __cplusplus
}
#endif

#endif /* __SERIAL_PORT_MOINTOR_H */
