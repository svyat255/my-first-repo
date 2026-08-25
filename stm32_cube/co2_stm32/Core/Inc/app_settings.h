#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/* Пороги в RAM. На диск: последняя страница flash SETTINGS_FLASH_ADDR. */

typedef struct {
    uint16_t co2_warn;      /* ppm входа в Warning. Пишет load/UART, читает alert_fsm */
    uint16_t co2_crit;      /* ppm входа в Alarm. Должен быть > warn */
    uint8_t quiet_start;    /* час начала тишины 0…23 */
    uint8_t quiet_end;      /* час конца; 22→7 через полночь */
    int8_t tz_hours;        /* смещение при записи epoch в DS3231 */
    bool alerts_enabled;    /* тумблер «звук по порогам» с веба */
} app_settings_t;

/**
 * settings_load — заводские умолчания, затем overlay со страницы 0x0800FC00.
 * @param s  выход; всегда валиден после вызова
 * Ловушка: битый magic → defaults. Вызывать один раз после MX_*_Init.
 */
void settings_load(app_settings_t *s);

/**
 * settings_save — стереть страницу и записать blob полусловами.
 * @param s  то, что лежит в RAM
 * Ловушка: erase всей 1 КБ страницы. Не звать на каждый NTP epoch — только если пороги изменились.
 */
void settings_save(const app_settings_t *s);

/**
 * settings_defaults — завод: 800/1200, тихие 22–07, UTC+3, звук вкл.
 * @param s  заполняемая структура в RAM
 */
void settings_defaults(app_settings_t *s);

#endif
