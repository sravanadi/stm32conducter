/**
 * @file    meter_ui.h
 * @brief   User Interface engine for Mirrant TDS 4002 Conductivity Meter.
 *          Implements the 7 vertical menu categories, sub-menus, option pickers,
 *          numeric editors, and measurement screen.
 */

#ifndef __METER_UI_H
#define __METER_UI_H

#include "meter_core.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void meter_ui_init(settings_t *s, meter_reading_t *r);
void meter_ui_task(void);
int8_t meter_ui_relay_test_state(uint8_t relay_idx);

#ifdef __cplusplus
}
#endif

#endif /* __METER_UI_H */
