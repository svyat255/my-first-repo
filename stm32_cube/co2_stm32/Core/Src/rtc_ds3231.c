/*
 * DS3231 на I2C2 (PB10 SCL / PB11 SDA), адрес 0x68, батарейка CR2032.
 * В регистрах — локальные стенные часы (UTC epoch + tz_hours).
 * quiet_hours смотрит только hour: без Wi‑Fi ночь всё равно тихая.
 */
#include "rtc_ds3231.h"
#include "app_config.h"
#include "i2c.h"
#include <stdio.h>

#define DS3231_ADDR (DS3231_I2C_ADDR << 1)

/**
 * bin2bcd — десятичное 0…99 в формат часов (0x23 = двадцать три, не 35).
 *
 * Старшая тетрада — десятки, младшая — единицы. Так хранит DS3231.
 */
static uint8_t bin2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

/**
 * bcd2bin — обратное преобразование регистра часов в uint8 0…99.
 */
static uint8_t bcd2bin(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10u) + (v & 0x0Fu));
}

/**
 * ds3231_init — есть ли чип на I2C2 (три попытки ACK).
 *
 * false не останавливает прошивку: измерения и звук живут без RTC,
 * просто quiet_hours_active вернёт false (день не глушится целиком).
 *
 * @return true, если устройство ответило
 */
bool ds3231_init(void)
{
    return HAL_I2C_IsDeviceReady(&hi2c2, DS3231_ADDR, 3, 100) == HAL_OK;
}

/**
 * ds3231_read — секунды, минуты, часы из регистров 0x00…0x02.
 *
 * Сначала пишем указатель регистра 0x00, потом читаем 3 байта.
 * Бит 7 секунд — Clock Halt, маскируем. Часы маской 0x3F (режим 24h).
 * Мусор hour>23 / min>59 → valid=false, тихие часы не сработают.
 *
 * @param t  выход; при ошибке t->valid = false
 * @return   true только если I2C ок и поля в диапазоне
 */
bool ds3231_read(rtc_time_t *t)
{
    uint8_t reg = 0x00;
    uint8_t buf[3];
    t->valid = false;
    if (HAL_I2C_Master_Transmit(&hi2c2, DS3231_ADDR, &reg, 1, 100) != HAL_OK) {
        return false;
    }
    if (HAL_I2C_Master_Receive(&hi2c2, DS3231_ADDR, buf, 3, 100) != HAL_OK) {
        return false;
    }
    t->sec = bcd2bin(buf[0] & 0x7Fu);
    t->min = bcd2bin(buf[1] & 0x7Fu);
    t->hour = bcd2bin(buf[2] & 0x3Fu);
    if (t->hour > 23u || t->min > 59u) {
        return false;
    }
    t->valid = true;
    return true;
}

/**
 * unix_to_hms — локальный Unix-epoch → календарь для регистров DS3231.
 *
 * Дни с 1970, високосные (в т.ч. 2000/2100), weekday: 1=вс как у Maxim.
 * Год пишется как 00…99 от 2000 (*year = y - 2000).
 *
 * @param epoch  уже локальные секунды (UTC+tz), не сырой UTC
 */
static void unix_to_hms(uint32_t epoch, uint8_t *h, uint8_t *m, uint8_t *s,
                        uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *wday)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; /* дни месяца, февраль правит високосный */
    uint32_t days, rem;
    uint16_t y;
    uint8_t mo;
    days = epoch / 86400u;
    rem = epoch % 86400u;
    *h = (uint8_t)(rem / 3600u);
    rem %= 3600u;
    *m = (uint8_t)(rem / 60u);
    *s = (uint8_t)(rem % 60u);
    *wday = (uint8_t)((days + 4u) % 7u + 1u);
    y = 1970;
    for (;;) {
        uint16_t diy = ((y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u)) ? 366u : 365u;
        if (days < diy) {
            break;
        }
        days -= diy;
        y++;
    }
    *year = (uint8_t)(y - 2000u);
    for (mo = 0; mo < 12u; mo++) {
        uint8_t md = dim[mo];
        if (mo == 1u && ((y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u))) {
            md = 29;
        }
        if (days < md) {
            break;
        }
        days -= md;
    }
    *month = (uint8_t)(mo + 1u);
    *day = (uint8_t)(days + 1u);
}

/**
 * ds3231_set_epoch_utc — записать NTP-время с ESP32 в чип как локальное.
 *
 * ESP32 шлёт Unix UTC. Прибавляем tz_hours*3600 и пишем 7 регистров с 0x00
 * одним пакетом (sec…year). Год > 99 подменяем на 26 — защита от безумного epoch.
 *
 * @param epoch     секунды UTC (0 или < 1.7e9 вызывающая сторона не должна слать)
 * @param tz_hours  смещение зоны, как в настройках (−12…+14)
 * @return          true при успешном I2C
 */
bool ds3231_set_epoch_utc(uint32_t epoch, int8_t tz_hours)
{
    int32_t local = (int32_t)epoch + (int32_t)tz_hours * 3600;
    uint8_t buf[8];
    uint8_t h, m, s, y, mo, d, w;
    if (local < 0) {
        local = 0;
    }
    unix_to_hms((uint32_t)local, &h, &m, &s, &y, &mo, &d, &w);
    if (y > 99u) {
        y = 26;
    }
    buf[0] = 0x00;
    buf[1] = bin2bcd(s);
    buf[2] = bin2bcd(m);
    buf[3] = bin2bcd(h);
    buf[4] = bin2bcd(w);
    buf[5] = bin2bcd(d);
    buf[6] = bin2bcd(mo);
    buf[7] = bin2bcd(y);
    return HAL_I2C_Master_Transmit(&hi2c2, DS3231_ADDR, buf, 8, 200) == HAL_OK;
}

/**
 * ds3231_format_hhmm — "22:15" для JSON/LCD или "--:--" если часы невалидны.
 *
 * @param t        результат ds3231_read
 * @param out      буфер не короче 6 байт
 * @param out_len  sizeof буфера; меньше 6 — ничего не пишем
 */
void ds3231_format_hhmm(const rtc_time_t *t, char *out, uint8_t out_len)
{
    if (out_len < 6u) {
        return;
    }
    if (!t->valid) {
        out[0] = '-';
        out[1] = '-';
        out[2] = ':';
        out[3] = '-';
        out[4] = '-';
        out[5] = '\0';
        return;
    }
    (void)snprintf(out, out_len, "%02u:%02u", t->hour, t->min);
}
