/**
 * @file    meter_afe.h
 * @brief   Analog Front End (AFE) driver for probe daughterboard interface.
 *
 * DAUGHTERBOARD (PROBE FRONT END) INTERFACE:
 * Connector: J2 / Probe Interface Header (8-pin 2.54mm)
 * Pin list:
 *   Pin 1: +3.3V (VCC_ANALOG supply, ASSUMPTION: 3.3V +/-1%)
 *   Pin 2: GND   (Analog ground)
 *   Pin 3: EXC_A (PA6, MCU Pin 13) - AC Excitation Phase A
 *   Pin 4: EXC_B (PA7, MCU Pin 14) - AC Excitation Phase B
 *   Pin 5: CELL_SENSE (PA0, MCU Pin 7, ADC1_IN0) - Cell voltage divider
 *   Pin 6: TEMP_SENSE (PA5, MCU Pin 12, ADC1_IN5) - NTC10K / PT1000 divider
 *   Pin 7: NC / SPARE (PA15)
 *   Pin 8: SHIELD / EARTH (Connected to probe cable shield)
 *
 * Signals:
 *   [Excitation pins]            : PA6 (EXC_A), PA7 (EXC_B) (Bipolar square wave)
 *   [ADC pins cell/temperature]  : PA0 (Cell Sense), PA5 (Temp Sense)
 *   [Range select pins]          : Single wide dynamic range, R_ref = 1000 Ohm
 *   Supply                       : +3.3V regulated from main board
 */

#ifndef __METER_AFE_H
#define __METER_AFE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize ADC1 and excitation GPIOs (PA0, PA5, PA6, PA7) */
void meter_afe_init(void);

/**
 * @brief  Implement afe_read(&cell_ohms, &temp_ohms) for the daughterboard.
 *         Performs active bipolar AC square wave excitation on PA6/PA7,
 *         eliminates electrode polarization, and samples ADC on PA0 & PA5.
 * @param  cell_ohms: Output cell resistance in Ohms.
 * @param  temp_ohms: Output temperature probe resistance in Ohms.
 * @return true if valid reading acquired.
 */
bool afe_read(float *cell_ohms, float *temp_ohms);

/* Backward compatibility alias */
bool meter_afe_read(float *cell_ohms, float *temp_ohms);

#ifdef __cplusplus
}
#endif

#endif /* __METER_AFE_H */
