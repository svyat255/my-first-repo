#include "app_settings.h"
#include <Preferences.h>

/**
 * settings_load — прочитать namespace NVS "co2" (только чтение).
 *
 * Ключи короткие: warn, crit, qstart, qend, tz, tg_token, tg_chat, ntfy.
 * Нет ключа → число из второго аргумента get* (завод 800/1200/22/7/+3).
 * Если warn ≥ crit (битые данные) — crit = warn + 100, как на STM32.
 *
 * @param s  заполняется на месте
 */
void settings_load(AppSettings &s)
{
    Preferences p;
    p.begin("co2", true);
    s.co2_warn = p.getUShort("warn", 800);
    s.co2_crit = p.getUShort("crit", 1200);
    s.quiet_start = p.getUChar("qstart", 22);
    s.quiet_end = p.getUChar("qend", 7);
    s.tz_hours = (int8_t)p.getChar("tz", 3);
    s.telegram_token = p.getString("tg_token", "");
    s.telegram_chat = p.getString("tg_chat", "");
    s.ntfy_topic = p.getString("ntfy", "");
    p.end();
    if (s.co2_warn >= s.co2_crit) {
        s.co2_crit = s.co2_warn + 100;
    }
}

/**
 * settings_save — записать те же ключи (begin с false = запись).
 *
 * Токены Telegram живут только здесь; STM32 их не видит.
 * После save веб ещё шлёт пороги по UART — две копии намеренно.
 */
void settings_save(const AppSettings &s)
{
    Preferences p;
    p.begin("co2", false);
    p.putUShort("warn", s.co2_warn);
    p.putUShort("crit", s.co2_crit);
    p.putUChar("qstart", s.quiet_start);
    p.putUChar("qend", s.quiet_end);
    p.putChar("tz", s.tz_hours);
    p.putString("tg_token", s.telegram_token);
    p.putString("tg_chat", s.telegram_chat);
    p.putString("ntfy", s.ntfy_topic);
    p.end();
}
