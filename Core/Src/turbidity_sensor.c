/**
 * @file    turbidity_sensor.c
 * @brief   RS-485 Modbus RTU Master driver for Digital Turbidity Sensor (0-400
 * NTU). Configured for Slave ID: 3, Baud: 9600, Start Register: 6, Length:
 * 10/15 (Register 14). 100% Real RS-485 Data only - Zero Simulation.
 */

#include "turbidity_sensor.h"
#include "meter_core.h"
#include "serial_port_mointor.h"
#include "stm32g0xx_hal.h"
#include <math.h>
#include <string.h>

/* Primary target parameters from user */
static uint8_t s_sensor_addr = 0x03; /* Modbus Slave ID 3 */
static uint16_t s_reg_addr =
    0x0006; /* Start Register 6 (reads block through 15) */
static uint8_t s_func_code = 0x03; /* Modbus Function Code: 0x03 (Holding) */
static uint8_t s_reg_count = 10;   /* Read 10 registers (from 6 to 15) */

/* Active transmission state during hunting/polling */
static uint8_t s_poll_addr = 0x03;
static uint8_t s_poll_fc = 0x03;
static uint16_t s_poll_reg = 0x0006;
static uint8_t s_poll_count = 10;
static bool s_is_locked =
    false; /* Set true once sensor responds successfully */
static uint8_t s_hunt_step = 0;

static float s_current_ntu = 0.0f;
static bool s_is_fault =
    true; /* Starts in fault/offline until real packet arrives */
static uint32_t s_last_poll_time = 0;
static uint32_t s_last_resp_time = 0;
static bool s_first_reading = true;

void turbidity_init(void) {
  s_sensor_addr = 0x03;
  s_reg_addr = 0x0006; /* Start Address 6 */
  s_func_code = 0x03;
  s_reg_count = 10; /* Reads registers 6 to 15 */
  s_poll_addr = 0x03;
  s_poll_fc = 0x03;
  s_poll_reg = 0x0006;
  s_poll_count = 10;
  s_is_locked = false;
  s_hunt_step = 0;

  s_current_ntu = 0.0f; /* Pure zero when unlinked - no simulation */
  s_is_fault = true;
  s_last_poll_time = 0;
  s_last_resp_time = 0;
  s_first_reading = true;
}

void turbidity_set_slave_addr(uint8_t addr) {
  if (addr >= 1 && addr <= 247) {
    s_sensor_addr = addr;
    s_poll_addr = addr;
    s_is_locked = false;
    s_hunt_step = 0;
  }
}

uint8_t turbidity_get_slave_addr(void) { return s_sensor_addr; }

void turbidity_set_reg_addr(uint16_t reg) {
  s_reg_addr = reg;
  s_poll_reg = reg;
  s_is_locked = false;
  s_hunt_step = 0;
}

uint16_t turbidity_get_reg_addr(void) { return s_reg_addr; }

float turbidity_get_ntu(void) { return s_current_ntu; }

bool turbidity_is_fault(void) { return s_is_fault; }

/* Modbus CRC16 Calculation */
static uint16_t calc_crc16(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (uint8_t i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

/* Periodic polling of Turbidity sensor */
void turbidity_task(uint32_t now_ms) {
  /* Timeout check: if no response for > 2500ms, mark sensor offline */
  if (!s_is_fault && (now_ms - s_last_resp_time) > 2500) {
    s_is_fault = true;
    s_is_locked = false;
    s_current_ntu = 0.0f;
  }

  /* Poll every 750 ms */
  if ((now_ms - s_last_poll_time) >= 750) {
    s_last_poll_time = now_ms;

    if (s_is_locked) {
      /* Use locked successful parameters */
      s_poll_addr = s_sensor_addr;
      s_poll_fc = s_func_code;
      s_poll_reg = s_reg_addr;
      s_poll_count = s_reg_count;
    } else {
      /* Hunting mode: Try the user's specific definitions */
      switch (s_hunt_step % 6) {
      case 0:
        /* Candidate 0: Start Addr 6, Quantity 10 (reads 6 through 15) FC 03 */
        s_poll_addr = 0x03;
        s_poll_fc = 0x03;
        s_poll_reg = 0x0006;
        s_poll_count = 10;
        break;
      case 1:
        /* Candidate 1: Start Addr 6, Quantity 15 (reads 6 through 20) FC 03 */
        s_poll_addr = 0x03;
        s_poll_fc = 0x03;
        s_poll_reg = 0x0006;
        s_poll_count = 15;
        break;
      case 2:
        /* Candidate 2: Start Addr 14, Quantity 2 (reads 14 and 15 directly) FC
         * 03 */
        s_poll_addr = 0x03;
        s_poll_fc = 0x03;
        s_poll_reg = 0x000E;
        s_poll_count = 2;
        break;
      case 3:
        /* Candidate 3: Start Addr 6, Quantity 10 with FC 04 (Input Registers)
         */
        s_poll_addr = 0x03;
        s_poll_fc = 0x04;
        s_poll_reg = 0x0006;
        s_poll_count = 10;
        break;
      case 4:
        /* Candidate 4: Start Addr 6, Quantity 15 with FC 04 */
        s_poll_addr = 0x03;
        s_poll_fc = 0x04;
        s_poll_reg = 0x0006;
        s_poll_count = 15;
        break;
      case 5:
        /* Candidate 5: Start Addr 14, Quantity 2 with FC 04 */
        s_poll_addr = 0x03;
        s_poll_fc = 0x04;
        s_poll_reg = 0x000E;
        s_poll_count = 2;
        break;
      }
      s_hunt_step++;
    }

    /* Build Modbus RTU Query Frame */
    uint8_t query[8];
    query[0] = s_poll_addr;
    query[1] = s_poll_fc;
    query[2] = (uint8_t)((s_poll_reg >> 8) & 0xFF);
    query[3] = (uint8_t)(s_poll_reg & 0xFF);
    query[4] = (uint8_t)((s_poll_count >> 8) & 0xFF);
    query[5] = (uint8_t)(s_poll_count & 0xFF);

    uint16_t crc = calc_crc16(query, 6);
    query[6] = (uint8_t)(crc & 0xFF);
    query[7] = (uint8_t)(crc >> 8);

    /* Transmit query over RS-485 */
    serial_monitor_send_bytes(query, 8);
  }
}

/* Parse incoming packet from RS-485 */
bool turbidity_parse_packet(const uint8_t *buf, uint16_t len) {
  if (!buf || len < 5)
    return false;

  /* Verify Slave Address matches our target */
  if (buf[0] != s_poll_addr && buf[0] != s_sensor_addr) {
    return false;
  }

  /* Check function code (0x03 or 0x04) */
  uint8_t fc = buf[1];
  if (fc != 0x03 && fc != 0x04) {
    return false;
  }

  /* Check byte count */
  uint8_t byte_count = buf[2];
  if (byte_count < 2 || len < (uint16_t)(byte_count + 5)) {
    return false;
  }

  /* Verify CRC16 */
  uint16_t rx_crc =
      (uint16_t)buf[byte_count + 3] | ((uint16_t)buf[byte_count + 4] << 8);
  if (calc_crc16(buf, byte_count + 3) != rx_crc) {
    return false;
  }

  /* Valid response from Turbidity sensor! Lock onto these settings */
  s_sensor_addr = buf[0];
  s_func_code = fc;
  s_reg_addr = s_poll_reg;
  s_reg_count = s_poll_count;
  s_is_locked = true;

  /* Extract data for Register 14 (and 15) based on start register */
  float val = 0.0f;
  bool parsed = false;

  /* Offset calculation: Register 13 (High Word) and Register 14 (Low Word) */
  if (s_poll_reg <= 13) {
    uint16_t reg13_ptr = (uint16_t)(3 + (13 - s_poll_reg) * 2);
    if ((reg13_ptr + 3) < (uint16_t)(3 + byte_count)) {
      uint32_t raw32 = ((uint32_t)buf[reg13_ptr] << 24) |
                       ((uint32_t)buf[reg13_ptr + 1] << 16) |
                       ((uint32_t)buf[reg13_ptr + 2] << 8) |
                       (uint32_t)buf[reg13_ptr + 3];

      /* Check if sensor uses milli-NTU 32-bit unsigned integer across
       * Reg 13..14: e.g., clean water: Reg13=0x0000, Reg14=0x009C -> 156 ->
       * 0.156 NTU dust water:  Reg13=0x0001, Reg14=0xD7AD -> 120749 -> 120.749
       * NTU */
      if (raw32 <= 2000000UL) {
        val = (float)raw32 / 1000.0f;
        parsed = true;
      }
    }
  }

  /* Fallback: where is Register 14 alone (or 14..15 float) in this payload? */
  uint16_t target_reg = 14;
  uint16_t reg_offset = 0;
  if (target_reg >= s_poll_reg) {
    reg_offset = target_reg - s_poll_reg;
  }
  uint16_t byte_ptr = 3 + (reg_offset * 2);

  /* Check if Register 14 is within the returned bytes */
    if (!parsed && (byte_ptr + 1) < (uint16_t)(3 + byte_count)) {
      /* Check if 4 bytes are available (Registers 14 and 15) for Float
       * ABCD/CDAB */
      if ((byte_ptr + 3) < (uint16_t)(3 + byte_count)) {
        union {
          float f;
          uint8_t b[4];
        } u_abcd, u_cdab;

        /* IEEE 754 Big-Endian (ABCD) */
        u_abcd.b[3] = buf[byte_ptr];
        u_abcd.b[2] = buf[byte_ptr + 1];
        u_abcd.b[1] = buf[byte_ptr + 2];
        u_abcd.b[0] = buf[byte_ptr + 3];

        /* IEEE 754 Word-Swapped (CDAB) */
        u_cdab.b[3] = buf[byte_ptr + 2];
        u_cdab.b[2] = buf[byte_ptr + 3];
        u_cdab.b[1] = buf[byte_ptr];
        u_cdab.b[0] = buf[byte_ptr + 1];

        uint32_t raw_abcd = ((uint32_t)buf[byte_ptr] << 24) |
                            ((uint32_t)buf[byte_ptr + 1] << 16) |
                            ((uint32_t)buf[byte_ptr + 2] << 8) |
                            (uint32_t)buf[byte_ptr + 3];
        uint32_t raw_cdab = ((uint32_t)buf[byte_ptr + 2] << 24) |
                            ((uint32_t)buf[byte_ptr + 3] << 16) |
                            ((uint32_t)buf[byte_ptr] << 8) |
                            (uint32_t)buf[byte_ptr + 1];

        /* Reject tiny subnormal floats (< 0.001) so 16-bit integer registers
         * fall through cleanly */
        if (!isnan(u_abcd.f) && !isinf(u_abcd.f) &&
            (raw_abcd == 0x00000000UL ||
             (u_abcd.f >= 0.001f && u_abcd.f <= 2500.0f))) {
          val = u_abcd.f;
          parsed = true;
        } else if (!isnan(u_cdab.f) && !isinf(u_cdab.f) &&
                   (raw_cdab == 0x00000000UL ||
                    (u_cdab.f >= 0.001f && u_cdab.f <= 2500.0f))) {
          val = u_cdab.f;
          parsed = true;
        }
      }

      /* If not an IEEE float, decode as 16-bit integer at Register 14 */
      if (!parsed) {
        uint16_t raw_14 = ((uint16_t)buf[byte_ptr] << 8) | buf[byte_ptr + 1];
        if (raw_14 > 2000 && raw_14 <= 20000) {
          /* Scaled by 10 (e.g. 100 NTU = 1000) */
          val = (float)raw_14 / 10.0f;
          parsed = true;
        } else {
          val = (float)raw_14;
          parsed = true;
        }
      }
    } else if (s_poll_reg == 15 && (3 + 1) < (uint16_t)(3 + byte_count)) {
      /* Case where start address was 15 directly */
      uint16_t raw_15 = ((uint16_t)buf[3] << 8) | buf[4];
      if (raw_15 > 2000 && raw_15 <= 20000) {
        val = (float)raw_15 / 10.0f;
        parsed = true;
      } else {
        val = (float)raw_15;
        parsed = true;
      }
    }

  if (parsed) {
    if (val < 0.0f)
      val = 0.0f;
    if (val > 2000.0f)
      val = 2000.0f;

    if (s_first_reading || s_is_fault) {
      s_current_ntu = val;
      s_first_reading = false;
    } else {
      /* Smooth exponential filter to prevent jitter */
      s_current_ntu = (s_current_ntu * 0.7f) + (val * 0.3f);
    }

    s_is_fault = false;
    s_last_resp_time = HAL_GetTick();
    return true;
  }

  return false;
}
