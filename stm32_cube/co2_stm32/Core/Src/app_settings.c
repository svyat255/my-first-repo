/*
 * Пороги во flash STM32: без ESP32 остаются те же warn/crit/quiet.
 * Адрес 0x0800FC00 — последняя 1 КБ страница 64 КБ F103C8.
 * Стирается целиком: не вызывать save в каждом обороте main (ESP пушит раз в 60 с).
 */
#include "app_settings.h"
#include "app_config.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* Образ страницы 0x0800FC00. Пишет settings_save, читает settings_load. */
typedef struct {
    uint32_t magic;           /* SETTINGS_MAGIC, иначе страница считается пустой */
    uint16_t co2_warn;        /* копия RAM-порога */
    uint16_t co2_crit;
    uint8_t quiet_start;
    uint8_t quiet_end;
    int8_t tz_hours;
    uint8_t alerts_enabled;   /* 0/1, не bool — выравнивание flash halfword */
    uint32_t reserved;        /* запас под будущие поля, всегда 0 */
} settings_blob_t __attribute__((aligned(4)));

/**
 * settings_defaults — завод: 800/1200 ppm, тихие часы 22–07, UTC+3, звук вкл.
 *
 * @param s  заполняемая структура в RAM
 *
 * Ловушка: flash не трогает — только RAM. Вызывать из load, не вместо save.
 */
void settings_defaults(app_settings_t *s)
{
    s->co2_warn = DEFAULT_CO2_WARN;
    s->co2_crit = DEFAULT_CO2_CRIT;
    s->quiet_start = DEFAULT_QUIET_START;
    s->quiet_end = DEFAULT_QUIET_END;
    s->tz_hours = DEFAULT_TZ_HOURS;
    s->alerts_enabled = true;
}

/**
 * settings_load — defaults, затем overlay с страницы, если magic = 0xC0C0A11A.
 *
 * Битые пороги (warn ≥ crit, вне диапазона) не берём — остаются заводские.
 * Часы quiet 0…23, tz −12…+14. alerts_enabled — любой ненулевой байт = true.
 *
 * @param s  выход; всегда валиден после вызова
 */
void settings_load(app_settings_t *s)
{
    const settings_blob_t *blob = (const settings_blob_t *)SETTINGS_FLASH_ADDR;
    settings_defaults(s);
    if (blob->magic != SETTINGS_MAGIC) {
        return;
    }
    if (blob->co2_warn >= 400u && blob->co2_warn < blob->co2_crit && blob->co2_crit <= 5000u) {
        s->co2_warn = blob->co2_warn;
        s->co2_crit = blob->co2_crit;
    }
    if (blob->quiet_start <= 23u && blob->quiet_end <= 23u) {
        s->quiet_start = blob->quiet_start;
        s->quiet_end = blob->quiet_end;
    }
    if (blob->tz_hours >= -12 && blob->tz_hours <= 14) {
        s->tz_hours = blob->tz_hours;
    }
    s->alerts_enabled = blob->alerts_enabled != 0u;
}

/**
 * settings_save — стереть страницу и записать blob полусловами.
 *
 * F103 умеет program только 16 бит. Unlock → erase 1 page → halfword loop → Lock.
 * Ошибка erase/program: тихий выход, RAM в main уже новая, flash отстанет
 * до следующего успешного save (ESP пушит снова через минуту).
 *
 * @param s  то, что лежит в RAM после UART
 */
void settings_save(const app_settings_t *s)
{
    settings_blob_t blob;
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;
    uint32_t addr;
    const uint16_t *src;
    uint32_t i;
    uint32_t words;

    memset(&blob, 0, sizeof(blob));
    blob.magic = SETTINGS_MAGIC;
    blob.co2_warn = s->co2_warn;
    blob.co2_crit = s->co2_crit;
    blob.quiet_start = s->quiet_start;
    blob.quiet_end = s->quiet_end;
    blob.tz_hours = s->tz_hours;
    blob.alerts_enabled = s->alerts_enabled ? 1u : 0u;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.PageAddress = SETTINGS_FLASH_ADDR;
    erase.NbPages = 1;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return;
    }
    src = (const uint16_t *)&blob;
    words = (sizeof(blob) + 1u) / 2u;
    addr = SETTINGS_FLASH_ADDR;
    for (i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, src[i]) != HAL_OK) {
            break;
        }
        addr += 2u;
    }
    HAL_FLASH_Lock();
}
