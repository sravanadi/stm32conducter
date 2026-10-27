/**
 * @file    meter_flash.h
 * @brief   Non-volatile settings persistence in STM32G030 Page 31 (0x0800F800).
 */

#ifndef __METER_FLASH_H
#define __METER_FLASH_H

#include "meter_core.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METER_FLASH_PAGE_NUM        31
#define METER_FLASH_BASE_ADDR       0x0800F800UL

/**
 * @brief Load settings from Flash. If invalid/corrupted, loads defaults.
 */
void meter_flash_load(settings_t *s);

/**
 * @brief Save settings to Flash Page 31 only if data has changed.
 * @return true if successfully written or unchanged, false on write error.
 */
bool meter_flash_save(const settings_t *s);

#ifdef __cplusplus
}
#endif

#endif /* __METER_FLASH_H */
