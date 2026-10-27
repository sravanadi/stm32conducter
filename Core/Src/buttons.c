/**
 * @file    buttons.c
 * @brief   Hardware button driver with instant-touch response, debounce,
 *          fast auto-repeat, and long-press detection:
 *            1st (far left)  SW4 (PB4, Pin 28) = MENU / RETURN
 *            2nd             SW3 (PB3, Pin 27) = DOWN (v)
 *            3rd             SW2 (PB2, Pin 17) = RIGHT (->)
 *            4th (far right) SW1 (PB1, Pin 16) = ENTER (Check)
 */

#include "buttons.h"
#include "stm32g0xx_hal.h"

#define DEBOUNCE_TIME_MS       12    /* 12 ms crisp, ultra-responsive debounce */
#define LONG_PRESS_TIME_MS     400   /* 400 ms hold on MENU to return to measurement */
#define REPEAT_DELAY_MS        250   /* 250 ms hold before auto-repeat begins */
#define REPEAT_RATE_MS         60    /* 60 ms repeat interval (~16 steps/second) */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t     pin;
    uint8_t      state;          /* 1 = pressed, 0 = released */
    uint8_t      last_raw;
    uint32_t     last_change;
    uint32_t     press_start;
    uint32_t     last_repeat;
    bool         long_fired;
} hw_button_t;

/* Physical pins matching board schematic (PB4=SW4, PB3=SW3, PB2=SW2, PB1=SW1) */
static hw_button_t s_hw_btns[4] = {
    { SW4_PORT, SW4_PIN, 0, 0, 0, 0, 0, false }, /* Index 0: 1st (far left)  SW4 (PB4) = MENU  */
    { SW3_PORT, SW3_PIN, 0, 0, 0, 0, 0, false }, /* Index 1: 2nd             SW3 (PB3) = DOWN  */
    { SW2_PORT, SW2_PIN, 0, 0, 0, 0, 0, false }, /* Index 2: 3rd             SW2 (PB2) = RIGHT */
    { SW1_PORT, SW1_PIN, 0, 0, 0, 0, 0, false }  /* Index 3: 4th (far right) SW1 (PB1) = ENTER */
};

static uint8_t s_event_queue = KEY_NONE;

void buttons_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SW1_PIN | SW2_PIN | SW3_PIN | SW4_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0; i < 4; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(s_hw_btns[i].port, s_hw_btns[i].pin) == GPIO_PIN_RESET) ? 1 : 0;
        s_hw_btns[i].state = raw;
        s_hw_btns[i].last_raw = raw;
        s_hw_btns[i].last_change = now;
        s_hw_btns[i].press_start = 0;
        s_hw_btns[i].last_repeat = 0;
        s_hw_btns[i].long_fired = false;
    }
    s_event_queue = KEY_NONE;
}

void buttons_update(void)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < 4; i++)
    {
        uint8_t raw = (HAL_GPIO_ReadPin(s_hw_btns[i].port, s_hw_btns[i].pin) == GPIO_PIN_RESET) ? 1 : 0;

        if (raw != s_hw_btns[i].last_raw)
        {
            s_hw_btns[i].last_change = now;
            s_hw_btns[i].last_raw = raw;
        }

        if ((now - s_hw_btns[i].last_change) >= DEBOUNCE_TIME_MS)
        {
            if (raw != s_hw_btns[i].state)
            {
                s_hw_btns[i].state = raw;

                if (s_hw_btns[i].state == 1)
                {
                    /* Button pressed down: fire instant events */
                    s_hw_btns[i].press_start = now;
                    s_hw_btns[i].last_repeat = now;
                    s_hw_btns[i].long_fired = false;

                    if (i == 1)      /* 2nd: SW3 (PB3) = DOWN -> INSTANT */
                    {
                        s_event_queue = KEY_SW3_DOWN;
                    }
                    else if (i == 2) /* 3rd: SW2 (PB2) = RIGHT -> INSTANT */
                    {
                        s_event_queue = KEY_SW2_RIGHT;
                    }
                    else if (i == 3) /* 4th: SW1 (PB1) = ENTER -> INSTANT */
                    {
                        s_event_queue = KEY_SW1_ENTER;
                    }
                }
                else
                {
                    /* Button released: SW4 (PB4 = MENU) fires if released before long-press threshold */
                    if (i == 0 && !s_hw_btns[0].long_fired)
                    {
                        s_event_queue = KEY_SW4_MENU;
                    }
                }
            }
            else if (s_hw_btns[i].state == 1)
            {
                /* 1. SW4: Check for 400 ms long press */
                if (i == 0 && !s_hw_btns[0].long_fired)
                {
                    if ((now - s_hw_btns[0].press_start) >= LONG_PRESS_TIME_MS)
                    {
                        s_hw_btns[0].long_fired = true;
                        s_event_queue = KEY_SW4_LONG;
                    }
                }
                /* 2. SW3 (DOWN) & SW2 (RIGHT): Smooth fast auto-repeat while held */
                else if (i == 1 || i == 2)
                {
                    if ((now - s_hw_btns[i].press_start) >= REPEAT_DELAY_MS)
                    {
                        if ((now - s_hw_btns[i].last_repeat) >= REPEAT_RATE_MS)
                        {
                            s_hw_btns[i].last_repeat = now;
                            if (i == 1)      s_event_queue = KEY_SW3_DOWN;
                            else if (i == 2) s_event_queue = KEY_SW2_RIGHT;
                        }
                    }
                }
            }
        }
    }
}

uint8_t key_down(void)
{
    /* Active low detection on PB1..PB4 */
    uint32_t idr = GPIOB->IDR;
    /* Physical left-to-right order: PB4 (MENU), PB3 (DOWN), PB2 (RIGHT), PB1 (ENTER) */
    if (!(idr & (1UL << 4))) return 1; /* PB4: MENU */
    if (!(idr & (1UL << 3))) return 2; /* PB3: DOWN */
    if (!(idr & (1UL << 2))) return 3; /* PB2: RIGHT */
    if (!(idr & (1UL << 1))) return 4; /* PB1: ENTER */
    return 0;
}

uint8_t key_get(void)
{
    uint8_t k = s_event_queue;
    s_event_queue = KEY_NONE;
    return k;
}

btn_event_t buttons_get_event(void)
{
    return (btn_event_t)key_get();
}

void buttons_clear(void)
{
    s_event_queue = KEY_NONE;
}