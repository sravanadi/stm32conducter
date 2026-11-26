/**
 * @file    meter_core.h
 * @brief   Core calculation engine, data models, calibration, and Modbus RTU
 *          for Mirrant TDS 4002 Conductivity/TDS/Resistivity/Salinity meter.
 */

#ifndef __METER_CORE_H
#define __METER_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METER_SETTINGS_MAGIC        0x54445334  /* 'TDS4' */
#define METER_SETTINGS_VERSION      1

/* Measurement Units */
typedef enum {
    UNIT_AUTO   = 0,
    UNIT_US_CM  = 1,
    UNIT_MS_CM  = 2
} display_unit_t;

/* Measurement Display Modes (cycled with SW2 ->) */
typedef enum {
    MODE_COND = 0,   /* Conductivity */
    MODE_RES  = 1,   /* Resistivity */
    MODE_TDS  = 2,   /* Total Dissolved Solids */
    MODE_SAL  = 3,   /* Salinity */
    MODE_TURB = 4,   /* Turbidity in NTU */
    MODE_FVAL = 5    /* Sample Test Formula: (a+b)(c-d) */
} measure_mode_t;

/* Temperature Probe Types */
typedef enum {
    TEMP_PROBE_NTC10K = 0,
    TEMP_PROBE_PT1000 = 1,
    TEMP_PROBE_MANUAL = 2
} temp_probe_type_t;

/* Temperature Units */
typedef enum {
    TEMP_UNIT_C = 0,
    TEMP_UNIT_F = 1
} temp_unit_t;

/* Relay Control Function Modes */
typedef enum {
    RELAY_FUNC_COND = 0,
    RELAY_FUNC_TEMP = 1,
    RELAY_FUNC_WASH = 2,
    RELAY_FUNC_RES  = 3,
    RELAY_FUNC_TDS  = 4,
    RELAY_FUNC_SAL  = 5
} relay_func_t;

/* Wash State */
typedef enum {
    WASH_STATE_HOLD     = 0,
    WASH_STATE_CONSTANT = 1
} wash_state_t;

/* Current Output Types */
typedef enum {
    CURRENT_4_20MA = 0,
    CURRENT_0_20MA = 1
} current_type_t;

/* Current Output Channel Association */
typedef enum {
    CURRENT_CHAN_COND = 0,
    CURRENT_CHAN_TEMP = 1,
    CURRENT_CHAN_RES  = 2,
    CURRENT_CHAN_TDS  = 3,
    CURRENT_CHAN_SAL  = 4
} current_channel_t;

/* RS-485 Parity */
typedef enum {
    PARITY_NONE = 0,
    PARITY_ODD  = 1,
    PARITY_EVEN = 2
} comm_parity_t;

/* RS-485 Stop Bits */
typedef enum {
    STOP_1_BIT = 0,
    STOP_2_BIT = 1
} comm_stop_bits_t;

/* System Hardware Settings Structure (Saved in Flash Page 31) */
typedef struct {
    uint32_t magic;
    uint16_t version;

    /* 1. Probe Settings */
    float    k_constant;         /* Cell constant K (default 1.000 1/cm) */
    float    ec_offset;          /* EC offset in uS/cm */
    float    ec_factor;          /* EC factor (default 1.000) */
    float    cal_std[5];         /* Calibration standard values */
    float    cal_raw[5];         /* Measured raw values at calibration */
    uint8_t  cal_points_count;   /* 1 to 5 active points (0 = none) */
    float    temp_coef;          /* Temp compensation coeff in %/C (default 2.00) */
    float    tds_coef;           /* TDS coeff (default 0.500) */
    uint8_t  display_unit;       /* display_unit_t: AUTO, uS/cm, mS/cm */

    /* 2. Temperature Settings */
    uint8_t  temp_probe_type;    /* temp_probe_type_t */
    uint8_t  temp_unit;          /* temp_unit_t: C or F */
    float    temp_cal_offset;    /* Temperature calibration offset in C */
    float    manual_temp;        /* Manual temperature fallback (default 25.0 C) */

    /* 3. Alarm / Relay Settings */
    /* Relay I */
    uint8_t  relay1_func;        /* relay_func_t */
    float    relay1_on_val;
    float    relay1_off_val;
    uint16_t relay1_wash_int_min;/* Wash interval in minutes */
    uint16_t relay1_wash_time_s; /* Wash duration in seconds */
    uint8_t  relay1_wash_state;  /* wash_state_t */

    /* Relay II */
    uint8_t  relay2_func;
    float    relay2_on_val;
    float    relay2_off_val;
    uint16_t relay2_wash_int_min;
    uint16_t relay2_wash_time_s;
    uint8_t  relay2_wash_state;

    /* 4. Current Output Settings */
    uint8_t  curr1_type;         /* current_type_t: 4-20mA / 0-20mA */
    uint8_t  curr1_chan;         /* current_channel_t */
    float    curr1_max_val;
    float    curr1_min_val;

    uint8_t  curr2_type;
    uint8_t  curr2_chan;
    float    curr2_max_val;
    float    curr2_min_val;

    /* 5. Communication Settings */
    uint8_t  slave_addr;         /* Modbus Slave Address (1..247, default 1) */
    uint32_t baud_rate;          /* 4800, 9600, 19200, 38400, 57600, 115200 */
    uint8_t  parity;             /* comm_parity_t */
    uint8_t  stop_bits;          /* comm_stop_bits_t */

    /* 6. System Settings */
    uint8_t  display_speed;      /* Buffer filter level: 2, 4, 8, 16, 32 */
    uint8_t  backlight_mode;     /* 0 = Scanty power (timeout), 1 = Always ON, 2 = Always OFF */
    uint8_t  backlight_timeout;  /* 1, 5, or 10 minutes */
    uint8_t  backlight_state;    /* ON or OFF */
    uint8_t  contrast;           /* ST7565 electronic volume (0..63) */
    uint16_t password;           /* Menu password (default 0000) */

    /* 7. Maintenance / Diagnostic */
    uint8_t  measure_mode;       /* 0 = Real Probe, 1 = Sim Measure (1413 uS/cm standard) */
    uint8_t  relay_safe_state;   /* Safe state during probe fault: 0 = All OFF */
    uint8_t  last_measure_type;  /* 0=COND, 1=RES, 2=TDS, 3=SAL, 4=FVAL */
    uint8_t  last_sub_bar_mode;  /* 0=Mode 1 (Temp & K), 1=Mode 2 (TDS & Salinity) */

    /* 8. Sample Test Settings: Formula (a+b)(c-d) */
    float    sample_a;           /* Input field a */
    float    sample_b;           /* Input field b */
    float    sample_c;           /* Input field c */
    float    sample_d;           /* Input field d */

    uint16_t crc;                /* Checksum over preceding bytes */
} settings_t;

/* Live Calculated Output Reading */
typedef struct {
    float   cell_ohms;           /* Measured cell resistance in Ohms */
    float   temp_ohms;           /* Measured RTD/NTC resistance */
    float   temp_c;              /* Temperature in Celsius */
    float   temp_f;              /* Temperature in Fahrenheit */
    float   cond_us;             /* Temp-compensated conductivity in uS/cm (at 25C) */
    float   cond_raw_us;         /* Uncompensated conductivity in uS/cm */
    float   res_mohm_cm;         /* Resistivity in M-Ohm * cm */
    float   tds_ppm;             /* Total Dissolved Solids in ppm */
    float   sal_ppt;             /* Salinity in ppt */
    float   turb_ntu;            /* Turbidity reading in NTU (0.0 to 400.0 NTU) */
    float   sample_fval;         /* Calculated FVALUE = (a + b) * (c - d) */

    float   current1_ma;         /* Calculated mA output 1 */
    float   current2_ma;         /* Calculated mA output 2 */

    uint8_t relay1_state;        /* 1 = Energized, 0 = De-energized */
    uint8_t relay2_state;

    bool    fault_open;          /* Probe disconnected */
    bool    fault_short;         /* Probe electrodes shorted */
    bool    fault_temp;          /* Temperature sensor missing */
    bool    fault_turb;          /* Turbidity sensor offline */
    bool    out_of_range;        /* Measurement exceeds physical span */
} meter_reading_t;

/* Public API Prototypes */
void meter_core_default_settings(settings_t *s);
void meter_compute(float cell_ohms, float temp_ohms, const settings_t *s, meter_reading_t *out);
float ntc10k_to_c(float ohms);
float pt1000_to_c(float ohms);
void relay_step(meter_reading_t *r, const settings_t *s, uint32_t now_ms);
const meter_reading_t *meter_data(void);

/* Modbus Support */
uint16_t crc16_modbus(const uint8_t *buf, uint16_t len);
uint16_t modbus_handle(const uint8_t *rx_buf, uint16_t rx_len, uint8_t *tx_buf,
                       const meter_reading_t *reading, const settings_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* __METER_CORE_H */
