/**
 * @file    lcd_st7565.h
 * @brief   Driver for NHD-C12864LZ-FSW-FBW-3V3 (Sitronix ST7565R Controller)
 *          128x64 Graphic Monochrome LCD in 4-wire serial SPI mode.
 */

#ifndef __LCD_ST7565_H
#define __LCD_ST7565_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"
#include "board_pins.h"

/* ========================================================================== */
/*                           DISPLAY GEOMETRY                                 */
/* ========================================================================== */
#define LCD_WIDTH                   128
#define LCD_HEIGHT                  64
#define LCD_PAGES                   8     /* 64 rows / 8 bits per page = 8 pages */

/*
 * Column offset:
 * ST7565R internal RAM has 132 columns (0..131).
 * Most 128x64 glass displays align column 0 to RAM address 0 (offset 0).
 * If your module has a 4-pixel shift, set this to 4.
 */
#define LCD_COL_OFFSET              0

/* ========================================================================== */
/*                EASY-TO-CHANGE ORIENTATION & CONTRAST CONFIG                */
/* ========================================================================== */

/*
 * Hardware SPI vs GPIO Bit-Bang:
 *   1 = Use STM32 Hardware SPI1 on PA1 (SCK) and PA2 (MOSI)
 *   0 = Use GPIO Bit-Bang on LCD_SCK_PIN and LCD_MOSI_PIN
 */
#define LCD_USE_HARDWARE_SPI        1

/*
 * Segment Driver Direction (Horizontal flip / ADC select):
 *   0xA0 = Normal (SEG0 -> SEG127, Column 0 on the left)
 *   0xA1 = Reverse (SEG127 -> SEG0, Column 127 on the left, horizontally mirrored)
 */
#define LCD_ORIENTATION_ADC         0xA0

/*
 * Common Driver Direction (Vertical flip / COM scan):
 *   0xC8 = Reverse scan direction (COM63 -> COM0, standard for NHD-C12864LZ)
 *   0xC0 = Normal scan direction (COM0 -> COM63, upside down)
 */
#define LCD_ORIENTATION_COM         0xC8

/*
 * LCD Bias Selection:
 *   0xA2 = Bias 1/9 (Recommended for 1/65 duty, standard for NHD-C12864LZ-FSW-FBW-3V3)
 *   0xA3 = Bias 1/7
 */
#define LCD_BIAS_MODE               0xA2

/*
 * Internal Regulator Resistor Ratio (Coarse contrast):
 *   Range: 0x20 to 0x27 (Ratio 3.0 to 6.5)
 *   0x25 (Ratio 5.5) or 0x26 (Ratio 6.0) is standard for 3.3V operation.
 */
#define LCD_REGULATOR_RATIO         0x25

#define LCD_CONTRAST_VALUE          0x0C  /* Very low contrast (0x0C / 12) - prevents background ghost dots */
#define LCD_INVERSE_MODE            0xA6  /* Normal display (1=dark pixel, 0=clear background) */

/* ========================================================================== */
/*                          ST7565R COMMAND CODES                             */
/* ========================================================================== */
#define ST7565_CMD_DISPLAY_OFF      0xAE  /* Turn display OFF */
#define ST7565_CMD_DISPLAY_ON       0xAF  /* Turn display ON */
#define ST7565_CMD_SET_START_LINE   0x40  /* Set Display Start Line (0x40 | 0..63) */
#define ST7565_CMD_SET_PAGE         0xB0  /* Set Page Address (0xB0 | 0..7) */
#define ST7565_CMD_SET_COL_HIGH     0x10  /* Set Column Address MSB (0x10 | col>>4) */
#define ST7565_CMD_SET_COL_LOW      0x00  /* Set Column Address LSB (0x00 | col&0x0F) */
#define ST7565_CMD_ALL_ON_NORMAL    0xA4  /* Display follows RAM content */
#define ST7565_CMD_ALL_ON_FORCE     0xA5  /* Entire display ON (test pattern) */
#define ST7565_CMD_RESET            0xE2  /* Software Reset */
#define ST7565_CMD_POWER_CTRL       0x28  /* Power Controller Set base (0x28 | B | R | F) */
#define ST7565_CMD_ELEC_VOL_MODE    0x81  /* Enter Electronic Volume mode */


/* ========================================================================== */
/*                           PUBLIC FUNCTION APIS                             */
/* ========================================================================== */

/**
 * @brief  Initialize the ST7565R LCD controller and power on backlight.
 *         Applies hardware reset pulse, sends init commands, and clears screen.
 */
void lcd_init(void);

/**
 * @brief  Send a single command byte to the ST7565R controller (A0 = LOW).
 * @param  cmd: 8-bit ST7565R command byte
 */
void lcd_write_cmd(uint8_t cmd);

/**
 * @brief  Send a single display data byte to the ST7565R RAM (A0 = HIGH).
 * @param  data: 8 vertical pixel bits (Bit 0 = top, Bit 7 = bottom)
 */
void lcd_write_data(uint8_t data);

/**
 * @brief  Clear the entire 128x64 display RAM (8 pages x 128 columns = 1024 bytes).
 */
void lcd_clear(void);

/**
 * @brief  Set the current drawing cursor position in display RAM.
 * @param  page: Page index (0 to 7, each page = 8 vertical pixels)
 * @param  col:  Column index (0 to 127)
 */
void lcd_set_cursor(uint8_t page, uint8_t col);

/**
 * @brief  Print an ASCII string starting at the specified page and column.
 * @param  page: Page index (0 to 7)
 * @param  col:  Column index (0 to 127)
 * @param  str:  Null-terminated ASCII string
 */
void lcd_print(uint8_t page, uint8_t col, const char *str);

/**
 * @brief  Print an ASCII string horizontally centered on the specified page.
 * @param  page: Page index (0 to 7)
 * @param  str:  Null-terminated ASCII string
 */
void lcd_print_centered(uint8_t page, const char *str);

/**
 * @brief  Print a compact 3x5 text string starting at the specified page and column.
 *         Pitch is 4 pixels per character (3px glyph + 1px spacing).
 *         Supports up to 32 characters on a single 128-column row.
 * @param  page: Page index (0 to 7)
 * @param  col:  Column index (0 to 127)
 * @param  str:  Null-terminated ASCII string
 */
void lcd_print_compact(uint8_t page, uint8_t col, const char *str);

/**
 * @brief  Print a compact 3x5 text string horizontally centered on the specified page.
 * @param  page: Page index (0 to 7)
 * @param  str:  Null-terminated ASCII string
 */
void lcd_print_compact_centered(uint8_t page, const char *str);

/**
 * @brief  Turn the LCD LED backlight ON or OFF via DISP_LED_EN (PC6).
 * @param  on: 1 = Backlight ON, 0 = Backlight OFF
 */
void lcd_set_backlight(uint8_t on);

/**
 * @brief  Adjust the fine contrast level (Electronic Volume) at runtime.
 * @param  contrast: Value from 0x00 (lowest) to 0x3F (highest)
 */
void lcd_set_contrast(uint8_t contrast);

/**
 * @brief  Clear a single page row (8 pixels high by 128 pixels wide).
 * @param  page: Page index (0 to 7)
 */
void lcd_clear_page(uint8_t page);

/**
 * @brief  Draw a horizontal pattern line across the full 128-column width of a page.
 * @param  page: Page index (0 to 7)
 * @param  pattern: 8-bit vertical column pattern (e.g. 0x08 for a thin horizontal line)
 */
void lcd_draw_hline(uint8_t page, uint8_t pattern);

/**
 * @brief  Print an ASCII string with optional inverse highlight (for menu selection).
 * @param  page: Page index (0 to 7)
 * @param  col: Column index (0 to 127)
 * @param  str: ASCII string
 * @param  invert: 1 = Inverted (dark background, clear text), 0 = Normal
 */
void lcd_print_inverse(uint8_t page, uint8_t col, const char *str, uint8_t invert);

/**
 * @brief  Print a full menu line across all 128 columns, padding to end of page.
 * @param  page: Page index (0 to 7)
 * @param  str: Text to display
 * @param  selected: 1 = Inverted selection highlight, 0 = Normal
 */
void lcd_print_line(uint8_t page, const char *str, uint8_t selected);

/**
 * @brief  Print 2x scaled large characters (10x14 pixels, spans 2 vertical pages).
 * @param  page: Upper page index (0 to 6)
 * @param  col: Column index (0 to 127)
 * @param  str: ASCII string (digits, letters, symbols)
 */
void lcd_print_large(uint8_t page, uint8_t col, const char *str);
void lcd_print_large_centered(uint8_t page, const char *str);

/**
 * @brief  Print 10x16 dedicated large characters (spans 2 vertical pages).
 * @param  page: Upper page index (0 to 6)
 * @param  col:  Column index (0 to 127)
 * @param  str:  String to print (digits 0-9, '.', '-', space, and status words)
 */
void lcd_print_10x16(uint8_t page, uint8_t col, const char *str);

/**
 * @brief  Print 10x16 large characters centered horizontally across the display.
 * @param  page: Upper page index (0 to 6)
 * @param  str:  String to print
 */
void lcd_print_10x16_centered(uint8_t page, const char *str);

/**
 * @brief  Draw a warning triangle symbol with '!' inside at start_page (spans 2 pages).
 * @param  start_page: Upper page (e.g. Page 3)
 * @param  col: Column index (e.g. col 4)
 */
void lcd_draw_warning_triangle(uint8_t start_page, uint8_t col);

/**
 * @brief  Clear the warning triangle area at start_page (spans 2 pages).
 * @param  start_page: Upper page
 * @param  col: Column index
 */
void lcd_clear_warning_triangle(uint8_t start_page, uint8_t col);

/**
 * @brief  Draw an electrical switch relay symbol on the top bar.
 * @param  page: Page index (Page 0)
 * @param  col: Column index
 * @param  is_on: 1 = switch closed (ON), 0 = switch open (OFF)
 * @param  inverted: 1 = white symbol on solid black bar, 0 = normal
 */
void lcd_draw_relay_symbol(uint8_t page, uint8_t col, uint8_t is_on, uint8_t inverted);

/* ========================================================================== */
/*                             MULTI-FONT SUPPORT                             */
/* ========================================================================== */

typedef enum {
    LCD_FONT_5X7 = 0,   /* Standard 5x7 ASCII font, 6px pitch, 1 page tall  */
    LCD_FONT_8X16,      /* 8x16 font, 8px pitch, 2 pages tall (16px)        */
    LCD_FONT_16X24      /* 16x24 large font, up to 16px, 3 pages tall (24px) */
} lcd_font_t;

/**
 * @brief  Returns the pixel width of a string in the specified font.
 *         Ensures calculations prevent strings from exceeding column 127.
 * @param  font: LCD_FONT_5X7, LCD_FONT_8X16, or LCD_FONT_16X24
 * @param  str: Null-terminated ASCII string
 * @return Pixel width of the string (0 to LCD_WIDTH)
 */
uint8_t lcd_str_width(lcd_font_t font, const char *str);

/**
 * @brief  Draw a string in any of the 3 fonts with strict boundary checking.
 *         Automatically inverts bytes (XOR 0xFF) for pages 0-1 and 6-7.
 * @param  font: LCD_FONT_5X7, LCD_FONT_8X16, or LCD_FONT_16X24
 * @param  page: Starting page (0 to 7)
 * @param  col: Starting column (0 to 127)
 * @param  str: String to draw (will not exceed col 127)
 */
void lcd_print_font(lcd_font_t font, uint8_t page, uint8_t col, const char *str);

/**
 * @brief  Draw a string with explicit inverted/normal selection.
 * @param  font: LCD_FONT_5X7, LCD_FONT_8X16, or LCD_FONT_16X24
 * @param  page: Starting page (0 to 7)
 * @param  col: Starting column (0 to 127)
 * @param  str: String to draw (will not exceed col 127)
 * @param  invert: 1 = inverted (white on black), 0 = normal
 */
void lcd_print_font_inv(lcd_font_t font, uint8_t page, uint8_t col, const char *str, uint8_t invert);

/**
 * @brief  Print string centered horizontally using the given font.
 * @param  font: LCD_FONT_5X7, LCD_FONT_8X16, or LCD_FONT_16X24
 * @param  page: Starting page (0 to 7)
 * @param  str: String to print
 */
void lcd_print_font_centered(lcd_font_t font, uint8_t page, const char *str);

/**
 * @brief  Draw a full 128-column solid black inverted bar (pages 0-1 or 6-7)
 *         with centered/padded text in 7x16 font.
 * @param  page: 0 (top bar) or 6 (sub-display bar)
 * @param  str: Text string (up to 16 chars)
 */
void lcd_print_bar(uint8_t page, const char *str);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_ST7565_H */
