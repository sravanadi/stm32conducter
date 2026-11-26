/**
 * @file    turbidity_sensor.h
 * @brief   RS-485 Modbus RTU Master driver for Digital Turbidity Sensor (0-400 NTU).
 *          Physical: RS-485 bus on PB6(TX), PB7(RX), PB5(DE/RE).
 */

#ifndef __TURBIDITY_SENSOR_H
#define __TURBIDITY_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the Turbidity Sensor polling engine */
void turbidity_init(void);

/* Periodic task: non-blocking Modbus RTU polling of the turbidity sensor */
void turbidity_task(uint32_t now_ms);

/* Process an incoming RS-485 packet to check if it is a Turbidity response.
 * Returns true if the packet was consumed as a valid turbidity response. */
bool turbidity_parse_packet(const uint8_t *buf, uint16_t len);

/* Get the latest turbidity reading in NTU (0.0 to 400.0 NTU) */
float turbidity_get_ntu(void);

/* Returns true if sensor is disconnected, offline, or timed out */
bool turbidity_is_fault(void);

/* Set and get the Modbus slave address to poll (default 0x03) */
void turbidity_set_slave_addr(uint8_t addr);
uint8_t turbidity_get_slave_addr(void);

/* Set and get the Modbus register address to poll (default 14 / 0x000E) */
void turbidity_set_reg_addr(uint16_t reg);
uint16_t turbidity_get_reg_addr(void);

#ifdef __cplusplus
}
#endif

#endif /* __TURBIDITY_SENSOR_H */
