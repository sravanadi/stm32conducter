/**
 * @file    meter_comm.h
 * @brief   RS-485 Modbus 
  slave interface on USART1 (PB6=TX, PB7=RX, PB5=REDE).
 */

#ifndef __METER_COMM_H
#define __METER_COMM_H

#include "meter_core.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void meter_comm_init(const settings_t *s);
void meter_comm_task(const meter_reading_t *r, const settings_t *s);

#ifdef __cplusplus
}
#endif


#endif /* __METER_COMM_H */
