/**
 * @file    lcd_st7565.c
 * @brief   Driver implementation for NHD-C12864LZ-FSW-FBW-3V3 (ST7565R
 * Controller) Target: STM32G030K8T6 with Hardware SPI1 or GPIO Bit-Bang
 * fallback.
 */

#include "lcd_st7565.h"
#include "font10x16.h"
#include "font16x24.h"
#include "font5x7.h"
#include "font8x16.h"
#include <string.h>

#if LCD_USE_HARDWARE_SPI
/* External handle to SPI1 peripheral initialized in main.c */
extern SPI_HandleTypeDef hspi1;
#endif

/* ========================================================================== */
/*                          LOW-LEVEL SPI TRANSMIT                            */
/* ========================================================================== */

/**
 * @brief  Transmit 1 byte over SPI to the display controller
 * @param  byte: 8-bit data to transmit (MSB first)
 */
static inline void lcd_spi_tx(uint8_t byte) {
#if LCD_USE_HARDWARE_SPI
  /* Hardware SPI1 transmit with 10 ms timeout */
  HAL_SPI_Transmit(&hspi1, &byte, 1, 10);
#else
  /* GPIO Bit-Bang SPI (Mode 0: SCLK idle LOW, sample on rising edge) */
  for (uint8_t i = 0; i < 8; i++) {
    /* Set MOSI bit */
    if (byte & 0x80) {
      HAL_GPIO_WritePin(LCD_MOSI_PORT, LCD_MOSI_PIN, GPIO_PIN_SET);
    } else {
      HAL_GPIO_WritePin(LCD_MOSI_PORT, LCD_MOSI_PIN, GPIO_PIN_RESET);
    }

    /* Clock Rising Edge: ST7565R latches data */
    HAL_GPIO_WritePin(LCD_SCK_PORT, LCD_SCK_PIN, GPIO_PIN_SET);

    /* Shift next bit */
    byte <<= 1;

    /* Clock Falling Edge */
    HAL_GPIO_WritePin(LCD_SCK_PORT, LCD_SCK_PIN, GPIO_PIN_RESET);
  }
#endif
}

/* ========================================================================== */
/*                      COMMAND / DATA BUS WRITING                            */
/* ========================================================================== */

/**
 * @brief  Send a command byte to the ST7565R (A0 = LOW)
 */
void lcd_write_cmd(uint8_t cmd) {
  /* Select Command Mode: A0 = 0 */
  HAL_GPIO_WritePin(LCD_A0_PORT, LCD_A0_PIN, GPIO_PIN_RESET);

  /* Assert Chip Select: /CS1 = 0 */
  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET);

  /* Transmit command */
  lcd_spi_tx(cmd);

  /* De-assert Chip Select: /CS1 = 1 */
  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief  Send a data byte to the ST7565R display RAM (A0 = HIGH)
 */
void lcd_write_data(uint8_t data) {
  /* Select Data Mode: A0 = 1 */
  HAL_GPIO_WritePin(LCD_A0_PORT, LCD_A0_PIN, GPIO_PIN_SET);

  /* Assert Chip Select: /CS1 = 0 */
  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET);

  /* Transmit data byte */
  lcd_spi_tx(data);

  /* De-assert Chip Select: /CS1 = 1 */
  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
}

/* ========================================================================== */
/*                         DISPLAY INITIALIZATION                             */
/* ========================================================================== */

void lcd_init(void) {
  /* 1. Turn ON the LED Backlight immediately via DISP_LED_EN (PC6 active HIGH)
   */
  lcd_set_backlight(1);

  /*
   * 2. Configure parallel control pins for serial mode stability:
   *    In 4-wire serial SPI mode, /RD (PA12) and /WR (PA11) must be held HIGH.
   */
  HAL_GPIO_WritePin(LCD_RW_PORT, LCD_RW_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_RD_PORT, LCD_RD_PIN, GPIO_PIN_SET);

  /* Deselect display */
  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);

  /*
   * 3. Hardware Reset Sequence:
   *    Pull /RES LOW for 20 ms, then release HIGH for 20 ms
   */
  HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, GPIO_PIN_SET);
  HAL_Delay(20);

  /*
   * 4. ST7565R Initialization Sequence:
   *    Every command is annotated with its hardware purpose.
   */

  /* Command 0xA2: Set LCD Bias to 1/9 duty (0xA2 = 1/9, 0xA3 = 1/7) */
  lcd_write_cmd(LCD_BIAS_MODE);

  /* Command 0xA0: ADC Select - Normal segment driver direction (SEG0 -> SEG127)
   */
  lcd_write_cmd(LCD_ORIENTATION_ADC);

  /* Command 0xC8: Common Output Scan Direction - Reverse (COM63 -> COM0) for
   * standard viewing */
  lcd_write_cmd(LCD_ORIENTATION_COM);

  /* Command 0x40: Set Display Start Line address to RAM row 0 */
  lcd_write_cmd(ST7565_CMD_SET_START_LINE | 0x00);

  /* Command 0xA4: Set all points normal (Display follows RAM, disables test
   * all-on mode) */
  lcd_write_cmd(ST7565_CMD_ALL_ON_NORMAL);

  /* Command 0xA6: Normal display polarity (1=dark pixel, 0=clear background) */
  lcd_write_cmd(LCD_INVERSE_MODE);

  /* Command 0x20..0x27: Internal Regulator Resistor Ratio (coarse contrast
   * setting) */
  lcd_write_cmd(0x20 | (LCD_REGULATOR_RATIO & 0x07));

  /* Command 0x81: Enter Electronic Volume mode to set fine contrast */
  lcd_write_cmd(ST7565_CMD_ELEC_VOL_MODE);

  /* Second byte: Electronic volume register value (0x00 to 0x3F) */
  lcd_write_cmd(LCD_CONTRAST_VALUE & 0x3F);

  /*
   * Power Controller Turn-on Sequence:
   * Turn on the internal charge pump and regulators in steps with short delays
   * to prevent inrush current and ensure internal V0..V4 LCD driving voltages
   * stabilize.
   */
  /* Step A: Turn ON internal Booster circuit (bit 2 = 1 -> 0x2C) */
  lcd_write_cmd(ST7565_CMD_POWER_CTRL | 0x04);
  HAL_Delay(5);

  /* Step B: Turn ON internal Voltage Regulator (bit 1 = 1 -> 0x2E) */
  lcd_write_cmd(ST7565_CMD_POWER_CTRL | 0x06);
  HAL_Delay(5);

  /* Step C: Turn ON internal Voltage Follower (bit 0 = 1 -> 0x2F: all circuits
   * ON) */
  lcd_write_cmd(ST7565_CMD_POWER_CTRL | 0x07);
  HAL_Delay(10);

  /* Command 0xAF: Turn Display ON */
  lcd_write_cmd(ST7565_CMD_DISPLAY_ON);

  /* 5. Clear display RAM so no random power-on noise remains */
  lcd_clear();
}

/* ========================================================================== */
/*                          SCREEN CLEAR & CURSOR                             */
/* ========================================================================== */

void lcd_clear(void) {
  /* Iterate through all 8 pages (each page = 8 vertical rows) */
  for (uint8_t page = 0; page < LCD_PAGES; page++) {
    lcd_set_cursor(page, 0);

    /* Write 128 blank columns (all vertical pixels cleared to 0) */
    for (uint8_t col = 0; col < LCD_WIDTH; col++) {
      lcd_write_data(0x00);
    }
  }
}

void lcd_set_cursor(uint8_t page, uint8_t col) {
  if (page >= LCD_PAGES) {
    page = LCD_PAGES - 1;
  }
  if (col >= LCD_WIDTH) {
    col = LCD_WIDTH - 1;
  }

  /* Set Page Address: 0xB0 to 0xB7 */
  lcd_write_cmd(ST7565_CMD_SET_PAGE | (page & 0x07));

  /* Account for hardware column offset if present */
  uint8_t actual_col = col + LCD_COL_OFFSET;

  /* Set Column Address MSB (upper 4 bits): 0x10 | (col >> 4) */
  lcd_write_cmd(ST7565_CMD_SET_COL_HIGH | ((actual_col >> 4) & 0x0F));

  /* Set Column Address LSB (lower 4 bits): 0x00 | (col & 0x0F) */
  lcd_write_cmd(ST7565_CMD_SET_COL_LOW | (actual_col & 0x0F));
}

/* ========================================================================== */
/*                             TEXT RENDERING                                 */
/* ========================================================================== */

void lcd_print(uint8_t page, uint8_t col, const char *str) {
  if (!str)
    return;

  lcd_set_cursor(page, col);

  while (*str != '\0' && col < LCD_WIDTH) {
    char c = *str++;

    /* Check printable ASCII bounds */
    if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR) {
      c = ' ';
    }

    uint8_t char_idx = (uint8_t)(c - FONT5X7_FIRST_CHAR);

    /* Draw 5 columns of the character glyph */
    for (uint8_t i = 0; i < FONT5X7_WIDTH; i++) {
      if (col < LCD_WIDTH) {
        lcd_write_data(font5x7[char_idx][i]);
        col++;
      }
    }

    /* Draw 1 blank column for inter-character spacing */
    if (col < LCD_WIDTH) {
      lcd_write_data(0x00);
      col++;
    }
  }
}

void lcd_print_centered(uint8_t page, const char *str) {
  if (!str)
    return;

  size_t len = strlen(str);
  if (len == 0)
    return;

  /* Each character occupies FONT5X7_PITCH (6) columns */
  uint16_t text_pixel_width = (uint16_t)(len * FONT5X7_PITCH);

  uint8_t start_col = 0;
  if (text_pixel_width < LCD_WIDTH) {
    start_col = (uint8_t)((LCD_WIDTH - text_pixel_width) / 2);
  }

  lcd_print(page, start_col, str);
}

static const uint8_t s_font3x5[59][3] = {/* 0x20 ' ' to 0x2F '/' */
                                         {0x00, 0x00, 0x00},
                                         {0x00, 0x17, 0x00},
                                         {0x03, 0x00, 0x03},
                                         {0x14, 0x1F, 0x14},
                                         {0x12, 0x1F, 0x09},
                                         {0x19, 0x04, 0x13},
                                         {0x0A, 0x15, 0x12},
                                         {0x00, 0x03, 0x00},
                                         {0x00, 0x0E, 0x11},
                                         {0x11, 0x0E, 0x00},
                                         {0x0A, 0x04, 0x0A},
                                         {0x04, 0x0E, 0x04},
                                         {0x00, 0x10, 0x08},
                                         {0x04, 0x04, 0x04},
                                         {0x00, 0x10, 0x00},
                                         {0x10, 0x08, 0x04},
                                         /* 0x30 '0' to 0x39 '9' */
                                         {0x0E, 0x11, 0x0E},
                                         {0x12, 0x1F, 0x10},
                                         {0x19, 0x15, 0x12},
                                         {0x11, 0x15, 0x0A},
                                         {0x07, 0x04, 0x1F},
                                         {0x17, 0x15, 0x09},
                                         {0x0E, 0x15, 0x08},
                                         {0x01, 0x1D, 0x03},
                                         {0x0A, 0x15, 0x0A},
                                         {0x02, 0x15, 0x0E},
                                         /* 0x3A ':' to 0x40 '@' */
                                         {0x00, 0x0A, 0x00},
                                         {0x00, 0x12, 0x00},
                                         {0x04, 0x0A, 0x11},
                                         {0x0A, 0x0A, 0x0A},
                                         {0x11, 0x0A, 0x04},
                                         {0x01, 0x15, 0x02},
                                         {0x0E, 0x15, 0x0D},
                                         /* 0x41 'A' to 0x5A 'Z' */
                                         {0x1E, 0x05, 0x1E},
                                         {0x1F, 0x15, 0x0A},
                                         {0x0E, 0x11, 0x11},
                                         {0x1F, 0x11, 0x0E},
                                         {0x1F, 0x15, 0x11},
                                         {0x1F, 0x05, 0x01},
                                         {0x0E, 0x11, 0x1D},
                                         {0x1F, 0x04, 0x1F},
                                         {0x11, 0x1F, 0x11},
                                         {0x08, 0x10, 0x0F},
                                         {0x1F, 0x04, 0x1B},
                                         {0x1F, 0x10, 0x10},
                                         {0x1F, 0x0E, 0x1F},
                                         {0x1F, 0x06, 0x1F},
                                         {0x0E, 0x11, 0x0E},
                                         {0x1F, 0x05, 0x02},
                                         {0x0E, 0x19, 0x1E},
                                         {0x1F, 0x05, 0x1A},
                                         {0x12, 0x15, 0x09},
                                         {0x01, 0x1F, 0x01},
                                         {0x0F, 0x10, 0x0F},
                                         {0x07, 0x18, 0x07},
                                         {0x0F, 0x18, 0x0F},
                                         {0x1B, 0x04, 0x1B},
                                         {0x03, 0x1C, 0x03},
                                         {0x19, 0x15, 0x13}};

void lcd_print_compact(uint8_t page, uint8_t col, const char *str) {
  if (!str)
    return;

  lcd_set_cursor(page, col);

  while (*str != '\0' && col < LCD_WIDTH) {
    char c = *str++;
    if (c >= 'a' && c <= 'z')
      c -= ('a' - 'A'); /* Convert lowercase to uppercase */
    uint8_t idx = (c >= 0x20 && c <= 0x5A) ? (uint8_t)(c - 0x20) : 0;

    for (uint8_t i = 0; i < 3; i++) {
      if (col < LCD_WIDTH) {
        lcd_write_data(s_font3x5[idx][i]);
        col++;
      }
    }

    if (col < LCD_WIDTH) {
      lcd_write_data(0x00);
      col++;
    }
  }
}

void lcd_print_compact_centered(uint8_t page, const char *str) {
  if (!str)
    return;

  size_t len = strlen(str);
  if (len == 0)
    return;

  uint16_t text_pixel_width = (uint16_t)(len * 4);
  uint8_t start_col = 0;
  if (text_pixel_width < LCD_WIDTH) {
    start_col = (uint8_t)((LCD_WIDTH - text_pixel_width) / 2);
  }

  /* Clear any unused columns before the text */
  if (start_col > 0) {
    lcd_set_cursor(page, 0);
    for (uint8_t i = 0; i < start_col; i++) {
      lcd_write_data(0x00);
    }
  }

  lcd_print_compact(page, start_col, str);

  /* Clear any unused columns after the text */
  uint8_t end_col = start_col + (uint8_t)text_pixel_width;
  if (end_col < LCD_WIDTH) {
    lcd_set_cursor(page, end_col);
    for (uint8_t i = end_col; i < LCD_WIDTH; i++) {
      lcd_write_data(0x00);
    }
  }
}

/* ========================================================================== */
/*                          HARDWARE CONTROLS                                 */
/* ========================================================================== */

void lcd_set_backlight(uint8_t on) {
  /* AD8591 shutdown pin is active HIGH */
  HAL_GPIO_WritePin(LCD_LED_EN_PORT, LCD_LED_EN_PIN,
                    on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void lcd_set_contrast(uint8_t contrast) /*background 100*/
{
  lcd_write_cmd(ST7565_CMD_ELEC_VOL_MODE);
  lcd_write_cmd(contrast & 0x3F);
  lcd_write_cmd(LCD_INVERSE_MODE);
  lcd_write_cmd(LCD_ORIENTATION_ADC);
}

void lcd_clear_page(uint8_t page) {
  if (page >= LCD_PAGES)
    return;
  lcd_set_cursor(page, 0);
  for (uint8_t col = 0; col < LCD_WIDTH; col++) {
    lcd_write_data(0x00);
  }
}

void lcd_draw_hline(uint8_t page, uint8_t pattern) {
  if (page >= LCD_PAGES)
    return;
  lcd_set_cursor(page, 0);
  for (uint8_t col = 0; col < LCD_WIDTH; col++) {
    lcd_write_data(pattern);
  }
}

void lcd_print_inverse(uint8_t page, uint8_t col, const char *str,
                       uint8_t invert) {
  if (!str)
    return;
  lcd_set_cursor(page, col);
  while (*str != '\0' && col < LCD_WIDTH) {
    char c = *str++;
    if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR) {
      c = ' ';
    }
    uint8_t char_idx = (uint8_t)(c - FONT5X7_FIRST_CHAR);

    for (uint8_t i = 0; i < FONT5X7_WIDTH; i++) {
      if (col < LCD_WIDTH) {
        uint8_t b = font5x7[char_idx][i];
        lcd_write_data(invert ? (uint8_t)(~b) : b);
        col++;
      }
    }

    if (col < LCD_WIDTH) {
      lcd_write_data(invert ? 0xFF : 0x00);
      col++;
    }
  }
}

void lcd_print_line(uint8_t page, const char *str, uint8_t selected) {
  if (page >= LCD_PAGES)
    return;
  lcd_set_cursor(page, 0);

  uint8_t col = 0;
  while (str && *str != '\0' && col < LCD_WIDTH) {
    char c = *str++;
    if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR) {
      c = ' ';
    }
    uint8_t char_idx = (uint8_t)(c - FONT5X7_FIRST_CHAR);

    for (uint8_t i = 0; i < FONT5X7_WIDTH; i++) {
      if (col < LCD_WIDTH) {
        uint8_t b = font5x7[char_idx][i];
        lcd_write_data(selected ? (uint8_t)(~b) : b);
        col++;
      }
    }

    if (col < LCD_WIDTH) {
      lcd_write_data(selected ? 0xFF : 0x00);
      col++;
    }
  }

  /* Pad remaining row columns for a clean full-width line */
  while (col < LCD_WIDTH) {
    lcd_write_data(selected ? 0xFF : 0x00);
    col++;
  }
}

static void expand_bits_2x(uint8_t b, uint8_t *low_byte, uint8_t *high_byte) {
  uint8_t l = 0, h = 0;
  if (b & (1 << 0))
    l |= 0x03;
  if (b & (1 << 1))
    l |= 0x0C;
  if (b & (1 << 2))
    l |= 0x30;
  if (b & (1 << 3))
    l |= 0xC0;
  if (b & (1 << 4))
    h |= 0x03;
  if (b & (1 << 5))
    h |= 0x0C;
  if (b & (1 << 6))
    h |= 0x30;
  if (b & (1 << 7))
    h |= 0xC0;
  *low_byte = l;
  *high_byte = h;
}

void lcd_print_large(uint8_t page, uint8_t col, const char *str) {
  if (!str || page >= LCD_PAGES - 1)
    return;

  while (*str != '\0' && col < LCD_WIDTH) {
    char c = *str++;
    if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR)
      c = ' ';
    uint8_t char_idx = (uint8_t)(c - FONT5X7_FIRST_CHAR);

    for (uint8_t i = 0; i < FONT5X7_WIDTH; i++) {
      uint8_t glyph = font5x7[char_idx][i];
      uint8_t low_b, high_b;
      expand_bits_2x(glyph, &low_b, &high_b);

      for (uint8_t rep = 0; rep < 2; rep++) {
        if (col < LCD_WIDTH) {
          lcd_set_cursor(page, col);
          lcd_write_data(low_b);
          lcd_set_cursor(page + 1, col);
          lcd_write_data(high_b);
          col++;
        }
      }
    }

    /* 2-pixel blank spacing between characters */
    for (uint8_t sp = 0; sp < 2; sp++) {
      if (col < LCD_WIDTH) {
        lcd_set_cursor(page, col);
        lcd_write_data(0x00);
        lcd_set_cursor(page + 1, col);
        lcd_write_data(0x00);
        col++;
      }
    }
  }
}

void lcd_print_large_centered(uint8_t page, const char *str) {
  if (!str)
    return;
  size_t len = strlen(str);
  if (len == 0)
    return;
  uint16_t width = (uint16_t)(len * 12);
  uint8_t start_col = 0;
  if (width < LCD_WIDTH) {
    start_col = (uint8_t)((LCD_WIDTH - width) / 2);
  }
  lcd_print_large(page, start_col, str);
}

void lcd_print_10x16(uint8_t page, uint8_t col, const char *str) {
  if (!str || page >= LCD_PAGES - 1)
    return;

  while (*str != '\0' && col < LCD_WIDTH) {
    char c = *str++;
    const font10x16_glyph_t *glyph = font10x16_get_glyph(c);

    /* Draw 10 columns of the character */
    for (uint8_t i = 0; i < FONT10X16_WIDTH; i++) {
      if (col < LCD_WIDTH) {
        lcd_set_cursor(page, col);
        lcd_write_data(glyph->top[i]);
        lcd_set_cursor(page + 1, col);
        lcd_write_data(glyph->bot[i]);
        col++;
      }
    }

    /* 2-pixel blank spacing between characters */
    for (uint8_t sp = 0; sp < 2; sp++) {
      if (col < LCD_WIDTH) {
        lcd_set_cursor(page, col);
        lcd_write_data(0x00);
        lcd_set_cursor(page + 1, col);
        lcd_write_data(0x00);
        col++;
      }
    }
  }
}

void lcd_print_10x16_centered(uint8_t page, const char *str) {
  if (!str)
    return;
  size_t len = strlen(str);
  if (len == 0)
    return;
  uint16_t width = (uint16_t)(len * FONT10X16_PITCH);
  uint8_t start_col = 0;
  if (width < LCD_WIDTH) {
    start_col = (uint8_t)((LCD_WIDTH - width) / 2);
  }
  lcd_print_10x16(page, start_col, str);
}

void lcd_draw_warning_triangle(uint8_t start_page, uint8_t col) {
  if (start_page >= LCD_PAGES - 2)
    return;
  /* ASSUMPTION: 15x24 Warning Triangle with '!' inside spanning 3 pages (rows
   * 24-47) */
  static const uint8_t tri3_top[15] = {0x00, 0x00, 0x00, 0x00, 0xC0,
                                       0xF0, 0xFC, 0xFE, 0xFC, 0xF0,
                                       0xC0, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t tri3_mid[15] = {0x80, 0xFC, 0x3F, 0x0F, 0x03,
                                       0x00, 0x00, 0x3F, 0x00, 0x00,
                                       0x03, 0x0F, 0x3F, 0xFC, 0x80};
  static const uint8_t tri3_bot[15] = {0x3F, 0x30, 0x30, 0x30, 0x30,
                                       0x30, 0x30, 0x36, 0x30, 0x30,
                                       0x30, 0x30, 0x30, 0x30, 0x3F};

  for (uint8_t i = 0; i < 15; i++) {
    if (col + i < LCD_WIDTH) {
      lcd_set_cursor(start_page, col + i);
      lcd_write_data(tri3_top[i]);
      lcd_set_cursor(start_page + 1, col + i);
      lcd_write_data(tri3_mid[i]);
      lcd_set_cursor(start_page + 2, col + i);
      lcd_write_data(tri3_bot[i]);
    }
  }
}

void lcd_clear_warning_triangle(uint8_t start_page, uint8_t col) {
  if (start_page >= LCD_PAGES - 2)
    return;
  for (uint8_t i = 0; i < 16; i++) {
    if (col + i < LCD_WIDTH) {
      lcd_set_cursor(start_page, col + i);
      lcd_write_data(0x00);
      lcd_set_cursor(start_page + 1, col + i);
      lcd_write_data(0x00);
      lcd_set_cursor(start_page + 2, col + i);
      lcd_write_data(0x00);
    }
  }
}

void lcd_draw_relay_symbol(uint8_t page, uint8_t col, uint8_t is_on,
                           uint8_t inverted) {
  if (page >= LCD_PAGES)
    return;

  /* ASSUMPTION: 8x16 electrical switch relay symbol matching font8x16 */
  char sym = is_on ? RELAY_SYM_CLOSED : RELAY_SYM_OPEN;
  const font8x16_glyph_t *g = font8x16_get_glyph(sym);

  for (uint8_t i = 0; i < FONT8X16_WIDTH; i++) {
    if (col + i < LCD_WIDTH) {
      lcd_set_cursor(page, col + i);
      uint8_t t = g->top[i];
      lcd_write_data(inverted ? (uint8_t)(~t) : t);

      if (page + 1 < LCD_PAGES) {
        lcd_set_cursor(page + 1, col + i);
        uint8_t b = g->bot[i];
        lcd_write_data(inverted ? (uint8_t)(~b) : b);
      }
    }
  }
}

/* ========================================================================== */
/*                             MULTI-FONT DRAWING                             */
/* ========================================================================== */

uint8_t lcd_str_width(lcd_font_t font, const char *str) {
  if (!str)
    return 0;
  uint16_t w = 0;
  if (font == LCD_FONT_5X7) {
    w = (uint16_t)(strlen(str) * FONT5X7_PITCH);
  } else if (font == LCD_FONT_8X16) {
    w = (uint16_t)(strlen(str) * FONT8X16_WIDTH);
  } else if (font == LCD_FONT_16X24) {
    while (*str != '\0') {
      char c = *str++;
      const font16x24_glyph_t *g = font16x24_get_glyph(c);
      w += g->width;
    }
  }
  if (w > LCD_WIDTH)
    w = LCD_WIDTH;
  return (uint8_t)w;
}

void lcd_print_font_inv(lcd_font_t font, uint8_t page, uint8_t col,
                        const char *str, uint8_t invert) {
  if (!str || page >= LCD_PAGES || col >= LCD_WIDTH)
    return;

  if (font == LCD_FONT_5X7) {
    lcd_set_cursor(page, col);
    while (*str != '\0' && col < LCD_WIDTH) {
      char c = *str++;
      if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR)
        c = ' ';
      uint8_t char_idx = (uint8_t)(c - FONT5X7_FIRST_CHAR);

      for (uint8_t i = 0; i < FONT5X7_WIDTH; i++) {
        if (col < LCD_WIDTH) {
          uint8_t b = font5x7[char_idx][i];
          lcd_write_data(invert ? (uint8_t)(~b) : b);
          col++;
        }
      }

      /* 1 blank column for character spacing */
      if (col < LCD_WIDTH) {
        lcd_write_data(invert ? 0xFF : 0x00);
        col++;
      }
    }
  } else if (font == LCD_FONT_8X16) {
    if (page >= LCD_PAGES - 1)
      return;
    while (*str != '\0' && col < LCD_WIDTH) {
      char c = *str++;
      const font8x16_glyph_t *g = font8x16_get_glyph(c);

      for (uint8_t i = 0; i < FONT8X16_WIDTH; i++) {
        if (col < LCD_WIDTH) {
          lcd_set_cursor(page, col);
          uint8_t top_b = g->top[i];
          lcd_write_data(invert ? (uint8_t)(~top_b) : top_b);

          lcd_set_cursor(page + 1, col);
          uint8_t bot_b = g->bot[i];
          lcd_write_data(invert ? (uint8_t)(~bot_b) : bot_b);

          col++;
        }
      }
    }
  } else if (font == LCD_FONT_16X24) {
    if (page >= LCD_PAGES - 2)
      return;
    while (*str != '\0' && col < LCD_WIDTH) {
      char c = *str++;
      const font16x24_glyph_t *g = font16x24_get_glyph(c);

      for (uint8_t i = 0; i < g->width; i++) {
        if (col < LCD_WIDTH) {
          lcd_set_cursor(page, col);
          uint8_t top_b = g->top[i];
          lcd_write_data(invert ? (uint8_t)(~top_b) : top_b);

          lcd_set_cursor(page + 1, col);
          uint8_t mid_b = g->mid[i];
          lcd_write_data(invert ? (uint8_t)(~mid_b) : mid_b);

          lcd_set_cursor(page + 2, col);
          uint8_t bot_b = g->bot[i];
          lcd_write_data(invert ? (uint8_t)(~bot_b) : bot_b);

          col++;
        }
      }
    }
  }
}

void lcd_print_font(lcd_font_t font, uint8_t page, uint8_t col,
                    const char *str) {
  /* Inverted bars: automatically invert (white text on black) for pages 0-1 and
   * 6-7 */
  uint8_t invert = (page == 0 || page == 6 || page == 7) ? 1 : 0;
  lcd_print_font_inv(font, page, col, str, invert);
}

void lcd_print_font_centered(lcd_font_t font, uint8_t page, const char *str) {
  if (!str)
    return;
  uint8_t w = lcd_str_width(font, str);
  uint8_t start_col = 0;
  if (w < LCD_WIDTH) {
    start_col = (uint8_t)((LCD_WIDTH - w) / 2);
  }
  lcd_print_font(font, page, start_col, str);
}

void lcd_print_bar(uint8_t page, const char *str) {
  if (!str || (page != 0 && page != 6))
    return;

  uint8_t len = (uint8_t)strlen(str);
  uint8_t str_w = (uint8_t)(len * FONT8X16_WIDTH);
  uint8_t start_col = 8;
  if (str_w < LCD_WIDTH) {
    start_col = (uint8_t)((LCD_WIDTH - str_w) / 2);
  }

  /* Fill left solid black margin (2 pages tall) */
  for (uint8_t c = 0; c < start_col; c++) {
    lcd_set_cursor(page, c);
    lcd_write_data(0xFF);
    lcd_set_cursor(page + 1, c);
    lcd_write_data(0xFF);
  }

  /* Draw string with white pixels on black bar */
  lcd_print_font_inv(LCD_FONT_8X16, page, start_col, str, 1);

  /* Fill right solid black margin (2 pages tall) */
  uint8_t end_col = (uint8_t)(start_col + str_w);
  for (uint8_t c = end_col; c < LCD_WIDTH; c++) {
    lcd_set_cursor(page, c);
    lcd_write_data(0xFF);
    lcd_set_cursor(page + 1, c);
    lcd_write_data(0xFF);
  }
}

