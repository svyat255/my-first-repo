#ifndef ALERT_FSM_H
#define ALERT_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "app_settings.h"

/* Пороги CO2. Warmup внутренний; в JSON наружу он уходит как NORMAL (0). */

typedef enum {
    ALERT_WARMUP = -1, /* только внутри; телеметрия этого не шлёт */
    ALERT_NORMAL = 0,
    ALERT_WARNING = 1,
    ALERT_ALARM = 2
} alert_level_t;

/**
 * alert_fsm_init — WARMUP 60 с, mute и таймеры писков в ноль.
 * Вызывать один раз после старта. Ловушка: в JSON warmup отдаём как 0.
 */
void alert_fsm_init(void);

/**
 * alert_fsm_reset_warmup — снова 60 с без звука по порогам (после FRC).
 * Mute не трогает. Ловушка: вызывать из main сразу после scd41_frc_calibrate.
 */
void alert_fsm_reset_warmup(void);

/**
 * alert_fsm_update — шаг автомата по ppm с гистерезисом 50.
 * @param co2     свежие ppm
 * @param s       пороги warn/crit
 * @param now_ms  HAL_GetTick()
 * @return        0/1/2 для JSON (никогда не −1)
 * Ловушка: вниз из Warning/Alarm только если co2 + 50 < порог.
 */
alert_level_t alert_fsm_update(uint16_t co2, const app_settings_t *s, uint32_t now_ms);

/**
 * alert_fsm_set_mute — выключить пороговый звук на N минут.
 * @param now_ms   HAL_GetTick()
 * @param minutes  1…1440 (режет esp_link)
 * Ловушка: сравнение в muted() через int32_t — иначе overflow tick «залипнет».
 */
void alert_fsm_set_mute(uint32_t now_ms, uint32_t minutes);

/**
 * alert_fsm_unmute — снять mute сразу (кнопка Unmute на вебе).
 */
void alert_fsm_unmute(void);

/**
 * alert_fsm_muted — идёт ли окно mute.
 * @param now_ms  HAL_GetTick()
 * @return        true, пока now < дедлайна
 */
bool alert_fsm_muted(uint32_t now_ms);

/**
 * alert_fsm_sound_allowed — можно ли пищать по порогам.
 * @param s       нужен alerts_enabled
 * @param quiet   только ночь (без mute) из quiet_hours_active
 * @param now_ms  для mute
 * @return        true = process_sound имеет право ставить паттерн
 * Ловушка: веб-тест beep эту функцию не спрашивает.
 */
bool alert_fsm_sound_allowed(const app_settings_t *s, bool quiet, uint32_t now_ms);

/**
 * alert_fsm_process_sound — поставить Warning/Alarm паттерн, если разрешено.
 * @param level          уже 0/1/2 из update
 * @param sound_allowed  из sound_allowed()
 * @param now_ms         HAL_GetTick()
 * Ловушка: сам PWM не глушит — это buzzer_poll() в том же обороте.
 */
void alert_fsm_process_sound(alert_level_t level, bool sound_allowed, uint32_t now_ms);

#endif
