/**
 * @file    serial_port_mointor.c
 * @brief   Real-time serial port monitor, telemetry stream, and command parser
 *          for Mirrant TDS 4002 Conductivity/TDS Controller.
 *          Target MCU: STM32G030K8T6 (16 MHz HSI)
 *          Hardware Mapping:
 *            PB6 = USART1_TX (AF0, MCU Pin 30)
 *            PB7 = USART1_RX (AF0, MCU Pin 31)
 *            PB5 = RS-485 Transceiver RE/DE Control (MCU Pin 29, HIGH=TX, LOW=RX)
 *            J3  = 5-Pin SWD / Debug / Ground Header (Pin 4=GND, Pin 1=3.3V)
 */

#include "serial_port_mointor.h"
#include "turbidity_sensor.h"
#include "board_pins.h"
#include "meter_core.h"
#include "stm32g030xx.h"
#include "stm32g0xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define RX_BUF_SIZE         128
#define TX_BUF_SIZE         256
#define RX_ISR_SIZE         256

/* Receiver State */
static uint8_t  s_rx_buf[RX_BUF_SIZE];
static uint16_t s_rx_idx = 0;
static uint32_t s_last_rx_time = 0;
static uint32_t s_char_timeout_ms = 4;

/* Interrupt-driven RX ring buffer (prevents byte drops during ADC/LCD tasks) */
static volatile uint8_t  s_rx_isr_ring[RX_ISR_SIZE];
static volatile uint16_t s_rx_isr_head = 0;
static volatile uint16_t s_rx_isr_tail = 0;
static volatile uint32_t s_rx_isr_last_tick = 0;

/* Streaming State */
static bool     s_streaming_enabled = false;     /* False: quiet listening for Modbus & commands; type 's' to enable live stream */
static bool     s_json_mode = false;             /* false = Human Text, true = JSON */
static uint32_t s_stream_interval_ms = 200;      /* Default 200ms FAST live feed (5 Hz) */
static uint32_t s_last_stream_time = 0;

/* Shadow Settings for Runtime Change Detection */
static settings_t s_shadow_cfg;
static bool       s_shadow_valid = false;

/* Previous Relay States for Real-Time Event Notification */
static int8_t     s_prev_relay1_state = -1;
static int8_t     s_prev_relay2_state = -1;

/* Manual Relay Overrides (-1 = Automatic via logic, 0 = Force OFF, 1 = Force ON) */
static int8_t   s_relay1_override = -1;
static int8_t   s_relay2_override = -1;

/* Scratch Transmit Buffer */
static uint8_t  s_tx_buf[TX_BUF_SIZE];

/* ========================================================================== */
/*                     HELPER: LIGHTWEIGHT FLOAT FORMATTER                    */
/* ========================================================================== */
/**
 * @brief  Format a float into a string without standard library %f overhead.
 */
static void format_float(float val, int decimals, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;

    if (val < 0.0f)
    {
        if (out_size > 1) { *out++ = '-'; out_size--; }
        val = -val;
    }

    /* Rounding offset */
    float rounder = 0.5f;
    for (int i = 0; i < decimals; i++)
    {
        rounder /= 10.0f;
    }
    val += rounder;

    uint32_t int_part = (uint32_t)val;
    float remainder = val - (float)int_part;

    char temp[16];
    int t_idx = 0;
    if (int_part == 0)
    {
        temp[t_idx++] = '0';
    }
    else
    {
        uint32_t n = int_part;
        while (n > 0 && t_idx < 14)
        {
            temp[t_idx++] = (char)('0' + (n % 10));
            n /= 10;
        }
    }

    size_t o_idx = 0;
    while (t_idx > 0 && o_idx < out_size - 1)
    {
        out[o_idx++] = temp[--t_idx];
    }

    if (decimals > 0 && o_idx < out_size - 1)
    {
        out[o_idx++] = '.';
        for (int i = 0; i < decimals && o_idx < out_size - 1; i++)
        {
            remainder *= 10.0f;
            uint32_t digit = (uint32_t)remainder;
            if (digit > 9) digit = 9;
            out[o_idx++] = (char)('0' + digit);
            remainder -= (float)digit;
        }
    }
    out[o_idx] = '\0';
}

/* ========================================================================== */
/*                       HARDWARE INITIALIZATION (USART1)                     */
/* ========================================================================== */
void serial_monitor_init(const settings_t *s)
{
    /* 1. Clocks: GPIOB and USART1 */
    RCC->IOPENR   |= RCC_IOPENR_GPIOBEN;
    RCC->APBENR2  |= RCC_APBENR2_USART1EN;

    /* 2. Configure PB5 as Push-Pull Output (RS-485 DE/RE control) */
    GPIOB->MODER   &= ~(3UL << (5 * 2));
    GPIOB->MODER   |= (1UL << (5 * 2));
    GPIOB->BSRR     = (1UL << (5 + 16)); /* Start in RX mode (PB5 = LOW) */

    /* 3. Configure PB6 (TX) and PB7 (RX) as AF0 (USART1) */
    GPIOB->MODER   &= ~((3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOB->MODER   |= ((2UL << (6 * 2)) | (2UL << (7 * 2)));
    GPIOB->AFR[0]  &= ~((0xFUL << (6 * 4)) | (0xFUL << (7 * 4))); /* AF0 */

    /* 4. Configure Baud Rate */
    uint32_t baud = (s && s->baud_rate >= 4800) ? s->baud_rate : 9600;
    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = USART_CR3_OVRDIS; /* Disable overrun lockup so RX never freezes */
    USART1->BRR = (SystemCoreClock + (baud / 2)) / baud;

    /* Configure Parity and Stop Bits */
    if (s && s->parity == PARITY_EVEN)
    {
        USART1->CR1 |= USART_CR1_PCE | USART_CR1_M0; /* 8 data bits + 1 parity */
    }
    else if (s && s->parity == PARITY_ODD)
    {
        USART1->CR1 |= USART_CR1_PCE | USART_CR1_PS | USART_CR1_M0;
    }

    if (s && s->stop_bits == STOP_2_BIT)
    {
        USART1->CR2 |= USART_CR2_STOP_1; /* 2 stop bits */
    }

    /* Modbus 3.5 character frame timeout */
    s_char_timeout_ms = (38500UL / baud) + 2;

    /* Clear any initial error flags */
    USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF | USART_ICR_TCCF;

    s_rx_idx = 0;
    s_rx_isr_head = 0;
    s_rx_isr_tail = 0;
    s_last_rx_time = HAL_GetTick();
    s_rx_isr_last_tick = s_last_rx_time;
    s_last_stream_time = HAL_GetTick();

    /* 5. Enable USART1 Receiver, Transmitter, and RXNE interrupt */
    USART1->CR1 |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);

    if (s)
    {
        s_shadow_cfg = *s;
        s_shadow_valid = true;
    }

    /* Send initial startup banner */
    serial_monitor_send_str("\r\n=======================================================\r\n");
    serial_monitor_send_str("  MIRRANT TDS 4002 SERIAL MONITOR INITIALIZED\r\n");
    serial_monitor_send_str("  RS-485 USART1: PB6(TX), PB7(RX), PB5(RE/DE)\r\n");
    serial_monitor_send_str("  SWD / Ground  : J3 Header (Pin 4 = GND)\r\n");
    serial_monitor_send_str("  Type '?' or 'h' for interactive terminal commands\r\n");
    serial_monitor_send_str("=======================================================\r\n");
}

/* ========================================================================== */
/*                RS-485 100% NON-BLOCKING TRANSMISSION ENGINE                */
/* ========================================================================== */
#define TX_RING_SIZE        1024

static uint8_t  s_tx_ring[TX_RING_SIZE];
static volatile uint16_t s_tx_ring_head = 0;
static volatile uint16_t s_tx_ring_tail = 0;
static volatile bool     s_rs485_tx_active = false;

void serial_monitor_usart1_irq(void)
{
    uint32_t isr = USART1->ISR;

    /* Clear framing, noise, parity, or overrun error flags */
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
    {
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }

    /* 1. Drain RX hardware register into lock-free ring buffer */
    while (USART1->ISR & USART_ISR_RXNE_RXFNE)
    {
        uint8_t byte = (uint8_t)USART1->RDR;
        /* Ignore self-echo if RS-485 DE is active */
        if (!s_rs485_tx_active)
        {
            uint16_t next = (uint16_t)((s_rx_isr_head + 1U) % RX_ISR_SIZE);
            if (next != s_rx_isr_tail)
            {
                s_rx_isr_ring[s_rx_isr_head] = byte;
                s_rx_isr_head = next;
                s_rx_isr_last_tick = HAL_GetTick();
            }
        }
    }

    /* 2. Feed TX ring buffer while bytes remain */
    if ((USART1->CR1 & USART_CR1_TXEIE_TXFNFIE) && (isr & USART_ISR_TXE_TXFNF))
    {
        if (s_tx_ring_head != s_tx_ring_tail)
        {
            USART1->TDR = s_tx_ring[s_tx_ring_tail];
            s_tx_ring_tail = (uint16_t)((s_tx_ring_tail + 1U) % TX_RING_SIZE);
        }
        else
        {
            USART1->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
            USART1->CR1 |= USART_CR1_TCIE;
        }
    }

    /* 3. Instant RS-485 DE/RE LOW turnaround when last stop bit completes */
    if ((USART1->CR1 & USART_CR1_TCIE) && (USART1->ISR & USART_ISR_TC))
    {
        USART1->CR1 &= ~USART_CR1_TCIE;
        USART1->ICR = USART_ICR_TCCF | USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        if (s_tx_ring_head == s_tx_ring_tail)
        {
            GPIOB->BSRR = (1UL << (5 + 16)); /* Drive PB5 LOW -> Receive Mode */
            s_rs485_tx_active = false;
        }
        else
        {
            USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;
        }
    }
}

static void serial_monitor_pump_tx(void)
{
    __disable_irq();
    if (s_tx_ring_head != s_tx_ring_tail)
    {
        if (!s_rs485_tx_active)
        {
            /* Drive PB5 HIGH -> Transmit Mode */
            GPIOB->BSRR = (1UL << 5);
            s_rs485_tx_active = true;
            for (volatile int i = 0; i < 20; i++) { __NOP(); }
        }
        USART1->CR1 &= ~USART_CR1_TCIE;
        USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    }
    __enable_irq();
}

void serial_monitor_send_bytes(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;

    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t next_head = (uint16_t)((s_tx_ring_head + 1U) % TX_RING_SIZE);
        while (next_head == s_tx_ring_tail)
        {
            /* Wait for ISR to drain a byte */
            serial_monitor_pump_tx();
        }

        s_tx_ring[s_tx_ring_head] = data[i];
        s_tx_ring_head = next_head;
    }

    serial_monitor_pump_tx();
}

void serial_monitor_send_str(const char *str)
{
    if (!str) return;
    serial_monitor_send_bytes((const uint8_t *)str, (uint16_t)strlen(str));
}

/* ========================================================================== */
/*                   REAL-TIME SENSOR DATA FORMATTING                         */
/* ========================================================================== */
/* ========================================================================== */
/*                   REAL-TIME SENSOR DATA FORMATTING                         */
/* ========================================================================== */
void serial_monitor_send_reading(const meter_reading_t *r, const settings_t *s)
{
    if (!r) return;

    char cond_buf[12], raw_buf[12], temp_buf[10], tds_buf[12], res_buf[10], sal_buf[10], turb_buf[12];
    char i1_buf[10], i2_buf[10], rcell_buf[12], rtemp_buf[12];
    char k_buf[10], tc_buf[10], tds_f_buf[10], tcal_buf[10], ecfac_buf[10], ecoff_buf[10];

    char unit_c = (s && s->temp_unit == 1) ? 'F' : 'C';
    float disp_temp = (s && s->temp_unit == 1) ? r->temp_f : r->temp_c;

    /* Format all measurement numerical values (ensures clean display even if 0.0) */
    format_float(r->cond_us, 1, cond_buf, sizeof(cond_buf));
    format_float(r->turb_ntu, 1, turb_buf, sizeof(turb_buf));
    format_float(r->cond_raw_us, 1, raw_buf, sizeof(raw_buf));
    format_float(disp_temp, 1, temp_buf, sizeof(temp_buf));
    format_float(r->tds_ppm, 1, tds_buf, sizeof(tds_buf));
    format_float(r->res_mohm_cm, 3, res_buf, sizeof(res_buf));
    format_float(r->sal_ppt, 2, sal_buf, sizeof(sal_buf));
    format_float(r->current1_ma, 2, i1_buf, sizeof(i1_buf));
    format_float(r->current2_ma, 2, i2_buf, sizeof(i2_buf));
    if (r->fault_open || r->cell_ohms <= 0.0f)
    {
        strcpy(rcell_buf, "OPEN");
    }
    else
    {
        format_float(r->cell_ohms, 1, rcell_buf, sizeof(rcell_buf));
    }
    if (r->fault_temp || r->temp_ohms <= 0.0f)
    {
        strcpy(rtemp_buf, "OPEN");
    }
    else
    {
        format_float(r->temp_ohms, 1, rtemp_buf, sizeof(rtemp_buf));
    }

    /* Active configuration tags */
    if (s)
    {
        format_float(s->k_constant, 4, k_buf, sizeof(k_buf));
        format_float(s->temp_coef, 2, tc_buf, sizeof(tc_buf));
        format_float(s->tds_coef, 3, tds_f_buf, sizeof(tds_f_buf));
        format_float(s->temp_cal_offset, 1, tcal_buf, sizeof(tcal_buf));
        format_float(s->ec_factor, 3, ecfac_buf, sizeof(ecfac_buf));
        format_float(s->ec_offset, 1, ecoff_buf, sizeof(ecoff_buf));
    }
    else
    {
        strcpy(k_buf, "1.000");
        strcpy(tc_buf, "2.00");
        strcpy(tds_f_buf, "0.500");
        strcpy(tcal_buf, "0.0");
        strcpy(ecfac_buf, "1.000");
        strcpy(ecoff_buf, "0.0");
    }

    uint8_t r1 = (s_relay1_override >= 0) ? (uint8_t)s_relay1_override : r->relay1_state;
    uint8_t r2 = (s_relay2_override >= 0) ? (uint8_t)s_relay2_override : r->relay2_state;
    const char *mode_str = (s && s->measure_mode == 1) ? "SIM" : "REAL";

    char msg[512];
    snprintf(msg, sizeof(msg),
             "[FEED] COND:%s uS/cm | TURB:%s NTU | RAW:%s | TEMP:%s %c | TDS:%s ppm | RES:%s M-Ohm | SAL:%s ppt | R1:%s | R2:%s | I1:%smA | I2:%smA | R_CELL:%s | R_TEMP:%s [K:%s TC:%s%% TDS_F:%s T_CAL:%s EC_FAC:%s EC_OFF:%s UNIT:%c %s]\r\n",
             cond_buf, turb_buf, raw_buf, temp_buf, unit_c, tds_buf, res_buf, sal_buf,
             r1 ? "ON" : "OFF",
             r2 ? "ON" : "OFF",
             i1_buf, i2_buf,
             rcell_buf, rtemp_buf,
             k_buf, tc_buf, tds_f_buf, tcal_buf, ecfac_buf, ecoff_buf, unit_c, mode_str);

    serial_monitor_send_str(msg);
}

void serial_monitor_send_json(const meter_reading_t *r, const settings_t *s)
{
    if (!r) return;

    char cond_buf[12], raw_buf[12], temp_buf[10], tempf_buf[10], tds_buf[12], res_buf[10], sal_buf[10], turb_buf[12];
    char i1_buf[10], i2_buf[10], rcell_buf[12], rtemp_buf[12];
    char k_buf[10], tc_buf[10], tds_f_buf[10], tcal_buf[10], ecfac_buf[10], ecoff_buf[10];

    format_float(r->cond_us, 1, cond_buf, sizeof(cond_buf));
    format_float(r->turb_ntu, 1, turb_buf, sizeof(turb_buf));
    format_float(r->cond_raw_us, 1, raw_buf, sizeof(raw_buf));
    format_float(r->temp_c, 1, temp_buf, sizeof(temp_buf));
    format_float(r->temp_f, 1, tempf_buf, sizeof(tempf_buf));
    format_float(r->tds_ppm, 1, tds_buf, sizeof(tds_buf));
    format_float(r->res_mohm_cm, 3, res_buf, sizeof(res_buf));
    format_float(r->sal_ppt, 2, sal_buf, sizeof(sal_buf));
    format_float(r->current1_ma, 2, i1_buf, sizeof(i1_buf));
    format_float(r->current2_ma, 2, i2_buf, sizeof(i2_buf));
    if (r->fault_open || r->cell_ohms <= 0.0f)
    {
        strcpy(rcell_buf, "OPEN");
    }
    else
    {
        format_float(r->cell_ohms, 1, rcell_buf, sizeof(rcell_buf));
    }
    if (r->fault_temp || r->temp_ohms <= 0.0f)
    {
        strcpy(rtemp_buf, "OPEN");
    }
    else
    {
        format_float(r->temp_ohms, 1, rtemp_buf, sizeof(rtemp_buf));
    }

    if (s)
    {
        format_float(s->k_constant, 4, k_buf, sizeof(k_buf));
        format_float(s->temp_coef, 2, tc_buf, sizeof(tc_buf));
        format_float(s->tds_coef, 3, tds_f_buf, sizeof(tds_f_buf));
        format_float(s->temp_cal_offset, 1, tcal_buf, sizeof(tcal_buf));
        format_float(s->ec_factor, 3, ecfac_buf, sizeof(ecfac_buf));
        format_float(s->ec_offset, 1, ecoff_buf, sizeof(ecoff_buf));
    }
    else
    {
        strcpy(k_buf, "1.000");
        strcpy(tc_buf, "2.00");
        strcpy(tds_f_buf, "0.500");
        strcpy(tcal_buf, "0.0");
        strcpy(ecfac_buf, "1.000");
        strcpy(ecoff_buf, "0.0");
    }

    uint8_t r1 = (s_relay1_override >= 0) ? (uint8_t)s_relay1_override : r->relay1_state;
    uint8_t r2 = (s_relay2_override >= 0) ? (uint8_t)s_relay2_override : r->relay2_state;

    char json[512];
    snprintf(json, sizeof(json),
             "{\"time\":%lu,\"cond\":%s,\"turb\":%s,\"raw\":%s,\"temp\":%s,\"tempf\":%s,\"unit\":\"%c\",\"tds\":%s,\"res\":%s,\"sal\":%s,\"r1\":%d,\"r2\":%d,\"i1\":%s,\"i2\":%s,\"rcell\":\"%s\",\"rtemp\":\"%s\",\"k\":%s,\"tc\":%s,\"tdsf\":%s,\"tcal\":%s,\"ecfac\":%s,\"ecoff\":%s,\"sim\":%d}\r\n",
             HAL_GetTick(),
             cond_buf, turb_buf, raw_buf, temp_buf, tempf_buf, (s && s->temp_unit == 1) ? 'F' : 'C',
             tds_buf, res_buf, sal_buf,
             r1, r2, i1_buf, i2_buf,
             rcell_buf, rtemp_buf,
             k_buf, tc_buf, tds_f_buf, tcal_buf, ecfac_buf, ecoff_buf,
             (s && s->measure_mode == 1) ? 1 : 0);

    serial_monitor_send_str(json);
}

/* ========================================================================== */
/*                     INTERACTIVE COMMAND PROCESSOR                          */
/* ========================================================================== */
static void print_help(void)
{
    serial_monitor_send_str("\r\n=======================================================\r\n");
    serial_monitor_send_str("             MIRRANT TDS 4002 COMMAND HELP\r\n");
    serial_monitor_send_str("=======================================================\r\n");
    serial_monitor_send_str("  ? / h : Display this command help menu\r\n");
    serial_monitor_send_str("  d / r : Read single real-time sensor snapshot\r\n");
    serial_monitor_send_str("  s     : Toggle auto-streaming (ON / OFF)\r\n");
    serial_monitor_send_str("  j     : Toggle JSON stream output (JSON / TEXT)\r\n");
    serial_monitor_send_str("  f     : Toggle stream rate (200ms FAST / 100ms / 500ms / 1000ms)\r\n");
    serial_monitor_send_str("  m     : Print active calculation formulas & live math breakdown\r\n");
    serial_monitor_send_str("  1     : Toggle Relay 1 manual state (Auto/ON/OFF)\r\n");
    serial_monitor_send_str("  2     : Toggle Relay 2 manual state (Auto/ON/OFF)\r\n");
    serial_monitor_send_str("  c     : Print active device settings & calibration\r\n");
    serial_monitor_send_str("  v     : Print firmware version & pinout details\r\n");
    serial_monitor_send_str("=======================================================\r\n");
}

static void print_version(void)
{
    serial_monitor_send_str("\r\n=======================================================\r\n");
    serial_monitor_send_str("Mirrant TDS 4002 Conductivity & TDS Controller\r\n");
    serial_monitor_send_str("Firmware Version: v1.0.5 PRO (Live Telemetry & Math Engine)\r\n");
    serial_monitor_send_str("Target MCU      : STM32G030K8T6 (64KB Flash, 8KB RAM, 16MHz)\r\n");
    serial_monitor_send_str("Hardware Pins   : USART1 TX=PB6, RX=PB7, RS-485 DE/RE=PB5\r\n");
    serial_monitor_send_str("Debug / GND     : J3 Header (Pin 4 = GND, Pin 1 = 3.3V)\r\n");
    serial_monitor_send_str("=======================================================\r\n");
}

static void print_formulas(const meter_reading_t *r, const settings_t *s)
{
    serial_monitor_send_str("\r\n========================================================================\r\n");
    serial_monitor_send_str("       MIRRANT TDS 4002 - LIVE MATHEMATICAL CALCULATION BREAKDOWN\r\n");
    serial_monitor_send_str("========================================================================\r\n");

    if (r && s)
    {
        char k_b[10], tc_b[10], tds_b[10], tcal_b[10], ecfac_b[10], ecoff_b[10];
        char rcell_b[12], rtemp_b[12];
        char raw_b[12], comp_f_b[10], ec25_b[12], cond_b[12], res_b[10], sal_b[10];
        char temp_c_b[10], temp_f_b[10], tmeas_b[10], tds_val_b[12];
        char i1_b[10], i2_b[10];
        char r1_on[10], r1_off[10], r2_on[10], r2_off[10];

        format_float(s->k_constant, 4, k_b, sizeof(k_b));
        format_float(s->temp_coef, 2, tc_b, sizeof(tc_b));
        format_float(s->tds_coef, 3, tds_b, sizeof(tds_b));
        format_float(s->temp_cal_offset, 1, tcal_b, sizeof(tcal_b));
        format_float(s->ec_factor, 3, ecfac_b, sizeof(ecfac_b));
        format_float(s->ec_offset, 1, ecoff_b, sizeof(ecoff_b));
        format_float(r->cell_ohms, 1, rcell_b, sizeof(rcell_b));
        format_float(r->temp_ohms, 1, rtemp_b, sizeof(rtemp_b));

        float t_meas = r->temp_c - s->temp_cal_offset;
        float comp_fac = 1.0f + (s->temp_coef / 100.0f) * (r->temp_c - 25.0f);
        if (comp_fac < 0.1f) comp_fac = 0.1f;
        float ec_25 = (comp_fac > 0.0f) ? (r->cond_raw_us / comp_fac) : 0.0f;

        format_float(t_meas, 1, tmeas_b, sizeof(tmeas_b));
        format_float(r->temp_c, 1, temp_c_b, sizeof(temp_c_b));
        format_float(r->temp_f, 1, temp_f_b, sizeof(temp_f_b));
        format_float(r->cond_raw_us, 1, raw_b, sizeof(raw_b));
        format_float(comp_fac, 3, comp_f_b, sizeof(comp_f_b));
        format_float(ec_25, 1, ec25_b, sizeof(ec25_b));
        format_float(r->cond_us, 1, cond_b, sizeof(cond_b));
        format_float(r->tds_ppm, 1, tds_val_b, sizeof(tds_val_b));
        format_float(r->res_mohm_cm, 3, res_b, sizeof(res_b));
        format_float(r->sal_ppt, 2, sal_b, sizeof(sal_b));
        format_float(r->current1_ma, 2, i1_b, sizeof(i1_b));
        format_float(r->current2_ma, 2, i2_b, sizeof(i2_b));
        format_float(s->relay1_on_val, 1, r1_on, sizeof(r1_on));
        format_float(s->relay1_off_val, 1, r1_off, sizeof(r1_off));
        format_float(s->relay2_on_val, 1, r2_on, sizeof(r2_on));
        format_float(s->relay2_off_val, 1, r2_off, sizeof(r2_off));

        const char *p_type = (s->temp_probe_type == 0) ? "PT1000" : (s->temp_probe_type == 1 ? "NTC10K" : "MANUAL");
        char unit_char = (s->temp_unit == 1) ? 'F' : 'C';

        /* Conductance 1/R in Siemens */
        float conductance = (r->cell_ohms > 0.0f) ? (1.0f / r->cell_ohms) : 0.0f;
        char cond_siemens_b[16];
        format_float(conductance, 6, cond_siemens_b, sizeof(cond_siemens_b));

        float t_gap = r->temp_c - 25.0f;
        char tgap_b[10];
        format_float(t_gap, 1, tgap_b, sizeof(tgap_b));

        float ec_ms = r->cond_us / 1000.0f;
        char ec_ms_b[10];
        format_float(ec_ms, 3, ec_ms_b, sizeof(ec_ms_b));

        float res_ohm = r->res_mohm_cm * 1000000.0f;
        char res_ohm_b[12];
        format_float(res_ohm, 1, res_ohm_b, sizeof(res_ohm_b));

        char sa_b[10], sb_b[10], sc_b[10], sd_b[10], sfval_b[14];
        format_float(s->sample_a, 2, sa_b, sizeof(sa_b));
        format_float(s->sample_b, 2, sb_b, sizeof(sb_b));
        format_float(s->sample_c, 2, sc_b, sizeof(sc_b));
        format_float(s->sample_d, 2, sd_b, sizeof(sd_b));
        format_float(r->sample_fval, 2, sfval_b, sizeof(sfval_b));

        /* 1. INPUT PARAMETERS */
        char in_buf[700];
        snprintf(in_buf, sizeof(in_buf),
                 "[1. RAW SENSOR INPUTS & CONFIGURATION]\r\n"
                 "  Raw Cell Resistance (R_cell) : %s Ohms\r\n"
                 "  Temp Sensor Resistance(R_temp): %s Ohms (Type: %s)\r\n"
                 "  Cell Constant (K)            : %s 1/cm\r\n"
                 "  Temp Compensation Coeff (α)  : %s %%/C (Ref: 25.0 C)\r\n"
                 "  Temp Calibration Offset      : %s C (Unit: %c)\r\n"
                 "  Probe Cal Factor (F) / Offset: Factor=%s | Offset=%s uS/cm (Pts: %d)\r\n"
                 "  TDS Factor (C) / Salinity (S): TDS_C=%s | Sal_S=0.500\r\n"
                 "  Sample Test Inputs (a,b,c,d) : a=%s, b=%s, c=%s, d=%s\r\n"
                 "------------------------------------------------------------------------\r\n",
                 rcell_b, rtemp_b, p_type,
                 k_b, tc_b, tcal_b, unit_char,
                 ecfac_b, ecoff_b, s->cal_points_count,
                 tds_b,
                 sa_b, sb_b, sc_b, sd_b);
        serial_monitor_send_str(in_buf);
        serial_monitor_pump_tx();

        /* 2. STEP-BY-STEP MATHEMATICAL CALCULATIONS */
        char calc_buf1[800];
        snprintf(calc_buf1, sizeof(calc_buf1),
                 "[2. STEP-BY-STEP ELECTROCHEMICAL CALCULATION ENGINE]\r\n"
                 "  Step 1: Conductivity (EC)\r\n"
                 "    Expression: EC25 = [ K x (1 / R) x 1,000,000 ] / [ 1 + α x (T - 25) ]\r\n"
                 "    • Conductance (1 / R)       : 1 / %s = %s Siemens\r\n"
                 "    • Multiply by Cell Constant : K x %s = %s x %s = %s S/cm\r\n"
                 "    • Convert to uS/cm (EC_raw) : %s uS/cm (at sample temp %s C)\r\n"
                 "    • Temperature Gap (T - 25)  : %s - 25.0 = %s C\r\n"
                 "    • Correction Factor         : 1 + (α/100) x %s = %s\r\n"
                 "    • Bring back to 25 C (EC25) : %s / %s = %s uS/cm\r\n"
                 "    • Calibration Step (EC)     : EC25 x F + O = %s x %s + %s = %s uS/cm\r\n",
                 rcell_b, cond_siemens_b,
                 cond_siemens_b, k_b, cond_siemens_b, cond_siemens_b,
                 raw_b, temp_c_b,
                 temp_c_b, tgap_b,
                 tgap_b, comp_f_b,
                 raw_b, comp_f_b, ec25_b,
                 ec25_b, ecfac_b, ecoff_b, cond_b);
        serial_monitor_send_str(calc_buf1);
        serial_monitor_pump_tx();

        char calc_buf2[500];
        snprintf(calc_buf2, sizeof(calc_buf2),
                 "\r\n  Step 2: Resistivity (ρ)\r\n"
                 "    Expression: ρ (M-Ohm*cm) = 1 / EC (uS/cm)\r\n"
                 "    • EC = %s uS/cm\r\n"
                 "    • ρ  = 1 / %s = %s M-Ohm*cm (Capped at 18.0)\r\n"
                 "    • In Ohm-cm: %s x 1,000,000 = %s Ohm*cm\r\n"
                 "\r\n  Step 3: Total Dissolved Solids (TDS)\r\n"
                 "    Expression: TDS (ppm) = EC x C\r\n"
                 "    • EC = %s uS/cm, C = %s\r\n"
                 "    • TDS = %s x %s = %s ppm\r\n",
                 cond_b,
                 cond_b, res_b,
                 res_b, res_ohm_b,
                 cond_b, tds_b,
                 cond_b, tds_b, tds_val_b);
        serial_monitor_send_str(calc_buf2);
        serial_monitor_pump_tx();

        char calc_buf3[750];
        snprintf(calc_buf3, sizeof(calc_buf3),
                 "\r\n  Step 4: Salinity (SAL)\r\n"
                 "    Expression: Salinity (ppt) = (EC / 1000) x S\r\n"
                 "    • Convert to mS/cm: %s / 1000 = %s mS/cm\r\n"
                 "    • Multiply by Factor (0.50): %s x 0.500 = %s ppt\r\n"
                 "\r\n  Step 5: Sample Test Formula\r\n"
                 "    Expression: FVALUE = (a + b) x (c - d)\r\n"
                 "    • FVALUE = (%s + %s) x (%s - %s) = %s\r\n"
                 "\r\n  Step 6: Relays & Current Outputs\r\n"
                 "    • Current I1 = %s mA | Current I2 = %s mA\r\n"
                 "    • Relay 1: Func %d (ON: %s, OFF: %s) -> %s\r\n"
                 "    • Relay 2: Func %d (ON: %s, OFF: %s) -> %s\r\n"
                 "------------------------------------------------------------------------\r\n",
                 cond_b, ec_ms_b,
                 ec_ms_b, sal_b,
                 sa_b, sb_b, sc_b, sd_b, sfval_b,
                 i1_b, i2_b,
                 s->relay1_func, r1_on, r1_off, r->relay1_state ? "ON" : "OFF",
                 s->relay2_func, r2_on, r2_off, r->relay2_state ? "ON" : "OFF");
        serial_monitor_send_str(calc_buf3);
        serial_monitor_pump_tx();

        /* 3. OUTPUT VALUES (Matches LCD Screen) */
        char out_buf[256];
        snprintf(out_buf, sizeof(out_buf),
                 "[3. OUTPUT DISPLAY VALUES (Matches LCD Screen)]\r\n"
                 "  COND: %s uS/cm | RES: %s M-Ohm | TDS: %s ppm | SAL: %s ppt\r\n"
                 "  TEMP: %s %c | FVAL: %s | R1: %s | R2: %s | I1: %s mA | I2: %s mA\r\n",
                 cond_b, res_b, tds_val_b, sal_b,
                 (s->temp_unit == 1) ? temp_f_b : temp_c_b, unit_char,
                 sfval_b, r->relay1_state ? "ON" : "OFF", r->relay2_state ? "ON" : "OFF",
                 i1_b, i2_b);
        serial_monitor_send_str(out_buf);
    }
    else
    {
        serial_monitor_send_str("  [Waiting for valid sensor acquisition data...]\r\n");
    }
    serial_monitor_send_str("========================================================================\r\n");
}

static void print_config(const settings_t *s)
{
    if (!s) return;

    char k_buf[10], tc_buf[10], tds_buf[10], tcal_buf[10], ecfac_buf[10], ecoff_buf[10];
    char r1_on[10], r1_off[10], r2_on[10], r2_off[10];

    format_float(s->k_constant, 4, k_buf, sizeof(k_buf));
    format_float(s->temp_coef, 2, tc_buf, sizeof(tc_buf));
    format_float(s->tds_coef, 3, tds_buf, sizeof(tds_buf));
    format_float(s->temp_cal_offset, 1, tcal_buf, sizeof(tcal_buf));
    format_float(s->ec_factor, 3, ecfac_buf, sizeof(ecfac_buf));
    format_float(s->ec_offset, 1, ecoff_buf, sizeof(ecoff_buf));
    format_float(s->relay1_on_val, 1, r1_on, sizeof(r1_on));
    format_float(s->relay1_off_val, 1, r1_off, sizeof(r1_off));
    format_float(s->relay2_on_val, 1, r2_on, sizeof(r2_on));
    format_float(s->relay2_off_val, 1, r2_off, sizeof(r2_off));

    const char *p_type = (s->temp_probe_type == 0) ? "PT1000" : (s->temp_probe_type == 1 ? "NTC10K" : "MANUAL");

    char msg[700];
    snprintf(msg, sizeof(msg),
             "\r\n================ ACTIVE CONFIGURATION ================\r\n"
             "  Modbus Slave ID   : %d\r\n"
             "  Baud Rate         : %lu bps\r\n"
             "  Cell Constant K   : %s 1/cm\r\n"
             "  Probe Cal Factor  : %s (Offset: %s uS/cm, Points: %d)\r\n"
             "  Temp Coeff (TC)   : %s %%/C (Ref: 25.0 C)\r\n"
             "  Temp Cal Offset   : %s C\r\n"
             "  Temp Unit / Probe : %s / %s\r\n"
             "  TDS Factor        : %s\r\n"
             "  Operating Mode    : %s\r\n"
             "  Relay 1 (Func %d)  : ON=%s | OFF=%s\r\n"
             "  Relay 2 (Func %d)  : ON=%s | OFF=%s\r\n"
             "  Stream Interval   : %lu ms (Streaming: %s, Mode: %s)\r\n"
             "======================================================\r\n",
             s->slave_addr,
             s->baud_rate,
             k_buf,
             ecfac_buf, ecoff_buf, s->cal_points_count,
             tc_buf,
             tcal_buf,
             (s->temp_unit == 1) ? "Fahrenheit (F)" : "Celsius (C)", p_type,
             tds_buf,
             (s->measure_mode == 1) ? "SIMULATION (1413 uS/cm)" : "REAL PROBE",
             s->relay1_func, r1_on, r1_off,
             s->relay2_func, r2_on, r2_off,
             s_stream_interval_ms,
             s_streaming_enabled ? "ON" : "OFF",
             s_json_mode ? "JSON" : "TEXT");

    serial_monitor_send_str(msg);
}

static void handle_ascii_command(uint8_t cmd, const meter_reading_t *r, const settings_t *s)
{
    switch (cmd)
    {
        case '?':
        case 'h':
        case 'H':
            print_help();
            break;

        case 'd':
        case 'D':
        case 'r':
        case 'R':
            if (s_json_mode)
            {
                serial_monitor_send_json(r, s);
            }
            else
            {
                serial_monitor_send_reading(r, s);
            }
            break;

        case 's':
        case 'S':
            s_streaming_enabled = !s_streaming_enabled;
            if (s_streaming_enabled)
            {
                serial_monitor_send_str("[MONITOR] Auto-stream: ENABLED\r\n");
            }
            else
            {
                serial_monitor_send_str("[MONITOR] Auto-stream: DISABLED\r\n");
            }
            break;

        case 'j':
        case 'J':
            s_json_mode = !s_json_mode;
            if (s_json_mode)
            {
                serial_monitor_send_str("[MONITOR] Stream Mode: JSON\r\n");
            }
            else
            {
                serial_monitor_send_str("[MONITOR] Stream Mode: TEXT\r\n");
            }
            break;

        case 'f':
        case 'F':
            if (s_stream_interval_ms == 200)
            {
                s_stream_interval_ms = 100;
                serial_monitor_send_str("[MONITOR] Stream Rate: ULTRA-FAST (100 ms / 10 Hz)\r\n");
            }
            else if (s_stream_interval_ms == 100)
            {
                s_stream_interval_ms = 500;
                serial_monitor_send_str("[MONITOR] Stream Rate: MEDIUM (500 ms / 2 Hz)\r\n");
            }
            else if (s_stream_interval_ms == 500)
            {
                s_stream_interval_ms = 1000;
                serial_monitor_send_str("[MONITOR] Stream Rate: NORMAL (1000 ms / 1 Hz)\r\n");
            }
            else
            {
                s_stream_interval_ms = 200;
                serial_monitor_send_str("[MONITOR] Stream Rate: FAST (200 ms / 5 Hz)\r\n");
            }
            break;

        case 'm':
        case 'M':
            print_formulas(r, s);
            break;

        case '1':
            if (s_relay1_override < 0)
            {
                s_relay1_override = 1;
                serial_monitor_send_str("[MONITOR] Relay 1: FORCED ON\r\n");
            }
            else if (s_relay1_override == 1)
            {
                s_relay1_override = 0;
                serial_monitor_send_str("[MONITOR] Relay 1: FORCED OFF\r\n");
            }
            else
            {
                s_relay1_override = -1;
                serial_monitor_send_str("[MONITOR] Relay 1: AUTO (restored)\r\n");
            }
            break;

        case '2':
            if (s_relay2_override < 0)
            {
                s_relay2_override = 1;
                serial_monitor_send_str("[MONITOR] Relay 2: FORCED ON\r\n");
            }
            else if (s_relay2_override == 1)
            {
                s_relay2_override = 0;
                serial_monitor_send_str("[MONITOR] Relay 2: FORCED OFF\r\n");
            }
            else
            {
                s_relay2_override = -1;
                serial_monitor_send_str("[MONITOR] Relay 2: AUTO (restored)\r\n");
            }
            break;

        case 'c':
        case 'C':
            print_config(s);
            break;

        case 'v':
        case 'V':
            print_version();
            break;

        case '\r':
        case '\n':
        case ' ':
            /* Echo prompt */
            serial_monitor_send_str("> ");
            break;

        default:
            /* Unknown character: ignore */
            break;
    }
}

/* ========================================================================== */
/*                     SETTINGS RUNTIME CHANGE DETECTOR                       */
/* ========================================================================== */
static void check_settings_changes(const settings_t *s, const meter_reading_t *r)
{
    if (!s) return;

    if (!s_shadow_valid)
    {
        s_shadow_cfg = *s;
        s_shadow_valid = true;
        return;
    }

    bool k_changed          = (s->k_constant != s_shadow_cfg.k_constant);
    bool tc_changed         = (s->temp_coef != s_shadow_cfg.temp_coef);
    bool tds_changed        = (s->tds_coef != s_shadow_cfg.tds_coef);
    bool temp_cal_changed   = (s->temp_cal_offset != s_shadow_cfg.temp_cal_offset);
    bool temp_unit_changed  = (s->temp_unit != s_shadow_cfg.temp_unit);
    bool temp_probe_changed = (s->temp_probe_type != s_shadow_cfg.temp_probe_type);
    bool ec_factor_changed  = (s->ec_factor != s_shadow_cfg.ec_factor);
    bool ec_offset_changed  = (s->ec_offset != s_shadow_cfg.ec_offset);
    bool cal_pts_changed    = (s->cal_points_count != s_shadow_cfg.cal_points_count);
    bool mode_changed       = (s->measure_mode != s_shadow_cfg.measure_mode);
    bool baud_changed       = (s->baud_rate != s_shadow_cfg.baud_rate && s->baud_rate >= 4800);
    bool r1_changed         = (s->relay1_on_val != s_shadow_cfg.relay1_on_val ||
                               s->relay1_off_val != s_shadow_cfg.relay1_off_val ||
                               s->relay1_func != s_shadow_cfg.relay1_func);
    bool r2_changed         = (s->relay2_on_val != s_shadow_cfg.relay2_on_val ||
                               s->relay2_off_val != s_shadow_cfg.relay2_off_val ||
                               s->relay2_func != s_shadow_cfg.relay2_func);

    if (k_changed || tc_changed || tds_changed || temp_cal_changed || temp_unit_changed ||
        temp_probe_changed || ec_factor_changed || ec_offset_changed || cal_pts_changed ||
        mode_changed || baud_changed || r1_changed || r2_changed)
    {
        serial_monitor_send_str("\r\n************************************************************************\r\n");
        serial_monitor_send_str(">>> [SETTING CHANGED] Active Measurement Parameters Updated by User <<<\r\n");
        serial_monitor_send_str("************************************************************************\r\n");

        if (k_changed)
        {
            char old_b[12], new_b[12];
            format_float(s_shadow_cfg.k_constant, 4, old_b, sizeof(old_b));
            format_float(s->k_constant, 4, new_b, sizeof(new_b));
            char line[80];
            snprintf(line, sizeof(line), "  Cell Constant K  : %s -> %s (1/cm)\r\n", old_b, new_b);
            serial_monitor_send_str(line);
        }

        if (ec_factor_changed || ec_offset_changed || cal_pts_changed)
        {
            char old_f[10], new_f[10], old_o[10], new_o[10];
            format_float(s_shadow_cfg.ec_factor, 3, old_f, sizeof(old_f));
            format_float(s->ec_factor, 3, new_f, sizeof(new_f));
            format_float(s_shadow_cfg.ec_offset, 1, old_o, sizeof(old_o));
            format_float(s->ec_offset, 1, new_o, sizeof(new_o));
            char line[120];
            snprintf(line, sizeof(line), "  Probe Cal        : Fac %s -> %s | Off %s -> %s uS (Pts: %d->%d)\r\n",
                     old_f, new_f, old_o, new_o, s_shadow_cfg.cal_points_count, s->cal_points_count);
            serial_monitor_send_str(line);
        }

        if (tc_changed)
        {
            char old_b[10], new_b[10];
            format_float(s_shadow_cfg.temp_coef, 2, old_b, sizeof(old_b));
            format_float(s->temp_coef, 2, new_b, sizeof(new_b));
            char line[80];
            snprintf(line, sizeof(line), "  Temp Coeff (TC)  : %s -> %s %%/C\r\n", old_b, new_b);
            serial_monitor_send_str(line);
        }

        if (temp_cal_changed)
        {
            char old_b[10], new_b[10];
            format_float(s_shadow_cfg.temp_cal_offset, 1, old_b, sizeof(old_b));
            format_float(s->temp_cal_offset, 1, new_b, sizeof(new_b));
            char line[80];
            snprintf(line, sizeof(line), "  Temp Cal Offset  : %s -> %s C\r\n", old_b, new_b);
            serial_monitor_send_str(line);
        }

        if (temp_unit_changed)
        {
            char line[80];
            snprintf(line, sizeof(line), "  Temp Unit        : %s -> %s\r\n",
                     (s_shadow_cfg.temp_unit == 1) ? "Fahrenheit (F)" : "Celsius (C)",
                     (s->temp_unit == 1) ? "Fahrenheit (F)" : "Celsius (C)");
            serial_monitor_send_str(line);
        }

        if (temp_probe_changed)
        {
            const char *p_old = (s_shadow_cfg.temp_probe_type == 0) ? "PT1000" : (s_shadow_cfg.temp_probe_type == 1 ? "NTC10K" : "MANUAL");
            const char *p_new = (s->temp_probe_type == 0) ? "PT1000" : (s->temp_probe_type == 1 ? "NTC10K" : "MANUAL");
            char line[80];
            snprintf(line, sizeof(line), "  Temp Probe Type  : %s -> %s\r\n", p_old, p_new);
            serial_monitor_send_str(line);
        }

        if (tds_changed)
        {
            char old_b[10], new_b[10];
            format_float(s_shadow_cfg.tds_coef, 3, old_b, sizeof(old_b));
            format_float(s->tds_coef, 3, new_b, sizeof(new_b));
            char line[80];
            snprintf(line, sizeof(line), "  TDS Factor       : %s -> %s\r\n", old_b, new_b);
            serial_monitor_send_str(line);
        }

        if (r1_changed)
        {
            char on_b[10], off_b[10];
            format_float(s->relay1_on_val, 1, on_b, sizeof(on_b));
            format_float(s->relay1_off_val, 1, off_b, sizeof(off_b));
            char line[100];
            snprintf(line, sizeof(line), "  Relay 1 Updated  : Func %d (ON: %s, OFF: %s)\r\n",
                     s->relay1_func, on_b, off_b);
            serial_monitor_send_str(line);
        }

        if (r2_changed)
        {
            char on_b[10], off_b[10];
            format_float(s->relay2_on_val, 1, on_b, sizeof(on_b));
            format_float(s->relay2_off_val, 1, off_b, sizeof(off_b));
            char line[100];
            snprintf(line, sizeof(line), "  Relay 2 Updated  : Func %d (ON: %s, OFF: %s)\r\n",
                     s->relay2_func, on_b, off_b);
            serial_monitor_send_str(line);
        }

        if (mode_changed)
        {
            char line[80];
            snprintf(line, sizeof(line), "  Operating Mode   : %s -> %s\r\n",
                     (s_shadow_cfg.measure_mode == 1) ? "SIM 1413uS" : "REAL PROBE",
                     (s->measure_mode == 1) ? "SIM 1413uS" : "REAL PROBE");
            serial_monitor_send_str(line);
        }

        if (baud_changed)
        {
            char line[80];
            snprintf(line, sizeof(line), "  Modbus Baud      : %lu -> %lu bps\r\n",
                     s_shadow_cfg.baud_rate, s->baud_rate);
            serial_monitor_send_str(line);

            USART1->CR1 &= ~USART_CR1_UE;
            USART1->BRR = (SystemCoreClock + (s->baud_rate / 2)) / s->baud_rate;
            s_char_timeout_ms = (38500UL / s->baud_rate) + 2;
            USART1->CR1 |= USART_CR1_UE;
        }

        serial_monitor_send_str("------------------------------------------------------------------------\r\n");

        /* Print calculation formula impact immediately */
        print_formulas(r, s);

        s_shadow_cfg = *s;
    }
}

/* ========================================================================== */
/*                           MAIN PERIODIC TASK                               */
/* ========================================================================== */
void serial_monitor_task(const meter_reading_t *r, const settings_t *s)
{
    uint32_t now = HAL_GetTick();

    /* 1. Check if settings were updated via buttons/menu or Modbus */
    check_settings_changes(s, r);

    /* 2. Check real-time relay state transitions (LCD & Serial in sync) */
    if (r)
    {
        uint8_t cur_r1 = (s_relay1_override >= 0) ? (uint8_t)s_relay1_override : r->relay1_state;
        uint8_t cur_r2 = (s_relay2_override >= 0) ? (uint8_t)s_relay2_override : r->relay2_state;

        if (s_prev_relay1_state >= 0 && cur_r1 != (uint8_t)s_prev_relay1_state)
        {
            char val_buf[12], on_buf[12], off_buf[12];
            format_float(r->cond_us, 1, val_buf, sizeof(val_buf));
            format_float(s ? s->relay1_on_val : 0, 1, on_buf, sizeof(on_buf));
            format_float(s ? s->relay1_off_val : 0, 1, off_buf, sizeof(off_buf));

            char event_msg[160];
            snprintf(event_msg, sizeof(event_msg),
                     "\r\n>>> [RELAY EVENT] Relay 1: %s -> %s | Meas: %s uS/cm | On: %s | Off: %s <<<\r\n",
                     s_prev_relay1_state ? "ON" : "OFF",
                     cur_r1 ? "ON" : "OFF",
                     val_buf, on_buf, off_buf);
            serial_monitor_send_str(event_msg);
            serial_monitor_pump_tx();
        }
        s_prev_relay1_state = cur_r1;

        if (s_prev_relay2_state >= 0 && cur_r2 != (uint8_t)s_prev_relay2_state)
        {
            char val_buf[12], on_buf[12], off_buf[12];
            format_float(r->cond_us, 1, val_buf, sizeof(val_buf));
            format_float(s ? s->relay2_on_val : 0, 1, on_buf, sizeof(on_buf));
            format_float(s ? s->relay2_off_val : 0, 1, off_buf, sizeof(off_buf));

            char event_msg[160];
            snprintf(event_msg, sizeof(event_msg),
                     "\r\n>>> [RELAY EVENT] Relay 2: %s -> %s | Meas: %s uS/cm | On: %s | Off: %s <<<\r\n",
                     s_prev_relay2_state ? "ON" : "OFF",
                     cur_r2 ? "ON" : "OFF",
                     val_buf, on_buf, off_buf);
            serial_monitor_send_str(event_msg);
            serial_monitor_pump_tx();
        }
        s_prev_relay2_state = cur_r2;
    }

    /* 3. Drain interrupt RX ring buffer without corrupting binary Modbus bytes */
    while (s_rx_isr_tail != s_rx_isr_head)
    {
        uint8_t byte = s_rx_isr_ring[s_rx_isr_tail];
        s_rx_isr_tail = (uint16_t)((s_rx_isr_tail + 1U) % RX_ISR_SIZE);
        s_last_rx_time = s_rx_isr_last_tick;

        if (s_rx_idx < RX_BUF_SIZE)
        {
            s_rx_buf[s_rx_idx++] = byte;
        }
    }

    /* 4. Check for Modbus RTU 3.5-char silence timeout */
    if (s_rx_idx > 0 && (now - s_last_rx_time) >= s_char_timeout_ms)
    {
        if (s_rx_idx == 1)
        {
            /* Single-character terminal command (safe: cannot collide with multi-byte Modbus RTU frames) */
            uint8_t byte = s_rx_buf[0];
            if (byte == '?' || byte == 'h' || byte == 'H' ||
                byte == 'd' || byte == 'D' || byte == 'r' || byte == 'R' ||
                byte == 's' || byte == 'S' || byte == 'j' || byte == 'J' ||
                byte == 'f' || byte == 'F' || byte == '1' || byte == '2' ||
                byte == 'c' || byte == 'C' || byte == 'v' || byte == 'V' ||
                byte == 'm' || byte == 'M')
            {
                handle_ascii_command(byte, r, s);
            }
        }
        else
        {
            /* Check if packet is a response from the Turbidity Sensor */
            if (!turbidity_parse_packet(s_rx_buf, s_rx_idx))
            {
                if (s_rx_idx >= 8) /* Valid Modbus request has at least 8 bytes */
                {
                    uint16_t tx_len = modbus_handle(s_rx_buf, s_rx_idx, s_tx_buf, r, s);
                    if (tx_len > 0)
                    {
                        serial_monitor_send_bytes(s_tx_buf, tx_len);
                    }
                }
            }
        }
        s_rx_idx = 0; /* Reset frame */
    }

    /* 4. Automatic periodic telemetry streaming */
    if (s_streaming_enabled && (now - s_last_stream_time) >= s_stream_interval_ms)
    {
        s_last_stream_time = now;

        if (s_json_mode)
        {
            serial_monitor_send_json(r, s);
        }
        else
        {
            serial_monitor_send_reading(r, s);
        }
    }

    /* 5. Non-blocking RS-485 byte pump */
    serial_monitor_pump_tx();
}

/* ========================================================================== */
/*                         GETTERS & SETTERS                                  */
/* ========================================================================== */
void serial_monitor_set_streaming(bool enable)
{
    s_streaming_enabled = enable;
}

bool serial_monitor_is_streaming(void)
{
    return s_streaming_enabled;
}

void serial_monitor_set_json_mode(bool json_mode)
{
    s_json_mode = json_mode;
}

bool serial_monitor_is_json_mode(void)
{
    return s_json_mode;
}

void serial_monitor_set_interval(uint32_t interval_ms)
{
    if (interval_ms >= 100 && interval_ms <= 60000)
    {
        s_stream_interval_ms = interval_ms;
    }
}

int8_t serial_monitor_relay_override(uint8_t relay_index)
{
    if (relay_index == 0) return s_relay1_override;
    if (relay_index == 1) return s_relay2_override;
    return -1;
}
