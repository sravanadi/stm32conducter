/**
 * @file    meter_flash.c
 * @brief   Non-volatile settings persistence in STM32G030 Page 31 (0x0800F800).
 */

#include "meter_flash.h"
#include "stm32g030xx.h"
#include <string.h>

#define FLASH_KEY1_VAL      0x45670123UL
#define FLASH_KEY2_VAL      0xCDEF89ABUL

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = FLASH_KEY1_VAL;
        FLASH->KEYR = FLASH_KEY2_VAL;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_wait_bsy(void)
{
    while (FLASH->SR & FLASH_SR_BSY1)
    {
        /* Busy wait until flash operation completes */
    }
}

void meter_flash_load(settings_t *s)
{
    if (!s) return;

    const settings_t *flash_ptr = (const settings_t *)METER_FLASH_BASE_ADDR;

    /* Verify Magic Number and Version */
    if (flash_ptr->magic == METER_SETTINGS_MAGIC && flash_ptr->version == METER_SETTINGS_VERSION)
    {
        uint16_t expected_crc = crc16_modbus((const uint8_t *)flash_ptr, sizeof(settings_t) - sizeof(uint16_t));
        if (expected_crc == flash_ptr->crc)
        {
            memcpy(s, flash_ptr, sizeof(settings_t));

            /* Validate all parameter ranges per specification */
            bool valid = true;
            if (s->k_constant < 0.001f || s->k_constant > 99.999f) valid = false;
            if (s->ec_factor < 0.001f || s->ec_factor > 99.999f) valid = false;
            if (s->temp_coef < 0.00f || s->temp_coef > 99.99f) valid = false;
            if (s->tds_coef < 0.001f || s->tds_coef > 99.999f) valid = false;
            if (s->slave_addr < 1 || s->slave_addr > 247) valid = false;
            if (s->contrast > 63) valid = false;
            if (s->display_unit > 2) valid = false;
            if (s->temp_probe_type > 2) valid = false;

            if (valid)
            {
                return;
            }
        }
    }

    /* If unprogrammed, corrupted, bad CRC, or invalid range, restore defaults */
    meter_core_default_settings(s);
    meter_flash_save(s);
}

bool meter_flash_save(const settings_t *s)
{
    if (!s) return false;

    /* 1. Check if settings actually changed (wear prevention) */
    const settings_t *current = (const settings_t *)METER_FLASH_BASE_ADDR;
    settings_t new_copy;
    memcpy(&new_copy, s, sizeof(settings_t));
    new_copy.magic   = METER_SETTINGS_MAGIC;
    new_copy.version = METER_SETTINGS_VERSION;
    new_copy.crc     = crc16_modbus((const uint8_t *)&new_copy, sizeof(settings_t) - sizeof(uint16_t));

    if (memcmp(current, &new_copy, sizeof(settings_t)) == 0)
    {
        return true; /* Already identical in flash */
    }

    /* 2. Unlock Flash Controller */
    flash_wait_bsy();
    flash_unlock();

    /* Clear any lingering error status flags */
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR |
                FLASH_SR_FASTERR | FLASH_SR_OPTVERR;

    /* 3. Page Erase (Page 31 = 0x0800F800) */
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB_Msk) | (METER_FLASH_PAGE_NUM << FLASH_CR_PNB_Pos) | FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_STRT;
    flash_wait_bsy();
    FLASH->CR &= ~FLASH_CR_PER;

    /* 4. Double-Word Programming (64-bit = 8-byte steps) */
    FLASH->CR |= FLASH_CR_PG;

    uint32_t dest_addr = METER_FLASH_BASE_ADDR;
    const uint32_t *src_words = (const uint32_t *)&new_copy;
    uint32_t num_double_words = (sizeof(settings_t) + 7) / 8;

    for (uint32_t i = 0; i < num_double_words; i++)
    {
        volatile uint32_t *dest = (volatile uint32_t *)dest_addr;
        dest[0] = src_words[i * 2 + 0];
        dest[1] = src_words[i * 2 + 1];

        dest_addr += 8;
        flash_wait_bsy();

        if (FLASH->SR & (FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR))
        {
            FLASH->CR &= ~FLASH_CR_PG;
            flash_lock();
            return false;
        }
    }

    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();

    /* Verify written data */
    return (memcmp((const void *)METER_FLASH_BASE_ADDR, &new_copy, sizeof(settings_t)) == 0);
}
