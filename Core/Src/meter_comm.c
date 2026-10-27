/**
 * @file    meter_comm.c
 * @brief   RS-485 Modbus RTU & Serial Monitor communication wrapper on USART1.
 *          Hardware: PB6=TX, PB7=RX, PB5=RS-485 DE/RE
 */

#include "meter_comm.h"
#include "serial_port_mointor.h"

void meter_comm_init(const settings_t *s)
{
    serial_monitor_init(s);
}

void meter_comm_task(const meter_reading_t *r, const settings_t *s)
{
    serial_monitor_task(r, s);
}
