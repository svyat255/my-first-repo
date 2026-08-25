#include "quiet_hours.h"

/**
 * quiet_hours_active — сейчас ли окно «зуммер молчит» по часам DS3231.
 *
 * Смотрим только hour, минуты отброшены: в 22:00 уже тихо, в 07:00 уже нет.
 * Часы невалидны → false: лучше днём пищать, чем заглушить всё при мёртвом RTC.
 * start == end → функция выключена (с веба можно так «снять» ночь).
 * start > end → через полночь (22→7): hour≥22 ИЛИ hour<7.
 * Иначе обычный интервал в пределах суток (например 13→15).
 *
 * Mute сюда не входит: его OR-ит main отдельно для поля quiet в JSON.
 *
 * @param s  quiet_start / quiet_end (0…23)
 * @param t  последний ds3231_read
 * @return   true → sound_allowed должен запретить пороговый писк
 */
bool quiet_hours_active(const app_settings_t *s, const rtc_time_t *t)
{
    uint8_t h;
    if (!t->valid) {
        return false;
    }
    h = t->hour;
    if (s->quiet_start == s->quiet_end) {
        return false;
    }
    if (s->quiet_start > s->quiet_end) {
        return (h >= s->quiet_start) || (h < s->quiet_end);
    }
    return (h >= s->quiet_start) && (h < s->quiet_end);
}
