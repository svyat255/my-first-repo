#ifndef QUIET_HOURS_H
#define QUIET_HOURS_H

#include <stdint.h>
#include <stdbool.h>
#include "app_settings.h"
#include "rtc_ds3231.h"

/* Тихие часы по часу DS3231. Нет валидных часов → false (день не глушится). */

/**
 * quiet_hours_active — сейчас ли окно «зуммер молчит» по часу DS3231.
 * @param s  quiet_start / quiet_end (0…23)
 * @param t  последний ds3231_read
 * @return   true → sound_allowed должен запретить пороговый писк
 * Ловушка: mute сюда не входит (его OR-ит main для JSON quiet). start==end → выкл.
 */
bool quiet_hours_active(const app_settings_t *s, const rtc_time_t *t);

#endif
