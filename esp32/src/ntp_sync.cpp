#include "ntp_sync.h"
#include <time.h>
#include <WiFi.h>

/** s_tz — часы смещения UTC, заданные последним ntp_begin; пишет ntp_begin, читает ntp_hhmm (сейчас только чтобы поле не было «мёртвым»). */
static int8_t s_tz = 3;

/**
 * ntp_begin — подписать ESP на NTP и задать смещение зоны для getLocalTime.
 *
 * Серверы: pool.ntp.org и time.google.com. tz_hours * 3600 — без DST.
 * time() на ESP-IDF после этого обычно остаётся UTC; его и шлём в epoch.
 * Вызывать при старте, после смены tz на вебе и раз в сутки из loop.
 *
 * @param tz_hours  как в форме «UTC+3», −12…+14
 */
void ntp_begin(int8_t tz_hours)
{
    s_tz = tz_hours;
    configTime((long)tz_hours * 3600L, 0, "pool.ntp.org", "time.google.com");
}

/**
 * ntp_ready — часы уже не «1970».
 *
 * До ответа NTP ESP отдаёт маленькую эпоху. Порог 1.7e9 ≈ 2023 год.
 */
bool ntp_ready()
{
    time_t now = time(nullptr);
    return now > 1700000000;
}

/**
 * ntp_epoch_utc — Unix UTC для поля epoch в JSON на STM32.
 *
 * @return 0, пока NTP не пришёл (STM32 нуль в DS3231 не пишет)
 */
uint32_t ntp_epoch_utc()
{
    time_t now = time(nullptr);
    if (now < 1700000000) {
        return 0;
    }
    return (uint32_t)now;
}

/**
 * ntp_hhmm — локальные часы ESP для отладки ("14:30" или "--:--").
 *
 * LCD берёт время из телеметрии STM32, не отсюда. Функция оставлена
 * на случай, если понадобится показать NTP без UART.
 */
String ntp_hhmm()
{
    struct tm t;
    if (!getLocalTime(&t, 10)) {
        return "--:--";
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    (void)s_tz;
    return String(buf);
}
