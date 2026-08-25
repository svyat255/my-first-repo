#ifndef RTC_DS3231_H
#define RTC_DS3231_H

#include <stdint.h>
#include <stdbool.h>

/* Часы на I2C2. В чипе — локальное время (UTC epoch + tz). */

typedef struct {
    uint8_t hour;  /* 0…23; пишет ds3231_read, читают quiet_hours и format_hhmm */
    uint8_t min;   /* 0…59 */
    uint8_t sec;   /* 0…59 */
    bool valid;    /* false при ошибке I2C или мусоре в регистрах — ночь тогда не глушит */
} rtc_time_t;

/**
 * ds3231_init — три попытки ACK на I2C2.
 * @return true, если чип ответил. Ловушка: false не фатален — измерения живут без RTC.
 */
bool ds3231_init(void);

/**
 * ds3231_read — sec/min/hour из регистров 0x00…0x02.
 * @param t  выход; при ошибке t->valid = false
 * @return   true только если I2C ок и поля в диапазоне
 * Ловушка: только hi2c2. Из main не чаще 1 Гц — иначе убиваем шину.
 */
bool ds3231_read(rtc_time_t *t);

/**
 * ds3231_set_epoch_utc — NTP UTC + tz → локальные регистры чипа.
 * @param epoch     секунды UTC (> 1.7e9 проверяет вызывающая сторона)
 * @param tz_hours  −12…+14
 * @return          true при успешном I2C
 * Ловушка: flash не трогает. Только из main / esp_link_poll, не из IRQ.
 */
bool ds3231_set_epoch_utc(uint32_t epoch, int8_t tz_hours);

/**
 * ds3231_format_hhmm — "22:15" или "--:--" в буфер ≥ 6 байт.
 * @param t        результат ds3231_read
 * @param out      буфер
 * @param out_len  sizeof; меньше 6 — ничего не пишем
 */
void ds3231_format_hhmm(const rtc_time_t *t, char *out, uint8_t out_len);

#endif
