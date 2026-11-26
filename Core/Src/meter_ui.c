/**
 * @file    meter_ui.c
 * @brief   User Interface engine for Mirrant TDS 4002 Conductivity Meter.
 *          Implements:
 *            - Section 4.2 Measurement Screen:
 *                * Page 0 (inverted): R1 and R2 graphical switch symbols
 *                * Page 2: Measure type (left) and unit (right) in CAPITAL LETTERS
 *                * Pages 3-5: Centered 16x24 large digits, out-of-range "!" warning
 *                * Pages 6-7 (inverted): Sub-bar 1 (Temp & K) or Sub-bar 2 (I1 & I2 currents)
 *            - Physical Buttons (Left-to-Right):
 *                * SW4 (PB4): MENU / RETURN (Short: open/back, Long 1s: measurement mode)
 *                * SW3 (PB3): DOWN (cursor down / digit minus / sign toggle / sub-bar switch)
 *                * SW2 (PB2): RIGHT (cursor up / next digit / measure type)
 *                * SW1 (PB1): ENTER (select / save / status screen)
 *            - Full 7-Category Menu and Submenu Tree matching Flowchart Parts 1 & 2:
 *                * Digit-by-digit editor with clamping and "OUT OF RANGE" warning
 *                * Option lists marking saved choice with "*"
 *                * Dynamic conditional Relay branching (Control vs. Wash)
 *                * Point-by-point calibration wizard with ascending order validation
 *                * Live contrast editor and real-time hardware relay test
 *                * Flicker-free differential redraw at most 4 times a second (250 ms)
 */

#include "meter_ui.h"
#include "lcd_st7565.h"
#include "font8x16.h"
#include "buttons.h"
#include "board_pins.h"
#include "meter_flash.h"
#include "meter_comm.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    UI_MEASURE = 0,
    UI_STATUS_SCREEN,    /* Section 4.1: Status screen */
    UI_MENU_L1,          /* 7 Main Categories */
    UI_MENU_L2,          /* Sub-menu inside category */
    UI_MENU_L3,          /* Level 3 menu (Probe CAL, Relays, Current, Backlight, Test) */
    UI_MENU_L4,          /* Level 4 menu (Relay ON/OFF, Scanty times) */
    UI_OPTION_PICKER,    /* Selecting option from a list */
    UI_NUMERIC_EDITOR,   /* Interactive digit-by-digit editor */
    UI_CONTRAST_EDITOR,  /* Interactive contrast adjustment */
    UI_INFO_SCREEN,      /* Software version display */
    UI_CAL_WIZARD,       /* Calibration screen with stability check */
    UI_SAMPLE_PREVIEW    /* Sample test formula preview */
} ui_screen_t;

static ui_screen_t       s_screen = UI_MEASURE;
static ui_screen_t       s_prev_screen = UI_MENU_L2;
static settings_t       *s_cfg    = NULL;
static bool              s_force_full_redraw = true;

/* Persistent measurement mode selections */
static uint8_t s_measure_type = 0; /* 0=COND, 1=RES, 2=TDS, 3=SAL, 4=TURB */
static uint8_t s_sub_bar_mode = 0; /* 0=Temp&K, 1=Turbidity&Temp, 2=TDS&SAL, 3=Currents I1&I2 */

/* Wash state frozen value cache */
static char s_frozen_val_str[16] = "";

/* Navigation cursors */
static uint8_t s_l1_cursor = 0;
static uint8_t s_l2_cursor = 0;
static uint8_t s_l3_cursor = 0;
static uint8_t s_l4_cursor = 0;

/* Numeric editor state */
typedef enum {
    EDIT_TYPE_FLOAT = 0,
    EDIT_TYPE_UINT8,
    EDIT_TYPE_UINT16
} edit_type_t;

static edit_type_t  s_edit_type = EDIT_TYPE_FLOAT;
static void        *s_edit_target_ptr = NULL;
static char         s_edit_buf[16];
static uint8_t      s_edit_cursor = 0;
static uint8_t      s_edit_len = 0;
static const char  *s_edit_title = "";
static float        s_edit_min = 0.0f;
static float        s_edit_max = 99999.0f;
static bool         s_edit_has_sign = false;
static uint8_t      s_edit_cal_point = 0xFF; /* Used when editing Point CAL standard */

/* Option picker state */
static uint8_t     *s_opt_target = NULL;
static const char **s_opt_strings = NULL;
static uint8_t      s_opt_count = 0;
static uint8_t      s_opt_cursor = 0;
static const char  *s_opt_title = "";
typedef void (*picker_cb_t)(uint8_t selected_idx);
static picker_cb_t  s_opt_callback = NULL;

/* Real-time relay test override: -1=auto/normal, 0=force OFF, 1=force ON */
static int8_t       s_relay_override[2] = { -1, -1 };

int8_t meter_ui_relay_test_state(uint8_t relay_idx)
{
    if (relay_idx < 2) return s_relay_override[relay_idx];
    return -1;
}

/* Contrast editor temporary backup */
static uint8_t      s_contrast_orig = 12;

/* Calibration wizard state */
static uint8_t      s_cal_point_idx = 0;
static float        s_cal_last_readings[8];
static uint8_t      s_cal_sample_idx = 0;

/* Backlight activity timer */
static uint32_t     s_last_btn_activity_tick = 0;

/* -------------------------------------------------------------------------- */
/* Main Menu Names (Capital letters, max 21 chars)                            */
/* -------------------------------------------------------------------------- */
#define MENU_L1_COUNT 7
static const char *s_menu_l1_names[MENU_L1_COUNT] = {
    "1. PROBE SET",
    "2. TEMPERATURE",
    "3. ALARM SET",
    "4. CURRENT SET",
    "5. COMM. SET",
    "6. SYSTEM SET",
    "7. TEST/MAINT"
};

/* Level 2 Menus */
static const char *s_sub_probe[] = {
    "1. K CONSTANT SET",
    "2. PROBE CAL",
    "3. TEMP. COEF.",
    "4. TDS COEF.",
    "5. DISPLAY UNIT"
};

static const char *s_sub_temp[] = {
    "1. PROBE TYPE",
    "2. TEMP. UNIT",
    "3. TEMP. CAL",
    "4. RESET TEMP. CAL"
};

static const char *s_sub_alarm[] = {
    "1. RELAY I",
    "2. RELAY II"
};

static const char *s_sub_current[] = {
    "1. CURRENT I",
    "2. CURRENT II"
};

static const char *s_sub_comm[] = {
    "1. SLAVE ADDRESS",
    "2. BAUD RATE",
    "3. VERIFY BIT",
    "4. STOP BIT"
};

static const char *s_sub_system[] = {
    "1. DISPLAY SPEED",
    "2. BACKLIGHT",
    "3. CONTRAST SET",
    "4. SOFTWARE",
    "5. PASSWORD"
};

static const char *s_sub_test[] = {
    "1. CURRENT CAL",
    "2. RELAY TEST",
    "3. CURRENT TEST",
    "4. MEASURE MODE",
    "5. RESTORE DEFAULTS"
};

/* Level 3 Submenus */
static const char *s_sub_cal_items[] = {
    "1. EC OFFSET",
    "2. EC FACTOR",
    "3. 1ST POINT CAL",
    "4. 2ND POINT CAL",
    "5. 3RD POINT CAL",
    "6. 4TH POINT CAL",
    "7. 5TH POINT CAL",
    "8. RESET CAL"
};

static const char *s_sub_relay_ctrl_items[] = {
    "1. FUNCTION SET",
    "2. SET ON VALUE",
    "3. SET OFF VALUE"
};

static const char *s_sub_relay_wash_items[] = {
    "1. FUNCTION SET",
    "2. WASH INTERVAL",
    "3. WASH TIME",
    "4. WASH STATE"
};

static const char *s_sub_curr_items[] = {
    "1. OUTPUT TYPE",
    "2. CHANNEL SELECT",
    "3. MAX. VALUE",
    "4. MIN. VALUE"
};

static const char *s_sub_backlight_items[] = {
    "1. SCANTY POWER",
    "2. ALWAYS ON",
    "3. ALWAYS OFF"
};

static const char *s_sub_test_two_relays[] = {
    "1. RELAY I",
    "2. RELAY II"
};

static const char *s_sub_test_two_currs[] = {
    "1. CURRENT I",
    "2. CURRENT II"
};

/* Level 4 Submenus */
static const char *s_sub_scanty_times[] = {
    "1. 01 MINUTE",
    "2. 05 MINUTE",
    "3. 10 MINUTE"
};

static const char *s_sub_relay_onoff[] = {
    "1. TURN-ON",
    "2. TURN-OFF"
};

static const char *s_sub_curr_cal_pts[] = {
    "1. CAL 4mA",
    "2. CAL 20mA"
};

/* Option Picker Lists */
static const char *s_opt_bauds[] = {
    "1. 4800 BPS",
    "2. 9600 BPS",
    "3. 19200 BPS",
    "4. 38400 BPS",
    "5. 57600 BPS",
    "6. 115200 BPS"
};
static const uint32_t s_baud_table[6] = { 4800, 9600, 19200, 38400, 57600, 115200 };
static uint8_t s_baud_choice_idx = 1;

static const char *s_opt_dspeed[] = {
    "1. BUFFERS LEVEL 2",
    "2. BUFFERS LEVEL 4",
    "3. BUFFERS LEVEL 8",
    "4. BUFFERS LEVEL 16",
    "5. BUFFERS LEVEL 32"
};
static const uint8_t s_dspeed_table[5] = { 2, 4, 8, 16, 32 };
static uint8_t s_dspeed_choice_idx = 1;

static const char *s_opt_parity[] = { "1. NONE", "2. ODD", "3. EVEN" };
static const char *s_opt_stops[]  = { "1. 1 BIT", "2. 2 BIT" };
static const char *s_opt_probes[] = { "1. NTC10K", "2. PT1000", "3. MANUAL" };
static const char *s_opt_tunits[] = { "1. xxx.xC", "2. xxx.xF" };
static const char *s_opt_units[]  = { "1. AUTO", "2. US/CM", "3. MS/CM" };
static const char *s_opt_confirm[] = { "1. CANCEL", "2. OK" };
static uint8_t s_confirm_choice_idx = 0;

static const char *s_opt_rfuncs[] = {
    "1. COND CONTROL",
    "2. TEMP. CONTROL",
    "3. WASH RELAY",
    "4. RES CONTROL",
    "5. TDS CONTROL",
    "6. SAL CONTROL"
};

static const char *s_opt_wstates[] = { "1. HOLD", "2. CONSTANT" };
static const char *s_opt_ctypes[]  = { "1. 4-20mA", "2. 0-20mA" };
static const char *s_opt_cchans[]  = {
    "1. COND CHANNEL",
    "2. TEMP. CHANNEL",
    "3. RES CHANNEL",
    "4. TDS CHANNEL",
    "5. SAL CHANNEL"
};

static const char *s_opt_meas_modes[] = {
    "1. PROBE MEASURE",
    "2. SIM. MEASURE"
};

/* Forward declarations */
static void render_screen(void);
static void open_float_editor(const char *title, float *target, const char *format, float min_v, float max_v);
static void open_uint8_editor(const char *title, uint8_t *target, const char *format, uint8_t min_v, uint8_t max_v);
static void open_uint16_editor(const char *title, uint16_t *target, const char *format, uint16_t min_v, uint16_t max_v);
static void open_option_picker(const char *title, const char *options[], uint8_t count, uint8_t *target);
static void open_option_picker_cb(const char *title, const char *options[], uint8_t count, uint8_t *target, picker_cb_t cb);
static void start_cal_wizard(uint8_t point_idx);

/* ========================================================================== */
/*                             INITIALIZATION                                 */
/* ========================================================================== */

void meter_ui_init(settings_t *s, meter_reading_t *r)
{
    (void)r;
    s_cfg  = s;
    s_screen = UI_MEASURE;

    if (s_cfg->last_measure_type < 5)
    {
        s_measure_type = s_cfg->last_measure_type;
    }
    else
    {
        s_measure_type = 0;
    }

    if (s_cfg->last_sub_bar_mode < 4)
    {
        s_sub_bar_mode = s_cfg->last_sub_bar_mode;
    }
    else
    {
        s_sub_bar_mode = 0;
    }

    /* Initialize standard calibration values if empty */
    if (s_cfg->cal_std[0] <= 0.0f) s_cfg->cal_std[0] = 1413.0f;
    if (s_cfg->cal_std[1] <= 0.0f) s_cfg->cal_std[1] = 84.0f;
    if (s_cfg->cal_std[2] <= 0.0f) s_cfg->cal_std[2] = 12880.0f;
    if (s_cfg->cal_std[3] <= 0.0f) s_cfg->cal_std[3] = 500.0f;
    if (s_cfg->cal_std[4] <= 0.0f) s_cfg->cal_std[4] = 3000.0f;

    s_last_btn_activity_tick = HAL_GetTick();
    if (s_cfg->backlight_mode == 2)
    {
        HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_SET);
    }
    s_force_full_redraw = true;
    lcd_clear();
    render_screen();
}

/* ========================================================================== */
/*           LIGHTWEIGHT INTEGER-BASED FLOAT FORMATTER & PARSER               */
/* ========================================================================== */

static void ftoa_custom(float val, int decimals, int min_int_digits, bool show_sign, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    memset(out, 0, out_size);

    char temp[32];
    memset(temp, 0, sizeof(temp));
    size_t pos = 0;

    bool negative = false;
    if (val < 0.0f)
    {
        negative = true;
        val = -val;
    }

    if (show_sign)
    {
        temp[pos++] = negative ? '-' : '+';
    }
    else if (negative)
    {
        temp[pos++] = '-';
    }

    /* Rounding */
    float round_val = 0.5f;
    for (int i = 0; i < decimals; i++) round_val /= 10.0f;
    val += round_val;

    uint32_t ipart = (uint32_t)val;
    float fpart = val - (float)ipart;

    /* Format integer part in reverse */
    char int_buf[16];
    int int_len = 0;
    if (ipart == 0)
    {
        int_buf[int_len++] = '0';
    }
    else
    {
        uint32_t t = ipart;
        while (t > 0 && int_len < 15)
        {
            int_buf[int_len++] = (char)('0' + (t % 10));
            t /= 10;
        }
    }
    while (int_len < min_int_digits && int_len < 15)
    {
        int_buf[int_len++] = '0';
    }
    for (int i = int_len - 1; i >= 0 && pos < (sizeof(temp) - 1); i--)
    {
        temp[pos++] = int_buf[i];
    }

    if (decimals > 0 && pos < (sizeof(temp) - 1))
    {
        temp[pos++] = '.';
        for (int i = 0; i < decimals && pos < (sizeof(temp) - 1); i++)
        {
            fpart *= 10.0f;
            int digit = (int)fpart;
            if (digit > 9) digit = 9;
            if (digit < 0) digit = 0;
            temp[pos++] = (char)('0' + digit);
            fpart -= (float)digit;
        }
    }
    temp[pos] = '\0'; /* CRITICAL: explicit null terminator */

    size_t i = 0;
    while (i < pos && temp[i] != '\0' && i < (out_size - 1))
    {
        out[i] = temp[i];
        i++;
    }
    out[i] = '\0';
}

static bool atof_custom(const char *s, float *out)
{
    if (!s || !out) return false;
    while (*s == ' ') s++;

    bool negative = false;
    if (*s == '-') { negative = true; s++; }
    else if (*s == '+') { s++; }

    float ipart = 0.0f;
    while (*s >= '0' && *s <= '9')
    {
        ipart = (ipart * 10.0f) + (float)(*s - '0');
        s++;
    }

    float fpart = 0.0f;
    float div = 1.0f;
    if (*s == '.')
    {
        s++;
        while (*s >= '0' && *s <= '9')
        {
            fpart = (fpart * 10.0f) + (float)(*s - '0');
            div *= 10.0f;
            s++;
        }
    }

    float res = ipart + (fpart / div);
    if (negative) res = -res;
    *out = res;
    return true;
}

static void format_stable_measure_value(float val, uint8_t measure_type, uint8_t display_unit, char *buf, size_t max_len)
{
    if (!buf || max_len == 0) return;

    static uint8_t s_cond_dec = 1;
    static uint8_t s_tds_dec = 1;

    if (val <= 0.0001f)
    {
        /* Clean zero: stable fixed representation */
        if (measure_type == 0 && (display_unit == UNIT_MS_CM || (display_unit == UNIT_AUTO && val >= 10000.0f)))
        {
            snprintf(buf, max_len, "0.00");
        }
        else if (measure_type == 1 || measure_type == 3) /* RES or SAL */
        {
            snprintf(buf, max_len, "0.00");
        }
        else /* COND in uS/cm or TDS in ppm */
        {
            snprintf(buf, max_len, "0");
        }
        return;
    }

    if (measure_type == 0) /* COND */
    {
        if (display_unit == UNIT_MS_CM || (display_unit == UNIT_AUTO && val >= 10000.0f))
        {
            /* In mS/cm: 0.00 to 20.00 mS/cm (2 decimal places) */
            ftoa_custom(val / 1000.0f, 2, 1, false, buf, max_len);
        }
        else
        {
            /* In uS/cm: 0 to 9999 uS/cm with hysteresis to prevent digit jumping */
            if (s_cond_dec == 2)
            {
                if (val > 10.5f) s_cond_dec = 1;
            }
            else if (s_cond_dec == 1)
            {
                if (val < 9.5f) s_cond_dec = 2;
                else if (val > 1005.0f) s_cond_dec = 0;
            }
            else /* s_cond_dec == 0 */
            {
                if (val < 995.0f) s_cond_dec = 1;
            }

            ftoa_custom(val, s_cond_dec, 1, false, buf, max_len);
        }
    }
    else if (measure_type == 1) /* RES */
    {
        /* Resistivity: 0.001 to 18.00 Mohm.cm (Min resolution 0.001 per datasheet) */
        ftoa_custom(val, (val < 1.0f) ? 3 : 2, 1, false, buf, max_len);
    }
    else if (measure_type == 2) /* TDS */
    {
        /* TDS in ppm: 0 to 10000 ppm with hysteresis */
        if (s_tds_dec == 2)
        {
            if (val > 10.5f) s_tds_dec = 1;
        }
        else if (s_tds_dec == 1)
        {
            if (val < 9.5f) s_tds_dec = 2;
            else if (val > 1005.0f) s_tds_dec = 0;
        }
        else /* s_tds_dec == 0 */
        {
            if (val < 995.0f) s_tds_dec = 1;
        }

        ftoa_custom(val, s_tds_dec, 1, false, buf, max_len);
    }
    else if (measure_type == 3) /* SAL */
    {
        /* Salinity: 0.00 to 15.00 ppt (2 decimal places) */
        ftoa_custom(val, 2, 1, false, buf, max_len);
    }
    else if (measure_type == 4) /* TURB */
    {
        /* Turbidity: 0.0 to 400.0 NTU (1 decimal place) */
        ftoa_custom(val, 1, 1, false, buf, max_len);
    }
    else /* measure_type == 5: FVAL */
    {
        /* FVALUE = (a + b) * (c - d) */
        float abs_v = (val < 0.0f) ? -val : val;
        if (abs_v < 100.0f)
        {
            ftoa_custom(val, 2, 1, false, buf, max_len);
        }
        else if (abs_v < 1000.0f)
        {
            ftoa_custom(val, 1, 1, false, buf, max_len);
        }
        else
        {
            ftoa_custom(val, 0, 1, false, buf, max_len);
        }
    }
}

static void format_4sig_digits(float val, char *buf, size_t max_len)
{
    float abs_v = (val < 0.0f) ? -val : val;
    if (abs_v < 10.0f)
    {
        ftoa_custom(val, 3, 1, false, buf, max_len);
    }
    else if (abs_v < 100.0f)
    {
        ftoa_custom(val, 2, 1, false, buf, max_len);
    }
    else if (abs_v < 1000.0f)
    {
        ftoa_custom(val, 1, 1, false, buf, max_len);
    }
    else
    {
        ftoa_custom(val, 0, 1, false, buf, max_len);
    }
}

/* ========================================================================== */
/*                    MEASUREMENT SCREEN (SECTION 4.2)                        */
/* ========================================================================== */

static uint8_t s_prev_r1_state = 0xFF;
static uint8_t s_prev_r2_state = 0xFF;
static char    s_prev_type_str[8] = "";
static char    s_prev_unit_str[10] = "";
static char    s_prev_val_str[16] = "";
static bool    s_prev_warning = false;
static char    s_prev_sub_str[32] = "";

static void render_measure_screen(void)
{
    const meter_reading_t *r = meter_data();
    if (!r) return;

    /* 1. TOP BAR (PAGES 0-1, ROWS 0-15, INVERTED 7x16 FONT) */
    if (s_force_full_redraw || r->relay1_state != s_prev_r1_state || r->relay2_state != s_prev_r2_state)
    {
        s_prev_r1_state = r->relay1_state;
        s_prev_r2_state = r->relay2_state;

        char top_bar_buf[17];
        char r1_buf[8];
        char r2_buf[8];
        snprintf(r1_buf, sizeof(r1_buf), "R1:%s", r->relay1_state ? "ON" : "OFF");
        snprintf(r2_buf, sizeof(r2_buf), "R2:%s", r->relay2_state ? "ON" : "OFF");
        int spaces = 16 - (int)strlen(r1_buf) - (int)strlen(r2_buf);
        if (spaces < 0) spaces = 0;
        snprintf(top_bar_buf, sizeof(top_bar_buf), "%s%*s%s", r1_buf, spaces, "", r2_buf);

        lcd_print_bar(0, top_bar_buf);
    }

    /* 2. PAGE 2 (ROWS 16-23): MEASURE TYPE & UNIT */
    const char *type_str = "COND";
    const char *unit_str = "US/CM";
    char raw_val_str[16] = "";
    bool is_out_of_range = false;

    if (s_measure_type == 0) /* COND */
    {
        type_str = "COND";
        float cond = r->cond_us;
        if (cond > 20000.0f || r->out_of_range)
        {
            is_out_of_range = true;
        }

        /* AUTO unit: uS/cm below 10 mS/cm, otherwise mS/cm */
        if (s_cfg->display_unit == UNIT_MS_CM || (s_cfg->display_unit == UNIT_AUTO && cond >= 10000.0f))
        {
            unit_str = "MS/CM";
        }
        else
        {
            unit_str = "US/CM";
        }
        format_stable_measure_value(cond, 0, s_cfg->display_unit, raw_val_str, sizeof(raw_val_str));
    }
    else if (s_measure_type == 1) /* RES */
    {
        type_str = "RES";
        unit_str = "MOHM.CM";
        format_stable_measure_value(r->res_mohm_cm, 1, 0, raw_val_str, sizeof(raw_val_str));
    }
    else if (s_measure_type == 2) /* TDS */
    {
        type_str = "TDS";
        unit_str = "PPM";
        if (r->tds_ppm >= 10000.0f || r->out_of_range)
        {
            is_out_of_range = true;
        }
        format_stable_measure_value(r->tds_ppm, 2, 0, raw_val_str, sizeof(raw_val_str));
    }
    else if (s_measure_type == 3) /* SAL */
    {
        type_str = "SAL";
        unit_str = "PPT";
        if (r->sal_ppt >= 15.0f || r->out_of_range)
        {
            is_out_of_range = true;
        }
        format_stable_measure_value(r->sal_ppt, 3, 0, raw_val_str, sizeof(raw_val_str));
    }
    else if (s_measure_type == 4) /* TURB */
    {
        type_str = "TURB";
        unit_str = "NTU";
        if (r->turb_ntu >= 400.0f || r->out_of_range)
        {
            is_out_of_range = true;
        }
        format_stable_measure_value(r->turb_ntu, 4, 0, raw_val_str, sizeof(raw_val_str));
    }
    else
    {
        /* Fallback / safe default: COND */
        type_str = "COND";
        unit_str = (s_cfg->display_unit == UNIT_MS_CM || (s_cfg->display_unit == UNIT_AUTO && r->cond_us >= 10000.0f)) ? "MS/CM" : "US/CM";
        format_stable_measure_value(r->cond_us, 0, s_cfg->display_unit, raw_val_str, sizeof(raw_val_str));
    }

    if (s_force_full_redraw || strcmp(type_str, s_prev_type_str) != 0 || strcmp(unit_str, s_prev_unit_str) != 0)
    {
        snprintf(s_prev_type_str, sizeof(s_prev_type_str), "%s", type_str);
        snprintf(s_prev_unit_str, sizeof(s_prev_unit_str), "%s", unit_str);

        lcd_clear_page(2);
        lcd_print_font(LCD_FONT_5X7, 2, 2, type_str);
        uint8_t unit_w = lcd_str_width(LCD_FONT_5X7, unit_str);
        uint8_t unit_col = (uint8_t)(126 - unit_w);
        lcd_print_font(LCD_FONT_5X7, 2, unit_col, unit_str);
    }

    /* 3. PAGES 3-5 (ROWS 24-47): LARGE MEASURED VALUE (PLANTED IN CENTER) */
    char disp_val_str[16] = "";

    if (s_measure_type == 4)
    {
        if (r->fault_turb && r->turb_ntu <= 0.0f)
        {
            snprintf(disp_val_str, sizeof(disp_val_str), "0.0");
        }
        else
        {
            snprintf(disp_val_str, sizeof(disp_val_str), "%s", raw_val_str);
        }
    }
    else if (r->fault_open || r->cell_ohms <= 0.0f || r->cond_us <= 0.0001f)
    {
        /* Clean zeros in middle when disconnected / no daughter device */
        snprintf(disp_val_str, sizeof(disp_val_str), "%s", raw_val_str);
    }
    else if (r->fault_short)
    {
        snprintf(disp_val_str, sizeof(disp_val_str), "SHORT");
    }
    else
    {
        bool is_washing_hold = false;
        if ((s_cfg->relay1_func == RELAY_FUNC_WASH && s_cfg->relay1_wash_state == WASH_STATE_HOLD && r->relay1_state == 1) ||
            (s_cfg->relay2_func == RELAY_FUNC_WASH && s_cfg->relay2_wash_state == WASH_STATE_HOLD && r->relay2_state == 1))
        {
            is_washing_hold = true;
        }

        if (is_washing_hold && s_frozen_val_str[0] != '\0')
        {
            snprintf(disp_val_str, sizeof(disp_val_str), "%s", s_frozen_val_str);
        }
        else
        {
            snprintf(disp_val_str, sizeof(disp_val_str), "%s", raw_val_str);
            snprintf(s_frozen_val_str, sizeof(s_frozen_val_str), "%s", raw_val_str);
        }
    }

    if (s_force_full_redraw || strcmp(disp_val_str, s_prev_val_str) != 0 || is_out_of_range != s_prev_warning)
    {
        snprintf(s_prev_val_str, sizeof(s_prev_val_str), "%s", disp_val_str);
        s_prev_warning = is_out_of_range;

        lcd_clear_page(3);
        lcd_clear_page(4);
        lcd_clear_page(5);

        if (is_out_of_range)
        {
            lcd_draw_warning_triangle(3, 2);
        }

        /* Firmly plant centered digits on screen without left/right shift */
        lcd_print_font_centered(LCD_FONT_16X24, 3, disp_val_str);
    }

    /* 4. PAGES 6-7 (ROWS 48-63): SUB-DISPLAY BAR (4 MODES) */
    char sub_bar_buf[32] = "";

    if (s_sub_bar_mode == 0)
    {
        /* Mode 0: Temperature and K-Constant */
        char temp_part[12] = "";
        char t_val[10];
        if (r->fault_open || r->fault_temp || r->cell_ohms <= 0.0f || r->temp_c < -40.0f)
        {
            snprintf(temp_part, sizeof(temp_part), "0.0C");
        }
        else if (s_cfg->temp_unit == TEMP_UNIT_F)
        {
            ftoa_custom(r->temp_f, 1, 1, false, t_val, sizeof(t_val));
            snprintf(temp_part, sizeof(temp_part), "%sF", t_val);
        }
        else
        {
            ftoa_custom(r->temp_c, 1, 1, false, t_val, sizeof(t_val));
            snprintf(temp_part, sizeof(temp_part), "%sC", t_val);
        }

        char k_val[12];
        ftoa_custom(s_cfg->k_constant, 4, 1, false, k_val, sizeof(k_val));
        char k_part[16];
        snprintf(k_part, sizeof(k_part), "K:%s", k_val);

        int temp_len = (int)strlen(temp_part);
        int k_len = (int)strlen(k_part);
        int spaces = 16 - temp_len - k_len;
        if (spaces < 0) spaces = 0;
        snprintf(sub_bar_buf, sizeof(sub_bar_buf), "%s%*s%s", temp_part, spaces, "", k_part);
    }
    else if (s_sub_bar_mode == 1)
    {
        /* Mode 1: Real-Time Turbidity & Temperature */
        char tb_str[10];
        ftoa_custom(r->turb_ntu, 1, 1, false, tb_str, sizeof(tb_str));
        char tb_part[16];
        snprintf(tb_part, sizeof(tb_part), "TB:%sNTU", (r->fault_turb && r->turb_ntu <= 0.0f) ? "0.0" : tb_str);

        char temp_part[12] = "";
        char t_val[10];
        if (r->fault_open || r->fault_temp || r->cell_ohms <= 0.0f || r->temp_c < -40.0f)
        {
            snprintf(temp_part, sizeof(temp_part), "0.0C");
        }
        else if (s_cfg->temp_unit == TEMP_UNIT_F)
        {
            ftoa_custom(r->temp_f, 1, 1, false, t_val, sizeof(t_val));
            snprintf(temp_part, sizeof(temp_part), "%sF", t_val);
        }
        else
        {
            ftoa_custom(r->temp_c, 1, 1, false, t_val, sizeof(t_val));
            snprintf(temp_part, sizeof(temp_part), "%sC", t_val);
        }

        int tb_len = (int)strlen(tb_part);
        int temp_len = (int)strlen(temp_part);
        int spaces = 16 - tb_len - temp_len;
        if (spaces < 0) spaces = 0;
        snprintf(sub_bar_buf, sizeof(sub_bar_buf), "%s%*s%s", tb_part, spaces, "", temp_part);
    }
    else if (s_sub_bar_mode == 2)
    {
        /* Mode 2: Real-Time TDS and Salinity Readings */
        char tds_part[16] = "";
        char sal_part[16] = "";

        if (r->fault_open || r->cell_ohms <= 0.0f || r->cond_us <= 0.0001f)
        {
            snprintf(tds_part, sizeof(tds_part), "TDS:0");
            snprintf(sal_part, sizeof(sal_part), "S:0.00");
        }
        else
        {
            /* TDS formatting */
            char tds_str[10];
            if (r->tds_ppm < 1000.0f)
            {
                ftoa_custom(r->tds_ppm, 1, 1, false, tds_str, sizeof(tds_str));
            }
            else
            {
                ftoa_custom(r->tds_ppm, 0, 1, false, tds_str, sizeof(tds_str));
            }
            snprintf(tds_part, sizeof(tds_part), "TDS:%s", tds_str);

            /* Salinity formatting (ppt) */
            char sal_str[10];
            if (r->sal_ppt < 10.0f)
            {
                ftoa_custom(r->sal_ppt, 2, 1, false, sal_str, sizeof(sal_str));
            }
            else
            {
                ftoa_custom(r->sal_ppt, 1, 1, false, sal_str, sizeof(sal_str));
            }
            snprintf(sal_part, sizeof(sal_part), "S:%s", sal_str);
        }

        int tds_len = (int)strlen(tds_part);
        int sal_len = (int)strlen(sal_part);
        int spaces = 16 - tds_len - sal_len;
        if (spaces < 0) spaces = 0;
        snprintf(sub_bar_buf, sizeof(sub_bar_buf), "%s%*s%s", tds_part, spaces, "", sal_part);
    }
    else
    {
        /* Mode 3: Two Current Outputs (I1 & I2) */
        char c1_val[10], c2_val[10];
        ftoa_custom(r->current1_ma, 1, 1, false, c1_val, sizeof(c1_val));
        ftoa_custom(r->current2_ma, 1, 1, false, c2_val, sizeof(c2_val));

        /* Clean, professional formatting with distinct spacing and aligned columns:
         * e.g., "I1:20.0  I2:10.6" or "I1: 4.0  I2: 4.0" (always 16 chars with 2 spaces) */
        char curr1_str[16];
        char curr2_str[16];
        snprintf(curr1_str, sizeof(curr1_str), "I1:%s%s",
                 (r->current1_ma < 10.0f && r->current1_ma >= 0.0f) ? " " : "", c1_val);
        snprintf(curr2_str, sizeof(curr2_str), "I2:%s%s",
                 (r->current2_ma < 10.0f && r->current2_ma >= 0.0f) ? " " : "", c2_val);

        int len1 = (int)strlen(curr1_str);
        int len2 = (int)strlen(curr2_str);
        int spaces = 16 - len1 - len2;
        if (spaces < 0) spaces = 0;
        snprintf(sub_bar_buf, sizeof(sub_bar_buf), "%s%*s%s", curr1_str, spaces, "", curr2_str);
    }
    sub_bar_buf[16] = '\0';

    if (s_force_full_redraw || strcmp(sub_bar_buf, s_prev_sub_str) != 0)
    {
        snprintf(s_prev_sub_str, sizeof(s_prev_sub_str), "%s", sub_bar_buf);
        lcd_print_bar(6, sub_bar_buf);
    }

    s_force_full_redraw = false;
}

/* ========================================================================== */
/*                    STATUS SCREEN (SW1 ENTER IN MEASURE)                    */
/* ========================================================================== */

static void render_status_screen(void)
{
    const meter_reading_t *r = meter_data();
    if (!r) return;

    lcd_print_centered(0, "= STATUS =");
    lcd_draw_hline(1, 0x08);

    char line[32];
    snprintf(line, sizeof(line), "R1: %-3s    R2: %-3s",
             r->relay1_state ? "ON" : "OFF",
             r->relay2_state ? "ON" : "OFF");
    lcd_print(2, 2, line);

    lcd_print(3, 2, "RAW EC @ 25C:");
    char ec_str[16];
    if (r->cond_raw_us <= 0.0001f)
    {
        snprintf(ec_str, sizeof(ec_str), "0");
    }
    else
    {
        format_4sig_digits(r->cond_raw_us, ec_str, sizeof(ec_str));
    }
    snprintf(line, sizeof(line), "  %s US/CM", ec_str);
    lcd_print(4, 2, line);

    char turb_str[10];
    if (r->fault_turb)
    {
        snprintf(turb_str, sizeof(turb_str), "--.-");
    }
    else
    {
        ftoa_custom(r->turb_ntu, 1, 1, false, turb_str, sizeof(turb_str));
    }
    snprintf(line, sizeof(line), "TB:%sNTU  CAL:%d", turb_str, (int)s_cfg->cal_points_count);
    lcd_print(5, 2, line);

    lcd_draw_hline(6, 0x08);
    lcd_print_centered(7, "[ANY KEY: RETURN]");
}

static void render_sample_preview_screen(void)
{
    lcd_print_centered(0, "= SAMPLE TEST =");
    lcd_draw_hline(1, 0x08);

    lcd_print(2, 2, "FORMULA: (A+B)*(C-D)");

    char buf[32];
    char a_str[10], b_str[10], c_str[10], d_str[10], fval_str[16];
    ftoa_custom(s_cfg->sample_a, 2, 1, false, a_str, sizeof(a_str));
    ftoa_custom(s_cfg->sample_b, 2, 1, false, b_str, sizeof(b_str));
    ftoa_custom(s_cfg->sample_c, 2, 1, false, c_str, sizeof(c_str));
    ftoa_custom(s_cfg->sample_d, 2, 1, false, d_str, sizeof(d_str));

    float fval = (s_cfg->sample_a + s_cfg->sample_b) * (s_cfg->sample_c - s_cfg->sample_d);
    ftoa_custom(fval, 2, 1, false, fval_str, sizeof(fval_str));

    snprintf(buf, sizeof(buf), "A:%-6s  B:%-6s", a_str, b_str);
    lcd_print(3, 2, buf);

    snprintf(buf, sizeof(buf), "C:%-6s  D:%-6s", c_str, d_str);
    lcd_print(4, 2, buf);

    snprintf(buf, sizeof(buf), "FVAL = %s", fval_str);
    lcd_print(5, 2, buf);

    lcd_draw_hline(6, 0x08);
    lcd_print_centered(7, "[ANY KEY: RETURN]");
}

/* ========================================================================== */
/*                           VERTICAL MENU RENDERING                          */
/* ========================================================================== */

static void render_vertical_menu(const char *header, const char *items[], uint8_t count, uint8_t cursor)
{
    lcd_print_centered(0, header);
    lcd_draw_hline(1, 0x08);

    uint8_t start = 0;
    if (cursor >= 5)
    {
        start = cursor - 4;
    }

    for (uint8_t row = 0; row < 5; row++)
    {
        uint8_t idx = start + row;
        uint8_t page = 2 + row;

        if (idx < count)
        {
            char line[22];
            uint8_t is_sel = (idx == cursor);

            /* In Option Picker, mark the currently saved choice with '*' */
            if (s_screen == UI_OPTION_PICKER && s_opt_target)
            {
                bool is_saved = (idx == *s_opt_target);
                snprintf(line, sizeof(line), "%s%-16s%s", is_sel ? "> " : "  ", items[idx], is_saved ? "*" : " ");
            }
            else
            {
                snprintf(line, sizeof(line), "%s%s", is_sel ? "> " : "  ", items[idx]);
            }
            line[21] = '\0';
            lcd_print_line(page, line, is_sel);
        }
        else
        {
            lcd_clear_page(page);
        }
    }

    lcd_print_compact_centered(7, "1:BACK 2:DN 3:UP 4:OK");
}

/* ========================================================================== */
/*                              SCREEN RENDER DISPATCH                        */
/* ========================================================================== */

static void render_screen(void)
{
    switch (s_screen)
    {
        case UI_MEASURE:
            render_measure_screen();
            break;

        case UI_STATUS_SCREEN:
            render_status_screen();
            break;

        case UI_MENU_L1:
            render_vertical_menu("== MAIN MENU ==", s_menu_l1_names, MENU_L1_COUNT, s_l1_cursor);
            break;

        case UI_MENU_L2:
            if (s_l1_cursor == 0)      render_vertical_menu("= 1. PROBE SET =", s_sub_probe, 5, s_l2_cursor);
            else if (s_l1_cursor == 1) render_vertical_menu("= 2. TEMPERATURE =", s_sub_temp, 4, s_l2_cursor);
            else if (s_l1_cursor == 2) render_vertical_menu("= 3. ALARM SET =", s_sub_alarm, 2, s_l2_cursor);
            else if (s_l1_cursor == 3) render_vertical_menu("= 4. CURRENT SET =", s_sub_current, 2, s_l2_cursor);
            else if (s_l1_cursor == 4) render_vertical_menu("= 5. COMM. SET =", s_sub_comm, 4, s_l2_cursor);
            else if (s_l1_cursor == 5) render_vertical_menu("= 6. SYSTEM SET =", s_sub_system, 5, s_l2_cursor);
            else if (s_l1_cursor == 6) render_vertical_menu("= 7. TEST/MAINT =", s_sub_test, 5, s_l2_cursor);
            break;

        case UI_MENU_L3:
            if (s_l1_cursor == 0 && s_l2_cursor == 1) /* 1.2 Probe CAL */
            {
                render_vertical_menu("= PROBE CAL =", s_sub_cal_items, 8, s_l3_cursor);
            }
            else if (s_l1_cursor == 2) /* 3. Alarm Set (Relay I or II) */
            {
                uint8_t func = (s_l2_cursor == 0) ? s_cfg->relay1_func : s_cfg->relay2_func;
                const char *hdr = (s_l2_cursor == 0) ? "= RELAY I =" : "= RELAY II =";
                if (func == RELAY_FUNC_WASH)
                {
                    render_vertical_menu(hdr, s_sub_relay_wash_items, 4, s_l3_cursor);
                }
                else
                {
                    render_vertical_menu(hdr, s_sub_relay_ctrl_items, 3, s_l3_cursor);
                }
            }
            else if (s_l1_cursor == 3) /* 4. Current Set */
            {
                const char *hdr = (s_l2_cursor == 0) ? "= CURRENT I =" : "= CURRENT II =";
                render_vertical_menu(hdr, s_sub_curr_items, 4, s_l3_cursor);
            }
            else if (s_l1_cursor == 5 && s_l2_cursor == 1) /* 6.2 Backlight */
            {
                render_vertical_menu("= BACKLIGHT =", s_sub_backlight_items, 3, s_l3_cursor);
            }
            else if (s_l1_cursor == 6 && s_l2_cursor == 0) /* 7.1 Current CAL */
            {
                render_vertical_menu("= CURRENT CAL =", s_sub_test_two_currs, 2, s_l3_cursor);
            }
            else if (s_l1_cursor == 6 && s_l2_cursor == 1) /* 7.2 Relay Test */
            {
                render_vertical_menu("= RELAY TEST =", s_sub_test_two_relays, 2, s_l3_cursor);
            }
            else if (s_l1_cursor == 6 && s_l2_cursor == 2) /* 7.3 Current Test */
            {
                render_vertical_menu("= CURRENT TEST =", s_sub_test_two_currs, 2, s_l3_cursor);
            }
            break;

        case UI_MENU_L4:
            if (s_l1_cursor == 5 && s_l2_cursor == 1 && s_l3_cursor == 0) /* Scanty Power timeout */
            {
                render_vertical_menu("= SCANTY TIME =", s_sub_scanty_times, 3, s_l4_cursor);
            }
            else if (s_l1_cursor == 6 && s_l2_cursor == 0) /* Current CAL point */
            {
                const char *hdr = (s_l3_cursor == 0) ? "= CAL CURR I =" : "= CAL CURR II =";
                render_vertical_menu(hdr, s_sub_curr_cal_pts, 2, s_l4_cursor);
            }
            else if (s_l1_cursor == 6 && s_l2_cursor == 1) /* Relay Test ON/OFF */
            {
                const char *hdr = (s_l3_cursor == 0) ? "= TEST RELAY I =" : "= TEST RELAY II =";
                render_vertical_menu(hdr, s_sub_relay_onoff, 2, s_l4_cursor);
            }
            break;

        case UI_OPTION_PICKER:
            render_vertical_menu(s_opt_title, s_opt_strings, s_opt_count, s_opt_cursor);
            break;

        case UI_NUMERIC_EDITOR:
        {
            lcd_print_centered(0, s_edit_title);
            lcd_draw_hline(1, 0x08);
            lcd_clear_page(2);
            lcd_print_centered(3, "ENTER VALUE:");

            lcd_clear_page(4);
            lcd_clear_page(5);
            uint8_t start_col = (uint8_t)((128 - (s_edit_len * 10)) / 2);
            for (uint8_t i = 0; i < s_edit_len; i++)
            {
                char d[2] = { s_edit_buf[i], '\0' };
                uint8_t c_col = start_col + (i * 10);
                lcd_print(4, c_col, d);
                if (i == s_edit_cursor)
                {
                    lcd_print(5, c_col, "^");
                }
            }

            lcd_clear_page(6);
            lcd_print_compact_centered(7, "1:ESC 2:-1 3:-> 4:SAVE");
            break;
        }

        case UI_CONTRAST_EDITOR:
        {
            lcd_print_centered(0, "== CONTRAST SET ==");
            lcd_draw_hline(1, 0x08);
            lcd_clear_page(2);

            char buf[24];
            snprintf(buf, sizeof(buf), "LEVEL: [ %2d ]", (int)s_cfg->contrast);
            lcd_print_centered(3, buf);
            lcd_print_centered(4, "(RANGE: 0 - 63)");

            lcd_draw_hline(5, 0x08);
            lcd_clear_page(6);
            lcd_print_compact_centered(7, "1:ESC 2:-1 3:+1 4:SAVE");
            break;
        }

        case UI_INFO_SCREEN:
        {
            lcd_print_centered(0, "= SOFTWARE INFO =");
            lcd_draw_hline(1, 0x08);
            lcd_print(2, 4, "MODEL : TDS 4002");
            lcd_print(3, 4, "MCU   : STM32G030K8");
            lcd_print(4, 4, "VER   : v1.0.4 PRO");
            lcd_print(5, 4, "DATE  : OCT 2026");
            lcd_draw_hline(6, 0x08);
            lcd_print_centered(7, "[ANY KEY: RETURN]");
            break;
        }

        case UI_CAL_WIZARD:
        {
            const meter_reading_t *r = meter_data();
            lcd_print_centered(0, "= CALIBRATION =");
            lcd_draw_hline(1, 0x08);
            char b[32];
            snprintf(b, sizeof(b), "POINT %d / 5", s_cal_point_idx + 1);
            lcd_print_centered(2, b);

            char r_num[16], s_num[16];
            ftoa_custom(r ? r->cond_raw_us : 0.0f, 1, 1, false, r_num, sizeof(r_num));
            ftoa_custom(s_cfg->cal_std[s_cal_point_idx], 1, 1, false, s_num, sizeof(s_num));
            snprintf(b, sizeof(b), "RAW: %s US/CM", r_num);
            lcd_print_centered(3, b);
            snprintf(b, sizeof(b), "STD: %s US/CM", s_num);
            lcd_print_centered(4, b);

            if (r)
            {
                s_cal_last_readings[s_cal_sample_idx % 8] = r->cond_raw_us;
                s_cal_sample_idx++;
                float min_r = s_cal_last_readings[0];
                float max_r = s_cal_last_readings[0];
                for (uint8_t k = 1; k < 8; k++)
                {
                    if (s_cal_last_readings[k] < min_r) min_r = s_cal_last_readings[k];
                    if (s_cal_last_readings[k] > max_r) max_r = s_cal_last_readings[k];
                }
                float var_pct = (r->cond_raw_us > 0.01f) ? ((max_r - min_r) / r->cond_raw_us) * 100.0f : 0.0f;
                if (var_pct > 1.0f)
                {
                    lcd_print_centered(5, "WAIT, NOT STABLE");
                }
                else
                {
                    lcd_print_centered(5, "STABLE (READY)");
                }
            }

            lcd_draw_hline(6, 0x08);
            lcd_print_compact_centered(7, "1:CANCEL            4:SAVE");
            break;
        }

        case UI_SAMPLE_PREVIEW:
            render_sample_preview_screen();
            break;
    }
}

/* ========================================================================== */
/*                         NUMERIC EDITOR HELPERS                             */
/* ========================================================================== */

static void open_float_editor(const char *title, float *target, const char *format, float min_v, float max_v)
{
    s_prev_screen = s_screen;
    s_edit_type = EDIT_TYPE_FLOAT;
    s_edit_target_ptr = target;
    s_edit_title = title;
    s_edit_min = min_v;
    s_edit_max = max_v;
    s_edit_has_sign = (format[0] == '+' || format[1] == '+');
    s_edit_cal_point = 0xFF;

    int decimals = 3;
    int min_int = 2;
    bool show_sign = s_edit_has_sign;

    const char *dot = strchr(format, '.');
    if (dot)
    {
        decimals = 0;
        const char *dp = dot + 1;
        while (*dp >= '0' && *dp <= '9')
        {
            decimals = (decimals * 10) + (*dp - '0');
            dp++;
        }
    }

    const char *p = format + 1;
    if (*p == '+' || *p == '-') p++;
    if (*p == '0') p++;
    int total_width = 0;
    while (*p >= '0' && *p <= '9')
    {
        total_width = (total_width * 10) + (*p - '0');
        p++;
    }

    if (total_width > 0)
    {
        min_int = total_width - 1 - decimals - (show_sign ? 1 : 0);
        if (min_int < 1) min_int = 1;
    }

    memset(s_edit_buf, 0, sizeof(s_edit_buf));
    ftoa_custom(*target, decimals, min_int, show_sign, s_edit_buf, sizeof(s_edit_buf));
    s_edit_len = (uint8_t)strlen(s_edit_buf);

    /* Sanitize s_edit_buf: strictly numeric characters only, no trailing garbage */
    for (uint8_t j = 0; j < s_edit_len; j++)
    {
        char c = s_edit_buf[j];
        if (c != '.' && c != '+' && c != '-' && (c < '0' || c > '9'))
        {
            s_edit_buf[j] = '\0';
            s_edit_len = j;
            break;
        }
    }

    s_edit_cursor = 0;
    if (s_edit_buf[s_edit_cursor] == '.')
    {
        s_edit_cursor = 1;
    }

    s_screen = UI_NUMERIC_EDITOR;
    lcd_clear();
    render_screen();
}

static void open_uint8_editor(const char *title, uint8_t *target, const char *format, uint8_t min_v, uint8_t max_v)
{
    s_prev_screen = s_screen;
    s_edit_type = EDIT_TYPE_UINT8;
    s_edit_target_ptr = target;
    s_edit_title = title;
    s_edit_min = (float)min_v;
    s_edit_max = (float)max_v;
    s_edit_has_sign = false;
    s_edit_cal_point = 0xFF;
    memset(s_edit_buf, 0, sizeof(s_edit_buf));

    snprintf(s_edit_buf, sizeof(s_edit_buf), format, (unsigned int)*target);
    s_edit_len = (uint8_t)strlen(s_edit_buf);

    for (uint8_t j = 0; j < s_edit_len; j++)
    {
        char c = s_edit_buf[j];
        if (c < '0' || c > '9')
        {
            s_edit_buf[j] = '\0';
            s_edit_len = j;
            break;
        }
    }

    s_edit_cursor = 0;
    s_screen = UI_NUMERIC_EDITOR;
    lcd_clear();
    render_screen();
}

static void open_uint16_editor(const char *title, uint16_t *target, const char *format, uint16_t min_v, uint16_t max_v)
{
    s_prev_screen = s_screen;
    s_edit_type = EDIT_TYPE_UINT16;
    s_edit_target_ptr = target;
    s_edit_title = title;
    s_edit_min = (float)min_v;
    s_edit_max = (float)max_v;
    s_edit_has_sign = false;
    s_edit_cal_point = 0xFF;
    memset(s_edit_buf, 0, sizeof(s_edit_buf));

    snprintf(s_edit_buf, sizeof(s_edit_buf), format, (unsigned int)*target);
    s_edit_len = (uint8_t)strlen(s_edit_buf);

    for (uint8_t j = 0; j < s_edit_len; j++)
    {
        char c = s_edit_buf[j];
        if (c < '0' || c > '9')
        {
            s_edit_buf[j] = '\0';
            s_edit_len = j;
            break;
        }
    }

    s_edit_cursor = 0;
    s_screen = UI_NUMERIC_EDITOR;
    lcd_clear();
    render_screen();
}

static void open_option_picker(const char *title, const char *options[], uint8_t count, uint8_t *target)
{
    open_option_picker_cb(title, options, count, target, NULL);
}

static void open_option_picker_cb(const char *title, const char *options[], uint8_t count, uint8_t *target, picker_cb_t cb)
{
    s_prev_screen = s_screen;
    s_opt_title = title;
    s_opt_strings = options;
    s_opt_count = count;
    s_opt_target = target;
    s_opt_cursor = (target != NULL) ? *target : 0;
    if (s_opt_cursor >= count) s_opt_cursor = 0;
    s_opt_callback = cb;
    s_screen = UI_OPTION_PICKER;
    lcd_clear();
    render_screen();
}

static void start_cal_wizard(uint8_t point_idx)
{
    s_cal_point_idx = point_idx;
    s_screen = UI_CAL_WIZARD;
    lcd_clear();
    render_screen();
}

/* ========================================================================== */
/*                          PICKER CALLBACK HANDLERS                          */
/* ========================================================================== */

static void cb_baud_rate(uint8_t idx)
{
    if (idx < 6)
    {
        s_cfg->baud_rate = s_baud_table[idx];
        meter_comm_init(s_cfg);
    }
}

static void cb_parity(uint8_t idx)
{
    (void)idx;
    meter_comm_init(s_cfg);
}

static void cb_stop_bits(uint8_t idx)
{
    (void)idx;
    meter_comm_init(s_cfg);
}

static void cb_display_speed(uint8_t idx)
{
    if (idx < 5)
    {
        s_cfg->display_speed = s_dspeed_table[idx];
    }
}

static void cb_reset_cal(uint8_t idx)
{
    if (idx == 1) /* OK */
    {
        s_cfg->cal_points_count = 0;
        s_cfg->ec_offset = 0.0f;
        s_cfg->ec_factor = 1.000f;
    }
}

static void cb_reset_temp_cal(uint8_t idx)
{
    if (idx == 1) /* OK */
    {
        s_cfg->temp_cal_offset = 0.0f;
    }
}

static void cb_restore_defaults(uint8_t idx)
{
    if (idx == 1) /* OK */
    {
        meter_core_default_settings(s_cfg);
        s_screen = UI_MEASURE;
        s_force_full_redraw = true;
    }
}

/* ========================================================================== */
/*                          KEY EVENT PROCESSING                              */
/* ========================================================================== */

void meter_ui_task(void)
{
    buttons_update();
    btn_event_t ev = buttons_get_event();
    uint32_t now = HAL_GetTick();

    if (ev != BTN_EVT_NONE)
    {
        s_last_btn_activity_tick = now;
        if (s_cfg->backlight_mode != 2)
        {
            HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_SET);
        }
    }
    else
    {
        /* Backlight power saving check in Scanty Power mode */
        if (s_cfg->backlight_mode == 0) /* Scanty Power */
        {
            uint32_t timeout_ms = (uint32_t)(s_cfg->backlight_timeout ? s_cfg->backlight_timeout : 1) * 60000UL;
            if ((now - s_last_btn_activity_tick) >= timeout_ms)
            {
                HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_RESET);
            }
        }
        else if (s_cfg->backlight_mode == 2) /* Always OFF */
        {
            HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_RESET);
        }
    }

    /* Long-press SW4 (MENU) from anywhere in menus returns to Measurement mode */
    if (ev == BTN_EVT_MENU_LONG)
    {
        s_relay_override[0] = -1;
        s_relay_override[1] = -1;
        s_screen = UI_MEASURE;
        s_force_full_redraw = true;
        lcd_clear();
        render_screen();
        return;
    }

    /* ---------------------------------------------------------------------- */
    /* UI_MEASURE                                                             */
    /* ---------------------------------------------------------------------- */
    if (s_screen == UI_MEASURE)
    {
        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Open Main Menu */
        {
            s_screen = UI_MENU_L1;
            s_l1_cursor = 0;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Cycle bottom sub-bar (Temp&K -> TB&Temp -> TDS&SAL -> Currents) */
        {
            s_sub_bar_mode = (s_sub_bar_mode + 1) % 4;
            s_cfg->last_sub_bar_mode = s_sub_bar_mode;
            s_force_full_redraw = true;
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Cycle COND, RES, TDS, SAL, TURB */
        {
            s_measure_type = (s_measure_type + 1) % 5;
            s_cfg->last_measure_type = s_measure_type;
            s_force_full_redraw = true;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Show STATUS Screen */
        {
            s_screen = UI_STATUS_SCREEN;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_STATUS_SCREEN                                                       */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_STATUS_SCREEN)
    {
        if (ev != BTN_EVT_NONE)
        {
            s_screen = UI_MEASURE;
            s_force_full_redraw = true;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_INFO_SCREEN                                                         */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_INFO_SCREEN)
    {
        if (ev != BTN_EVT_NONE)
        {
            s_screen = UI_MENU_L2;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_SAMPLE_PREVIEW                                                      */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_SAMPLE_PREVIEW)
    {
        if (ev != BTN_EVT_NONE)
        {
            s_screen = UI_MENU_L3;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_MENU_L1: 7 Main Categories                                          */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_MENU_L1)
    {
        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Return to Measurement mode */
        {
            s_relay_override[0] = -1;
            s_relay_override[1] = -1;
            s_screen = UI_MEASURE;
            s_force_full_redraw = true;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Cursor Down */
        {
            s_l1_cursor = (s_l1_cursor + 1) % MENU_L1_COUNT;
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Cursor Up */
        {
            s_l1_cursor = (s_l1_cursor + MENU_L1_COUNT - 1) % MENU_L1_COUNT;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Enter Category L2 */
        {
            s_screen = UI_MENU_L2;
            s_l2_cursor = 0;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_MENU_L2: Submenu items inside Category                              */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_MENU_L2)
    {
        uint8_t count = 5;
        if (s_l1_cursor == 0) count = 5; /* 1. Probe Set has 5 items */
        else if (s_l1_cursor == 1 || s_l1_cursor == 4) count = 4;
        else if (s_l1_cursor == 2 || s_l1_cursor == 3) count = 2;

        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Back to Main Menu L1 */
        {
            s_screen = UI_MENU_L1;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Cursor Down */
        {
            s_l2_cursor = (s_l2_cursor + 1) % count;
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Cursor Up */
        {
            s_l2_cursor = (s_l2_cursor + count - 1) % count;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Select / Enter L3 */
        {
            if (s_l1_cursor == 0) /* 1. Probe Set */
            {
                if (s_l2_cursor == 0)      open_float_editor("K CONSTANT", &s_cfg->k_constant, "%07.4f", 0.0001f, 9.9999f);
                else if (s_l2_cursor == 1) { s_screen = UI_MENU_L3; s_l3_cursor = 0; lcd_clear(); } /* Probe CAL */
                else if (s_l2_cursor == 2) open_float_editor("TEMP COEF %/C", &s_cfg->temp_coef, "%05.2f", 0.00f, 99.99f);
                else if (s_l2_cursor == 3) open_float_editor("TDS COEF", &s_cfg->tds_coef, "%06.3f", 0.001f, 99.999f);
                else if (s_l2_cursor == 4) open_option_picker("= DISPLAY UNIT =", s_opt_units, 3, &s_cfg->display_unit);
            }
            else if (s_l1_cursor == 1) /* 2. Temperature */
            {
                if (s_l2_cursor == 0)      open_option_picker("= PROBE TYPE =", s_opt_probes, 3, &s_cfg->temp_probe_type);
                else if (s_l2_cursor == 1) open_option_picker("= TEMP UNIT =", s_opt_tunits, 2, &s_cfg->temp_unit);
                else if (s_l2_cursor == 2) open_float_editor("TEMP OFFSET C", &s_cfg->temp_cal_offset, "%+05.1f", -50.0f, 50.0f);
                else if (s_l2_cursor == 3) { s_confirm_choice_idx = 0; open_option_picker_cb("= RESET TEMP CAL =", s_opt_confirm, 2, &s_confirm_choice_idx, cb_reset_temp_cal); }
            }
            else if (s_l1_cursor == 2 || s_l1_cursor == 3) /* 3. Alarm Set or 4. Current Set */
            {
                s_screen = UI_MENU_L3;
                s_l3_cursor = 0;
                lcd_clear();
            }
            else if (s_l1_cursor == 4) /* 5. Comm Set */
            {
                if (s_l2_cursor == 0)
                {
                    open_uint8_editor("SLAVE ADDR (1-247)", &s_cfg->slave_addr, "%03u", 1, 247);
                }
                else if (s_l2_cursor == 1)
                {
                    s_baud_choice_idx = 1;
                    for (uint8_t b = 0; b < 6; b++)
                    {
                        if (s_cfg->baud_rate == s_baud_table[b]) { s_baud_choice_idx = b; break; }
                    }
                    open_option_picker_cb("= BAUD RATE =", s_opt_bauds, 6, &s_baud_choice_idx, cb_baud_rate);
                }
                else if (s_l2_cursor == 2)
                {
                    open_option_picker_cb("= VERIFY BIT =", s_opt_parity, 3, &s_cfg->parity, cb_parity);
                }
                else if (s_l2_cursor == 3)
                {
                    open_option_picker_cb("= STOP BIT =", s_opt_stops, 2, &s_cfg->stop_bits, cb_stop_bits);
                }
            }
            else if (s_l1_cursor == 5) /* 6. System Set */
            {
                if (s_l2_cursor == 0)
                {
                    s_dspeed_choice_idx = 1;
                    for (uint8_t d = 0; d < 5; d++)
                    {
                        if (s_cfg->display_speed == s_dspeed_table[d]) { s_dspeed_choice_idx = d; break; }
                    }
                    open_option_picker_cb("= DISPLAY SPEED =", s_opt_dspeed, 5, &s_dspeed_choice_idx, cb_display_speed);
                }
                else if (s_l2_cursor == 1) /* Backlight */
                {
                    s_screen = UI_MENU_L3;
                    s_l3_cursor = 0;
                    lcd_clear();
                }
                else if (s_l2_cursor == 2) /* Contrast Set */
                {
                    s_contrast_orig = s_cfg->contrast;
                    s_screen = UI_CONTRAST_EDITOR;
                    lcd_clear();
                }
                else if (s_l2_cursor == 3) /* Software Info */
                {
                    s_screen = UI_INFO_SCREEN;
                    lcd_clear();
                }
                else if (s_l2_cursor == 4) /* Password */
                {
                    open_uint16_editor("ENTER PASSWORD", &s_cfg->password, "%04u", 0, 9999);
                }
            }
            else if (s_l1_cursor == 6) /* 7. Test/Maint */
            {
                if (s_l2_cursor == 0 || s_l2_cursor == 1 || s_l2_cursor == 2) /* Current CAL, Relay Test, Current Test */
                {
                    s_screen = UI_MENU_L3;
                    s_l3_cursor = 0;
                    lcd_clear();
                }
                else if (s_l2_cursor == 3) /* Measure Mode */
                {
                    open_option_picker("= MEASURE MODE =", s_opt_meas_modes, 2, &s_cfg->measure_mode);
                }
                else if (s_l2_cursor == 4) /* Restore Defaults */
                {
                    s_confirm_choice_idx = 0;
                    open_option_picker_cb("= RESTORE DEFAULTS =", s_opt_confirm, 2, &s_confirm_choice_idx, cb_restore_defaults);
                }
            }
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_MENU_L3: Deep Submenus (Probe CAL, Relays, Current, System, Test)   */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_MENU_L3)
    {
        uint8_t count = 4;
        if (s_l1_cursor == 0 && s_l2_cursor == 1) count = 8; /* Probe CAL: 8 items */
        else if (s_l1_cursor == 2)
        {
            uint8_t func = (s_l2_cursor == 0) ? s_cfg->relay1_func : s_cfg->relay2_func;
            count = (func == RELAY_FUNC_WASH) ? 4 : 3;
        }
        else if (s_l1_cursor == 3) count = 4; /* Current I / II */
        else if (s_l1_cursor == 5 && s_l2_cursor == 1) count = 3; /* Backlight: 3 items */
        else if (s_l1_cursor == 6) count = 2; /* Current CAL / Relay Test / Current Test */

        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Back to L2 */
        {
            s_screen = UI_MENU_L2;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Cursor Down */
        {
            s_l3_cursor = (s_l3_cursor + 1) % count;
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Cursor Up */
        {
            s_l3_cursor = (s_l3_cursor + count - 1) % count;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Select / Enter L4 or Editor */
        {
            /* 1.2 PROBE CAL SUBMENU */
            if (s_l1_cursor == 0 && s_l2_cursor == 1)
            {
                if (s_l3_cursor == 0)      open_float_editor("EC OFFSET", &s_cfg->ec_offset, "%+08.2f", -9999.99f, 9999.99f);
                else if (s_l3_cursor == 1) open_float_editor("EC FACTOR", &s_cfg->ec_factor, "%06.3f", 0.001f, 99.999f);
                else if (s_l3_cursor >= 2 && s_l3_cursor <= 6)
                {
                    uint8_t pt = s_l3_cursor - 2;
                    s_edit_cal_point = pt;
                    /* First ask for reference conductivity standard (7.2 digits) */
                    open_float_editor("REF STANDARD", &s_cfg->cal_std[pt], "%08.2f", 0.01f, 99999.99f);
                }
                else if (s_l3_cursor == 7) /* 8. Reset CAL */
                {
                    s_confirm_choice_idx = 0;
                    open_option_picker_cb("= RESET CAL =", s_opt_confirm, 2, &s_confirm_choice_idx, cb_reset_cal);
                }
            }

            /* 3. ALARM SET (RELAY I or II) */
            else if (s_l1_cursor == 2)
            {
                uint8_t *r_func = (s_l2_cursor == 0) ? &s_cfg->relay1_func : &s_cfg->relay2_func;
                float   *on_val = (s_l2_cursor == 0) ? &s_cfg->relay1_on_val : &s_cfg->relay2_on_val;
                float   *off_val = (s_l2_cursor == 0) ? &s_cfg->relay1_off_val : &s_cfg->relay2_off_val;
                uint16_t *w_int  = (s_l2_cursor == 0) ? &s_cfg->relay1_wash_int_min : &s_cfg->relay2_wash_int_min;
                uint16_t *w_time = (s_l2_cursor == 0) ? &s_cfg->relay1_wash_time_s : &s_cfg->relay2_wash_time_s;
                uint8_t *w_state = (s_l2_cursor == 0) ? &s_cfg->relay1_wash_state : &s_cfg->relay2_wash_state;

                if (*r_func == RELAY_FUNC_WASH)
                {
                    if (s_l3_cursor == 0)      open_option_picker("= FUNCTION SET =", s_opt_rfuncs, 6, r_func);
                    else if (s_l3_cursor == 1) open_uint16_editor("WASH INT (MIN)", w_int, "%03u", 1, 999);
                    else if (s_l3_cursor == 2) open_uint16_editor("WASH TIME (S)", w_time, "%03u", 1, 999);
                    else if (s_l3_cursor == 3) open_option_picker("= WASH STATE =", s_opt_wstates, 2, w_state);
                }
                else
                {
                    if (s_l3_cursor == 0)      open_option_picker("= FUNCTION SET =", s_opt_rfuncs, 6, r_func);
                    else if (s_l3_cursor == 1) open_float_editor("SET ON VALUE", on_val, "%+08.2f", -9999.99f, 9999.99f);
                    else if (s_l3_cursor == 2) open_float_editor("SET OFF VALUE", off_val, "%+08.2f", -9999.99f, 9999.99f);
                }
            }

            /* 4. CURRENT SET */
            else if (s_l1_cursor == 3)
            {
                uint8_t *c_type = (s_l2_cursor == 0) ? &s_cfg->curr1_type : &s_cfg->curr2_type;
                uint8_t *c_chan = (s_l2_cursor == 0) ? &s_cfg->curr1_chan : &s_cfg->curr2_chan;
                float   *c_max  = (s_l2_cursor == 0) ? &s_cfg->curr1_max_val : &s_cfg->curr2_max_val;
                float   *c_min  = (s_l2_cursor == 0) ? &s_cfg->curr1_min_val : &s_cfg->curr2_min_val;

                if (s_l3_cursor == 0)      open_option_picker("= OUTPUT TYPE =", s_opt_ctypes, 2, c_type);
                else if (s_l3_cursor == 1) open_option_picker("= CHANNEL SELECT =", s_opt_cchans, 5, c_chan);
                else if (s_l3_cursor == 2) open_float_editor("MAX VAL (20mA)", c_max, "%+08.2f", -9999.99f, 9999.99f);
                else if (s_l3_cursor == 3) open_float_editor("MIN VAL (4mA)", c_min, "%+08.2f", -9999.99f, 9999.99f);
            }

            /* 6.2 BACKLIGHT */
            else if (s_l1_cursor == 5 && s_l2_cursor == 1)
            {
                if (s_l3_cursor == 0) /* 1. Scanty Power */
                {
                    s_screen = UI_MENU_L4;
                    s_l4_cursor = 0;
                    lcd_clear();
                }
                else if (s_l3_cursor == 1) /* 2. Always On */
                {
                    s_cfg->backlight_mode = 1;
                    meter_flash_save(s_cfg);
                    HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_SET);
                    s_screen = UI_MENU_L2;
                    lcd_clear();
                }
                else /* 3. Always Off */
                {
                    s_cfg->backlight_mode = 2;
                    meter_flash_save(s_cfg);
                    HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN, GPIO_PIN_RESET);
                    s_screen = UI_MENU_L2;
                    lcd_clear();
                }
            }

            /* 7.1 CURRENT CAL */
            else if (s_l1_cursor == 6 && s_l2_cursor == 0)
            {
                s_screen = UI_MENU_L4;
                s_l4_cursor = 0;
                lcd_clear();
            }

            /* 7.2 RELAY TEST */
            else if (s_l1_cursor == 6 && s_l2_cursor == 1)
            {
                s_screen = UI_MENU_L4;
                s_l4_cursor = 0;
                lcd_clear();
            }

            /* 7.3 CURRENT TEST */
            else if (s_l1_cursor == 6 && s_l2_cursor == 2)
            {
                static float s_test_curr_val = 12.0f;
                open_float_editor("TEST CURRENT mA", &s_test_curr_val, "%05.2f", 0.0f, 22.0f);
            }
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_MENU_L4: Deepest Level Menus (Relay ON/OFF, Scanty Times, Curr Cal)  */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_MENU_L4)
    {
        uint8_t count = 2;
        if (s_l1_cursor == 5 && s_l2_cursor == 1) count = 3; /* Scanty Times: 1, 5, 10 min */

        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Back to L3 */
        {
            s_screen = UI_MENU_L3;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Cursor Down */
        {
            s_l4_cursor = (s_l4_cursor + 1) % count;
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Cursor Up */
        {
            s_l4_cursor = (s_l4_cursor + count - 1) % count;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Execute action */
        {
            /* Scanty Power timeout */
            if (s_l1_cursor == 5 && s_l2_cursor == 1)
            {
                s_cfg->backlight_mode = 0;
                s_cfg->backlight_timeout = (s_l4_cursor == 0) ? 1 : ((s_l4_cursor == 1) ? 5 : 10);
                meter_flash_save(s_cfg);
                s_screen = UI_MENU_L2;
                lcd_clear();
            }
            /* Relay Test ON/OFF */
            else if (s_l1_cursor == 6 && s_l2_cursor == 1)
            {
                if (s_l3_cursor == 0) /* Relay I */
                {
                    if (s_l4_cursor == 0)
                    {
                        s_relay_override[0] = 1;
                        BOARD_RELAY1_ON();
                    }
                    else
                    {
                        s_relay_override[0] = 0;
                        BOARD_RELAY1_OFF();
                    }
                }
                else /* Relay II */
                {
                    if (s_l4_cursor == 0)
                    {
                        s_relay_override[1] = 1;
                        BOARD_RELAY2_ON();
                    }
                    else
                    {
                        s_relay_override[1] = 0;
                        BOARD_RELAY2_OFF();
                    }
                }
                s_screen = UI_MENU_L3;
                lcd_clear();
            }
            /* Current CAL point */
            else if (s_l1_cursor == 6 && s_l2_cursor == 0)
            {
                static float s_cal_ma_val = 4.0f;
                s_cal_ma_val = (s_l4_cursor == 0) ? 4.0f : 20.0f;
                open_float_editor("TRIM CURRENT mA", &s_cal_ma_val, "%05.2f", 0.0f, 25.0f);
            }
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_CONTRAST_EDITOR: Interactive Real-Time Contrast Screen              */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_CONTRAST_EDITOR)
    {
        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Cancel / Revert */
        {
            s_cfg->contrast = s_contrast_orig;
            lcd_set_contrast(s_cfg->contrast);
            s_screen = UI_MENU_L2;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Decrease contrast (-1) */
        {
            if (s_cfg->contrast > 0)
            {
                s_cfg->contrast--;
                lcd_set_contrast(s_cfg->contrast);
            }
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Increase contrast (+1) */
        {
            if (s_cfg->contrast < 63)
            {
                s_cfg->contrast++;
                lcd_set_contrast(s_cfg->contrast);
            }
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Save to Flash and return */
        {
            meter_flash_save(s_cfg);
            s_screen = UI_MENU_L2;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_OPTION_PICKER: Option Selection List with '*' for saved item        */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_OPTION_PICKER)
    {
        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Cancel */
        {
            s_screen = s_prev_screen;
            lcd_clear();
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Cursor Down */
        {
            s_opt_cursor = (s_opt_cursor + 1) % s_opt_count;
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Cursor Up */
        {
            s_opt_cursor = (s_opt_cursor + s_opt_count - 1) % s_opt_count;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Save parameter and return */
        {
            if (s_opt_target)
            {
                *s_opt_target = s_opt_cursor;
            }
            if (s_opt_callback)
            {
                s_opt_callback(s_opt_cursor);
            }
            meter_flash_save(s_cfg);
            s_screen = s_prev_screen;
            lcd_clear();
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_NUMERIC_EDITOR: Interactive Float / Integer Value Editor            */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_NUMERIC_EDITOR)
    {
        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Cancel */
        {
            s_screen = s_prev_screen;
            lcd_clear();
        }
        else if (ev == BTN_EVT_RIGHT_CLICK) /* SW2: Move cursor right across valid characters only */
        {
            s_edit_cursor++;
            if (s_edit_cursor >= s_edit_len)
            {
                s_edit_cursor = 0; /* Loop back to first character; never enters empty space */
            }
            if (s_edit_buf[s_edit_cursor] == '.')
            {
                s_edit_cursor++;
                if (s_edit_cursor >= s_edit_len)
                {
                    s_edit_cursor = 0;
                }
            }
        }
        else if (ev == BTN_EVT_DOWN_CLICK) /* SW3: Subtract 1 / toggle sign */
        {
            char c = s_edit_buf[s_edit_cursor];
            if (c >= '0' && c <= '9')
            {
                c = (c == '0') ? '9' : (c - 1);
            }
            else if (c == '-')
            {
                c = '+';
            }
            else if (c == '+')
            {
                c = '-';
            }
            s_edit_buf[s_edit_cursor] = c;
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Parse, validate range, save */
        {
            float val = 0.0f;
            atof_custom(s_edit_buf, &val);

            if (val < s_edit_min || val > s_edit_max)
            {
                /* Out of range: Display alert */
                lcd_print_centered(5, "! OUT OF RANGE !");
                HAL_Delay(1200);
                lcd_clear_page(5);
            }
            else
            {
                if (s_edit_target_ptr)
                {
                    if (s_edit_type == EDIT_TYPE_FLOAT)
                    {
                        *(float*)s_edit_target_ptr = val;
                    }
                    else if (s_edit_type == EDIT_TYPE_UINT8)
                    {
                        *(uint8_t*)s_edit_target_ptr = (uint8_t)val;
                    }
                    else if (s_edit_type == EDIT_TYPE_UINT16)
                    {
                        *(uint16_t*)s_edit_target_ptr = (uint16_t)val;
                    }
                }
                meter_flash_save(s_cfg);

                /* If this was setting reference conductivity for Point CAL, proceed to wizard */
                if (s_edit_cal_point < 5)
                {
                    start_cal_wizard(s_edit_cal_point);
                }
                else
                {
                    s_screen = s_prev_screen;
                    lcd_clear();
                }
            }
        }
    }

    /* ---------------------------------------------------------------------- */
    /* UI_CAL_WIZARD: Point-by-Point Calibration Screen                       */
    /* ---------------------------------------------------------------------- */
    else if (s_screen == UI_CAL_WIZARD)
    {
        if (ev == BTN_EVT_MENU_CLICK) /* SW4: Cancel */
        {
            s_screen = UI_MENU_L3;
            lcd_clear();
        }
        else if (ev == BTN_EVT_ENTER_CLICK) /* SW1: Save Calibration Point */
        {
            const meter_reading_t *r = meter_data();
            if (r)
            {
                /* Validate points must rise: reference must exceed previous standard */
                bool valid_order = true;
                if (s_cal_point_idx > 0)
                {
                    if (s_cfg->cal_std[s_cal_point_idx] <= s_cfg->cal_std[s_cal_point_idx - 1])
                    {
                        valid_order = false;
                    }
                }

                if (!valid_order)
                {
                    lcd_print_centered(5, "MUST BE > PREV STD");
                    HAL_Delay(1500);
                    lcd_clear_page(5);
                }
                else
                {
                    /* Compute measured EC at 25 C for calibration reference */
                    float comp_factor = 1.0f + (s_cfg->temp_coef / 100.0f) * (r->temp_c - 25.0f);
                    if (comp_factor < 0.1f) comp_factor = 0.1f;
                    float ec_25 = r->cond_raw_us / comp_factor;

                    s_cfg->cal_raw[s_cal_point_idx] = ec_25;
                    if ((s_cal_point_idx + 1) > s_cfg->cal_points_count)
                    {
                        s_cfg->cal_points_count = s_cal_point_idx + 1;
                    }
                    meter_flash_save(s_cfg);

                    lcd_print_centered(5, "* POINT STORED *");
                    HAL_Delay(1000);
                    s_screen = UI_MENU_L3;
                    lcd_clear();
                }
            }
        }
    }

    /* Screen Redraw timing: 500 ms / 2 Hz in live measurement to eliminate jitter & eye fatigue */
    static uint32_t last_redraw_tick = 0;

    if (ev != BTN_EVT_NONE ||
       (s_screen == UI_MEASURE && (now - last_redraw_tick) >= 500) ||
       (s_screen == UI_STATUS_SCREEN && (now - last_redraw_tick) >= 500) ||
       (s_screen == UI_CAL_WIZARD && (now - last_redraw_tick) >= 500))
    {
        last_redraw_tick = now;
        render_screen();
    }
}
