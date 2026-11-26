/**
 * @file    meter_core.c
 * @brief   Core calculation engine, data models, calibration, and Modbus RTU
 *          for Mirrant TDS 4002 Conductivity/TDS/Resistivity/Salinity meter.
 */

#include "meter_core.h"
#include "turbidity_sensor.h"
#include <math.h>
#include <string.h>

#define NTC_R25 10000.0f
#define NTC_T25 298.15f
#define NTC_BETA 3950.0f

void meter_core_default_settings(settings_t *s) {
  if (!s)
    return;
  memset(s, 0, sizeof(settings_t));

  s->magic = METER_SETTINGS_MAGIC;
  s->version = METER_SETTINGS_VERSION;

  /* 1. Probe Settings */
  s->k_constant = 0.0852f;
  s->ec_offset = 0.0f;
  s->ec_factor = 1.000f;
  s->cal_points_count = 0;
  s->temp_coef = 2.00f; /* 2.00 %/C */
  s->tds_coef = 0.500f; /* 0.500 ppm per uS/cm */
  s->display_unit = UNIT_AUTO;

  /* 2. Temperature Settings */
  s->temp_probe_type = TEMP_PROBE_PT1000;
  s->temp_unit = TEMP_UNIT_C;
  s->temp_cal_offset = 0.0f;
  s->manual_temp = 25.0f;

  /* 3. Alarm / Relay Settings */
  s->relay1_func = RELAY_FUNC_COND;
  s->relay1_on_val = 2000.0f;  /* High alarm ON at 2000 */
  s->relay1_off_val = 1800.0f; /* High alarm OFF at 1800 */
  s->relay1_wash_int_min = 60;
  s->relay1_wash_time_s = 10;
  s->relay1_wash_state = WASH_STATE_HOLD;

  s->relay2_func = RELAY_FUNC_COND;
  s->relay2_on_val = 100.0f;  /* Low alarm ON at 100 */
  s->relay2_off_val = 120.0f; /* Low alarm OFF at 120 */
  s->relay2_wash_int_min = 60;
  s->relay2_wash_time_s = 10;
  s->relay2_wash_state = WASH_STATE_HOLD;

  /* 4. Current Output Settings */
  s->curr1_type = CURRENT_4_20MA;
  s->curr1_chan = CURRENT_CHAN_COND;
  s->curr1_min_val = 0.0f;
  s->curr1_max_val = 2000.0f;

  s->curr2_type = CURRENT_4_20MA;
  s->curr2_chan = CURRENT_CHAN_TEMP;
  s->curr2_min_val = 0.0f;
  s->curr2_max_val = 100.0f;

  /* 5. Communication Settings */
  s->slave_addr = 1;
  s->baud_rate = 9600;
  s->parity = PARITY_NONE;
  s->stop_bits = STOP_1_BIT;

  /* 6. System Settings */
  s->display_speed = 3;     /* Filter level 4 */
  s->backlight_mode = 1;    /* Always ON */
  s->backlight_timeout = 5; /* 5 minutes */
  s->backlight_state = 1;   /* ON */
  s->contrast = 0x0C;       /* ST7565 default contrast */
  s->password = 0;

  /* 7. Maintenance / Test or Calibration Mode */
  s->measure_mode = 0;      /* 0 = Real Probe */
  s->relay_safe_state = 0;  /* Relays OFF during probe fault */
  s->last_measure_type = 0; /* 0 = COND default */
  s->last_sub_bar_mode = 0; /* 0 = Sub-bar mode 1 (Temp & K) */

  /* 8. Sample Test Defaults: Formula (a+b)(c-d) */
  s->sample_a = 10.0f;
  s->sample_b = 5.0f;
  s->sample_c = 20.0f;
  s->sample_d = 4.0f;

  s->crc =
      crc16_modbus((const uint8_t *)s, sizeof(settings_t) - sizeof(uint16_t));
}

float ntc10k_to_c(float ohms) {
  if (ohms <= 10.0f || ohms >= 1000000.0f) {
    return -999.0f; /* Invalid */
  }
  /* ASSUMPTION: Standard NTC 10k thermistor with Beta=3950 */
  float steinhart = (1.0f / NTC_T25) + (1.0f / NTC_BETA) * logf(ohms / NTC_R25);
  float temp_c = (1.0f / steinhart) - 273.15f;
  return temp_c;
}

float pt1000_to_c(float ohms) {
  if (ohms <= 100.0f || ohms >= 4000.0f) {
    return -999.0f;
  }
  /* IEC 60751: R(T) = R0 * (1 + A * T + B * T^2) for T >= 0 C
   * Exact quadratic inversion:
   *   T = (-A + sqrt(A^2 - 4 * B * (1 - R / 1000))) / (2 * B)
   * where A = 3.9083e-3, B = -5.775e-7
   */
  float radicand = 1.52748e-5f - 4.0f * (-5.775e-7f) * (1.0f - (ohms / 1000.0f));
  if (radicand < 0.0f) radicand = 0.0f;
  return (-3.9083e-3f + sqrtf(radicand)) / (2.0f * -5.775e-7f);
}

static float apply_calibration(float ec25, const settings_t *s) {
  /* If multi-point calibration is configured, apply piecewise linear
   * interpolation */
  if (s->cal_points_count >= 2) {
    if (ec25 <= s->cal_raw[0]) {
      if (s->cal_raw[0] > 0.0f)
        return ec25 * (s->cal_std[0] / s->cal_raw[0]);
      return ec25;
    }
    for (uint8_t i = 0; i < s->cal_points_count - 1; i++) {
      if (ec25 <= s->cal_raw[i + 1]) {
        float span_raw = s->cal_raw[i + 1] - s->cal_raw[i];
        if (span_raw <= 0.0001f)
          span_raw = 0.0001f;
        float t = (ec25 - s->cal_raw[i]) / span_raw;
        return s->cal_std[i] + t * (s->cal_std[i + 1] - s->cal_std[i]);
      }
    }
    /* Extrapolate from the last segment */
    uint8_t last = s->cal_points_count - 1;
    float span_raw = s->cal_raw[last] - s->cal_raw[last - 1];
    if (span_raw <= 0.0001f)
      span_raw = 0.0001f;
    float slope = (s->cal_std[last] - s->cal_std[last - 1]) / span_raw;
    return s->cal_std[last] + slope * (ec25 - s->cal_raw[last]);
  } else if (s->cal_points_count == 1) {
    /* Single point standard ratio scaling */
    if (s->cal_raw[0] > 0.0f) {
      return ec25 * (s->cal_std[0] / s->cal_raw[0]);
    }
  }

  return ec25;
}

static float get_chan_val(const meter_reading_t *out, uint8_t chan) {
  switch (chan) {
  case CURRENT_CHAN_COND:
    return out->cond_us;
  case CURRENT_CHAN_TEMP:
    return out->temp_c;
  case CURRENT_CHAN_RES:
    return out->res_mohm_cm;
  case CURRENT_CHAN_TDS:
    return out->tds_ppm;
  case CURRENT_CHAN_SAL:
    return out->sal_ppt;
  default:
    return 0.0f;
  }
}

static float calc_ma(float val, float min_v, float max_v, uint8_t type) {
  float span = max_v - min_v;
  if (fabsf(span) <= 0.0001f)
    span = 1.0f;
  float norm = (val - min_v) / span;
  if (norm < 0.0f)
    norm = 0.0f;
  if (norm > 1.0f)
    norm = 1.0f;
  if (type == CURRENT_4_20MA)
    return 4.0f + (norm * 16.0f);
  else
    return norm * 20.0f;
}

static float get_metric(const meter_reading_t *r, uint8_t func) {
  switch (func) {
  case RELAY_FUNC_COND:
    return r->cond_us;
  case RELAY_FUNC_TEMP:
    return r->temp_c;
  case RELAY_FUNC_RES:
    return r->res_mohm_cm;
  case RELAY_FUNC_TDS:
    return r->tds_ppm;
  case RELAY_FUNC_SAL:
    return r->sal_ppt;
  default:
    return 0.0f;
  }
}

static uint8_t eval_alarm(float val, float on_val, float off_val,
                          uint8_t current_state) {
  if (on_val > off_val) {
    /* High Alarm: Turn ON when value >= on_val; turn OFF when value <= off_val
     */
    if (val >= on_val)
      return 1;
    if (val <= off_val)
      return 0;
    return current_state;
  } else {
    /* Low Alarm: Turn ON when value <= on_val; turn OFF when value >= off_val
     */
    if (val <= on_val)
      return 1;
    if (val >= off_val)
      return 0;
    return current_state;
  }
}

static float s_filtered_cond = 0.0f;
static float s_steady_cond = 0.0f;
static bool s_first_sample = true;

void meter_compute(float cell_ohms, float temp_ohms, const settings_t *s,
                   meter_reading_t *out) {
  if (!s || !out)
    return;
  memset(out, 0, sizeof(meter_reading_t));

  /* Check Simulation Mode */
  if (s->measure_mode == 1) /* 1 = Sim. Measure */
  {
    /* Simulates standard 1413 uS/cm solution at 25 C for bench testing */
    cell_ohms = (s->k_constant * 1000000.0f) / 1413.0f;
    temp_ohms = 10000.0f; /* 25.0 C for NTC10K */
  }

  out->cell_ohms = cell_ohms;
  out->temp_ohms = temp_ohms;

  /* Always compute real-time Sample Test Formula FVALUE = (a + b) * (c - d) */
  out->sample_fval = (s->sample_a + s->sample_b) * (s->sample_c - s->sample_d);

  /* Get Turbidity Reading from Digital RS-485 Sensor */
  out->turb_ntu = turbidity_get_ntu();
  out->fault_turb = turbidity_is_fault();

  /* 1. Temperature Calculation */
  float temp_c = 0.0f;
  if (s->temp_probe_type == TEMP_PROBE_MANUAL) {
    temp_c = s->manual_temp;
  } else if (s->temp_probe_type == TEMP_PROBE_PT1000) {
    temp_c = pt1000_to_c(temp_ohms);
    if (temp_c < -50.0f || temp_c > 200.0f) {
      out->fault_temp = true;
      temp_c = 0.0f; /* No sensor connected: show clean 0.0 C */
    }
  } else {
    temp_c = ntc10k_to_c(temp_ohms);
    if (temp_c < -30.0f || temp_c > 160.0f) {
      out->fault_temp = true;
      temp_c = 0.0f; /* No sensor connected: show clean 0.0 C */
    }
  }

  if (!out->fault_temp) {
    temp_c += s->temp_cal_offset;
  }
  out->temp_c = temp_c;
  out->temp_f = (out->fault_temp) ? 0.0f : ((temp_c * 1.8f) + 32.0f);

  /* 2. Open Circuit Detection (probe disconnected or dry in air) */
  if (cell_ohms <= 0.0f || cell_ohms > 10000000.0f) {
    out->fault_open = true;
    out->cond_raw_us = 0.0f;
    out->cond_us = 0.0f;
    out->res_mohm_cm = 0.0f;
    out->tds_ppm = 0.0f;
    out->sal_ppt = 0.0f;
    s_filtered_cond = 0.0f;
    s_steady_cond = 0.0f;
    s_first_sample = true;
    out->current1_ma =
        calc_ma(get_chan_val(out, s->curr1_chan), s->curr1_min_val,
                s->curr1_max_val, s->curr1_type);
    out->current2_ma =
        calc_ma(get_chan_val(out, s->curr2_chan), s->curr2_min_val,
                s->curr2_max_val, s->curr2_type);
    return;
  }

  /* 3. Short Circuit Detection */
  if (cell_ohms < 0.5f) {
    out->fault_short = true;
    out->out_of_range = true;
    s_filtered_cond = 0.0f;
    s_steady_cond = 0.0f;
    s_first_sample = true;
    out->current1_ma =
        calc_ma(get_chan_val(out, s->curr1_chan), s->curr1_min_val,
                s->curr1_max_val, s->curr1_type);
    out->current2_ma =
        calc_ma(get_chan_val(out, s->curr2_chan), s->curr2_min_val,
                s->curr2_max_val, s->curr2_type);
    return;
  }

  /* 3. Raw Conductivity: EC_t = K x (1 / R_cell) x 1e6 (uS/cm at sample
   * temperature) */
  float ec_t = (s->k_constant * 1000000.0f) / cell_ohms;
  out->cond_raw_us = ec_t;

  /* 4. Temperature Compensation to 25 C: EC25 = EC_t / (1 + coef/100 x (T -
   * 25)) */
  float comp_temp = (out->fault_temp) ? 25.0f : temp_c;
  float comp_factor = 1.0f + (s->temp_coef / 100.0f) * (comp_temp - 25.0f);
  if (comp_factor < 0.1f)
    comp_factor = 0.1f;
  float ec_25 = ec_t / comp_factor;

  /* 5. Calibration adjustment: EC = calibration points applied to EC25, then x
   * factor + offset */
  float cal_ec = apply_calibration(ec_25, s);
  float ec = (cal_ec * s->ec_factor) + s->ec_offset;
  if (ec < 0.0f)
    ec = 0.0f;

  /* 6. Display Speed: moving average of the readings over chosen buffer count
   * (Levels 2, 4, 8, 16, 32) */
  uint8_t filter_level = (s->display_speed >= 2) ? s->display_speed : 4;

  if (s_first_sample || s_filtered_cond <= 0.0f) {
    s_filtered_cond = ec;
    s_steady_cond = ec;
    s_first_sample = false;
  } else {
    /* Exponential moving average based on user configured Display Speed */
    s_filtered_cond += (ec - s_filtered_cond) / (float)filter_level;

    /* Deadband damping: if small ADC jitter is within +/- 0.4% or 0.2 uS/cm,
     * lock steady */
    float deadband = s_steady_cond * 0.004f;
    if (deadband < 0.2f)
      deadband = 0.2f;

    float diff = s_filtered_cond - s_steady_cond;
    if (diff > deadband || diff < -deadband) {
      s_steady_cond = s_filtered_cond;
    }
  }
  ec = s_steady_cond;
  if (ec < 0.05f)
    ec = 0.0f;
  out->cond_us = ec;

  /* 7. Resistivity: RES = 1 / EC (Mohm.cm, capped at 18) */
  if (ec > 0.0001f) {
    out->res_mohm_cm = 1.0f / ec;
    if (out->res_mohm_cm > 18.0f)
      out->res_mohm_cm = 18.0f; /* Capped at 18 */
  } else {
    out->res_mohm_cm = 0.0f; /* Clean 0.00 when no probe / zero signal */
  }

  /* 8. TDS: TDS = EC x TDS coefficient */
  out->tds_ppm = ec * s->tds_coef;
  if (out->tds_ppm > 10000.0f) {
    out->tds_ppm = 10000.0f;
  }

  /* 9. Salinity: SAL = EC in mS/cm x [factor, 0.50] (0~15 ppt) */
  out->sal_ppt = (ec / 1000.0f) * 0.50f;
  if (out->sal_ppt > 15.0f) {
    out->sal_ppt = 15.0f;
  }

  /* 10. Out of range: nominal range is 200 uS/cm for probe K <= 0.15 */
  float max_span = (s->k_constant <= 0.15f) ? 200.0f : 20000.0f;
  if (ec > max_span ||
      (s->last_measure_type == 2 && out->tds_ppm >= 10000.0f)) {
    out->out_of_range = true;
  }

  /* 11. Current Outputs calculation */
  out->current1_ma = calc_ma(get_chan_val(out, s->curr1_chan), s->curr1_min_val,
                             s->curr1_max_val, s->curr1_type);
  out->current2_ma = calc_ma(get_chan_val(out, s->curr2_chan), s->curr2_min_val,
                             s->curr2_max_val, s->curr2_type);
}

void relay_step(meter_reading_t *r, const settings_t *s, uint32_t now_ms) {
  if (!r || !s)
    return;

  /* Probe Fault Safe-State: if cell is open/short and safe-state option is 0,
   * keep relays OFF */
  if (r->fault_open || r->fault_short) {
    if (s->relay_safe_state == 0) {
      r->relay1_state = 0;
      r->relay2_state = 0;
      return;
    }
  }

  /* Relay 1 evaluation */
  if (s->relay1_func == RELAY_FUNC_WASH) {
    uint32_t period_ms = (uint32_t)s->relay1_wash_int_min * 60000UL;
    uint32_t active_ms = (uint32_t)s->relay1_wash_time_s * 1000UL;
    if (period_ms == 0)
      period_ms = 60000UL;
    uint32_t phase = now_ms % period_ms;
    r->relay1_state = (phase < active_ms) ? 1 : 0;
  } else {
    r->relay1_state =
        eval_alarm(get_metric(r, s->relay1_func), s->relay1_on_val,
                   s->relay1_off_val, r->relay1_state);
  }

  /* Relay 2 evaluation */
  if (s->relay2_func == RELAY_FUNC_WASH) {
    uint32_t period_ms = (uint32_t)s->relay2_wash_int_min * 60000UL;
    uint32_t active_ms = (uint32_t)s->relay2_wash_time_s * 1000UL;
    if (period_ms == 0)
      period_ms = 60000UL;
    uint32_t phase = now_ms % period_ms;
    r->relay2_state = (phase < active_ms) ? 1 : 0;
  } else {
    r->relay2_state =
        eval_alarm(get_metric(r, s->relay2_func), s->relay2_on_val,
                   s->relay2_off_val, r->relay2_state);
  }
}

/* Modbus CRC-16 (Polynomial 0xA001) */
uint16_t crc16_modbus(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint16_t)buf[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

/* Encode 32-bit float in CDAB (3412 byte-swap) format per PDF Appendix */
static void float_to_cdab(float val, uint8_t *out) {
  union {
    float f;
    uint8_t b[4];
  } u;
  u.f = val;
  /* In Little-Endian Cortex-M0+: u.b[0]=D, u.b[1]=C, u.b[2]=B, u.b[3]=A */
  /* Desired wire order CDAB: u.b[1], u.b[0], u.b[3], u.b[2] */
  out[0] = u.b[1]; /* C */
  out[1] = u.b[0]; /* D */
  out[2] = u.b[3]; /* A */
  out[3] = u.b[2]; /* B */
}

uint16_t modbus_handle(const uint8_t *rx, uint16_t rx_len, uint8_t *tx,
                       const meter_reading_t *r, const settings_t *s) {
  if (!rx || !tx || !r || !s || rx_len < 8)
    return 0;

  /* Verify CRC16 */
  uint16_t rx_crc = (uint16_t)rx[rx_len - 2] | ((uint16_t)rx[rx_len - 1] << 8);
  if (crc16_modbus(rx, rx_len - 2) != rx_crc)
    return 0;

  /* Verify Slave Address */
  if (rx[0] != s->slave_addr && rx[0] != 0)
    return 0;

  uint8_t fc = rx[1];
  if (fc != 0x04) /* Function Code 04: Read Input Registers */
  {
    tx[0] = rx[0];
    tx[1] = fc | 0x80;
    tx[2] = 0x01; /* Illegal Function */
    uint16_t crc = crc16_modbus(tx, 3);
    tx[3] = (uint8_t)(crc & 0xFF);
    tx[4] = (uint8_t)(crc >> 8);
    return 5;
  }

  uint16_t start_reg = ((uint16_t)rx[2] << 8) | rx[3];
  uint16_t reg_count = ((uint16_t)rx[4] << 8) | rx[5];

  if (reg_count < 1 || reg_count > 16 || (start_reg + reg_count) > 16) {
    tx[0] = rx[0];
    tx[1] = fc | 0x80;
    tx[2] = 0x02; /* Illegal Data Address */
    uint16_t crc = crc16_modbus(tx, 3);
    tx[3] = (uint8_t)(crc & 0xFF);
    tx[4] = (uint8_t)(crc >> 8);
    return 5;
  }

  /* Build register memory map: 16 registers (32 bytes) */
  uint8_t reg_bytes[32];
  memset(reg_bytes, 0, sizeof(reg_bytes));

  /* Guarantee 0.0 values across all real-time registers if probe/data is
   * missing */
  float m_temp =
      (r->cell_ohms <= 0.0f || r->temp_c < -40.0f) ? 0.0f : r->temp_c;
  float m_cond =
      (r->cell_ohms <= 0.0f || r->cond_us <= 0.0001f) ? 0.0f : r->cond_us;
  float m_res =
      (r->cell_ohms <= 0.0f || r->cond_us <= 0.0001f) ? 0.0f : r->res_mohm_cm;
  float m_tds =
      (r->cell_ohms <= 0.0f || r->cond_us <= 0.0001f) ? 0.0f : r->tds_ppm;
  float m_sal =
      (r->cell_ohms <= 0.0f || r->cond_us <= 0.0001f) ? 0.0f : r->sal_ppt;

  /* Reg 0..1: Temperature (C) in CDAB */
  float_to_cdab(m_temp, &reg_bytes[0]);

  /* Reg 2..3: Conductivity (uS/cm) in CDAB */
  float_to_cdab(m_cond, &reg_bytes[4]);

  /* Reg 4..5: Resistivity (M-Ohm*cm) in CDAB */
  float_to_cdab(m_res, &reg_bytes[8]);

  /* Reg 6..7: Sample Test Formula FVALUE in CDAB */
  float_to_cdab(r->sample_fval, &reg_bytes[12]);

  /* Reg 8..9: Turbidity (NTU) in CDAB */
  float_to_cdab(r->turb_ntu, &reg_bytes[16]);

  /* Reg 12..13: TDS (ppm) in CDAB */
  float_to_cdab(m_tds, &reg_bytes[24]);

  /* Reg 14..15: Salinity (ppt) in CDAB */
  float_to_cdab(m_sal, &reg_bytes[28]);

  uint8_t byte_count = (uint8_t)(reg_count * 2);
  tx[0] = rx[0];
  tx[1] = 0x04;
  tx[2] = byte_count;

  uint16_t offset = (uint16_t)(start_reg * 2);
  for (uint8_t i = 0; i < byte_count; i++) {
    tx[3 + i] = reg_bytes[offset + i];
  }

  uint16_t crc = crc16_modbus(tx, 3 + byte_count);
  tx[3 + byte_count] = (uint8_t)(crc & 0xFF);
  tx[4 + byte_count] = (uint8_t)(crc >> 8);

  return (uint16_t)(5 + byte_count);
}
