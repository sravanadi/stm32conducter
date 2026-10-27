/**
 * @file    buttons.h
 * @brief   Hardware button driver with debounce and long-press detection
 *          matching physical order (left to right):
 *            1st (far left)  SW4 (PB4): MENU.   Short: open menu / back.
 *                                               Long (~1s): return to measurement.
 *            2nd             SW3 (PB3): DOWN.   Measurement: switch sub-bar.
 *                                               Menus: cursor down / digit minus.
 *            3rd             SW2 (PB2): RIGHT.  Measurement: cycle COND-RES-TDS-SAL.
 *                                               Menus: cursor up / next digit.
 *            4th (far right) SW1 (PB1): ENTER.  Measurement: status screen.
 *                                               Menus: select item / save value.
 *
 *          key_get() returns:
 *            1 for SW4 (MENU short press)
 *            2 for SW3 (DOWN)
 *            3 for SW2 (RIGHT)
 *            4 for SW1 (ENTER)
 *            5 for SW4 (MENU long press ~1s)
 *            0 for no key
 */

#ifndef __BUTTONS_H
#define __BUTTONS_H

#include "board_pins.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Key return values (1-4 matching physical left-to-right order: SW4, SW3, SW2, SW1) */
#define KEY_NONE            0
#define KEY_SW4_MENU        1   /* 1st (far left)  SW4 (PB4): MENU short press */
#define KEY_SW3_DOWN        2   /* 2nd             SW3 (PB3): DOWN */
#define KEY_SW2_RIGHT       3   /* 3rd             SW2 (PB2): RIGHT */
#define KEY_SW1_ENTER       4   /* 4th (far right) SW1 (PB1): ENTER */
#define KEY_SW4_LONG        5   /* 1st (far left)  SW4 (PB4) long press (~1 s): return to measurement mode */

/* Direct pin and switch aliases for maximum compatibility */
#define KEY_PB4_MENU        KEY_SW4_MENU
#define KEY_PB3_DOWN        KEY_SW3_DOWN
#define KEY_PB2_RIGHT       KEY_SW2_RIGHT
#define KEY_PB1_ENTER       KEY_SW1_ENTER
#define KEY_PB4_LONG        KEY_SW4_LONG
#define KEY_PB1_MENU        KEY_SW4_MENU
#define KEY_PB2_DOWN        KEY_SW3_DOWN
#define KEY_PB3_RIGHT       KEY_SW2_RIGHT
#define KEY_PB4_ENTER       KEY_SW1_ENTER
#define KEY_PB1_LONG        KEY_SW4_LONG

typedef enum {
    BTN_EVT_NONE        = KEY_NONE,
    BTN_EVT_MENU_CLICK  = KEY_SW4_MENU,
    BTN_EVT_DOWN_CLICK  = KEY_SW3_DOWN,
    BTN_EVT_RIGHT_CLICK = KEY_SW2_RIGHT,
    BTN_EVT_ENTER_CLICK = KEY_SW1_ENTER,
    BTN_EVT_MENU_LONG   = KEY_SW4_LONG
} btn_event_t;

void buttons_init(void);
void buttons_update(void);

/**
 * @brief  Returns active-low raw key state (1..4 for PB4..PB1, 0 for none).
 *         1 = MENU (PB4/PB1), 2 = DOWN (PB3), 3 = RIGHT (PB2), 4 = ENTER (PB1/PB4).
 */
uint8_t key_down(void);

/**
 * @brief  Get debounced key press event.
 * @return 1 for MENU, 2 for DOWN, 3 for RIGHT, 4 for ENTER, 5 for MENU Long, 0 if none.
 */
uint8_t key_get(void);

btn_event_t buttons_get_event(void);
void buttons_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTONS_H */
