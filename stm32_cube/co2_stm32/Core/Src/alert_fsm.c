/*
 * Конечный автомат CO2: Warmup → Normal ↔ Warning ↔ Alarm.
 *
 * Входы: ppm и пороги из настроек. Выход для JSON: 0/1/2 (warmup прячется в 0).
 * Гистерезис 50 ppm: выйти из Warning можно только при CO2 < warn−50,
 * иначе на 800 ppm зуммер щёлкал бы каждые 5 секунд.
 * Звук здесь только «поставить паттерн»; PWM гасит buzzer_poll().
 */
#include "alert_fsm.h"
#include "app_config.h"
#include "buzzer.h"
#include "stm32f1xx_hal.h"

/** s_boot_ms — HAL_GetTick старта warmup. Пишут init/reset_warmup, читает update. */
static uint32_t s_boot_ms;
/** s_mute_until — 0 = mute выкл; иначе дедлайн в мс. Пишут set_mute/unmute, читает muted. */
static uint32_t s_mute_until;
/** s_last_warn_beep — HAL_GetTick последнего Warning-писка. Пишет/читает process_sound. */
static uint32_t s_last_warn_beep;
/** s_last_alarm_beep — HAL_GetTick последней Alarm-тройки. Пишет/читает process_sound. */
static uint32_t s_last_alarm_beep;
/** s_level — внутренний уровень, включая WARMUP. Пишет update, читают update и process_sound. */
static alert_level_t s_level;

/**
 * alert_fsm_init — сброс автомата при старте прошивки.
 *
 * Уровень WARMUP на 60 с: SCD41 ещё стабилизируется, в телеметрии alert=0,
 * звука по порогам нет. Mute и таймеры писков обнуляются.
 */
void alert_fsm_init(void)
{
    s_boot_ms = HAL_GetTick();
    s_mute_until = 0;
    s_last_warn_beep = 0;
    s_last_alarm_beep = 0;
    s_level = ALERT_WARMUP;
}

/**
 * alert_fsm_reset_warmup — начать warmup заново (после FRC).
 *
 * Калибровка сбивает шкалу на минуту: не орём по старым 1200 ppm сразу.
 * Mute не трогаем — пользователь мог его включить с веба.
 */
void alert_fsm_reset_warmup(void)
{
    s_boot_ms = HAL_GetTick();
    s_level = ALERT_WARMUP;
}

/**
 * alert_fsm_update — один шаг по ppm: сменить уровень с учётом гистерезиса.
 *
 * Пока не истекли WARMUP_MS, внутри WARMUP, наружу всегда NORMAL (0).
 * Из Normal/Warmup: ≥crit → Alarm, ≥warn → Warning, иначе Normal.
 * Из Warning: ≥crit → Alarm; вниз только если co2 + 50 < warn.
 * Из Alarm: вниз если co2 + 50 < crit, затем Warning или Normal по warn.
 *
 * @param co2     свежие ppm с SCD41
 * @param s       пороги co2_warn / co2_crit (могут прийти с ESP32)
 * @param now_ms  HAL_GetTick()
 * @return        0/1/2 для JSON и LCD (никогда не отдаём −1)
 */
alert_level_t alert_fsm_update(uint16_t co2, const app_settings_t *s, uint32_t now_ms)
{
    uint16_t warn = s->co2_warn;
    uint16_t crit = s->co2_crit;
    if ((now_ms - s_boot_ms) < WARMUP_MS) {
        s_level = ALERT_WARMUP;
        return ALERT_NORMAL;
    }
    if (s_level == ALERT_WARMUP || s_level == ALERT_NORMAL) {
        if (co2 >= crit) {
            s_level = ALERT_ALARM;
        } else if (co2 >= warn) {
            s_level = ALERT_WARNING;
        } else {
            s_level = ALERT_NORMAL;
        }
    } else if (s_level == ALERT_WARNING) {
        if (co2 >= crit) {
            s_level = ALERT_ALARM;
        } else if (co2 + HYSTERESIS_PPM < warn) {
            s_level = ALERT_NORMAL;
        }
    } else {
        if (co2 + HYSTERESIS_PPM < crit) {
            s_level = (co2 >= warn) ? ALERT_WARNING : ALERT_NORMAL;
        }
    }
    return (s_level == ALERT_WARMUP) ? ALERT_NORMAL : s_level;
}

/**
 * alert_fsm_set_mute — выключить звук на N минут с текущего now_ms.
 *
 * Дедлайн = now + minutes*60000. Сравнение в muted() через int32_t,
 * чтобы переполнение HAL_GetTick (~49 суток) не «залипло» mute навсегда.
 * Потолок минут режет esp_link (1…1440). Тест beep mute обходит.
 *
 * @param now_ms   HAL_GetTick()
 * @param minutes  длительность
 */
void alert_fsm_set_mute(uint32_t now_ms, uint32_t minutes)
{
    s_mute_until = now_ms + minutes * 60000u;
}

/**
 * alert_fsm_unmute — снять mute сразу (кнопка Unmute на вебе).
 */
void alert_fsm_unmute(void)
{
    s_mute_until = 0;
}

/**
 * alert_fsm_muted — идёт ли ещё окно mute.
 *
 * @param now_ms  HAL_GetTick()
 * @return        true, пока now < s_mute_until
 */
bool alert_fsm_muted(uint32_t now_ms)
{
    return s_mute_until != 0u && (int32_t)(now_ms - s_mute_until) < 0;
}

/**
 * alert_fsm_sound_allowed — можно ли вообще пищать по порогам.
 *
 * Три независимых запрета: тумблер alerts_enabled, тихие часы, mute.
 * Тест с HTML идёт в buzzer_test_beep напрямую и эту функцию не спрашивает.
 *
 * @param s       настройки (нужен alerts_enabled)
 * @param quiet   результат quiet_hours_active (только ночь, без mute)
 * @param now_ms  для проверки mute
 * @return        true = alert_fsm_process_sound имеет право ставить паттерн
 */
bool alert_fsm_sound_allowed(const app_settings_t *s, bool quiet, uint32_t now_ms)
{
    if (!s->alerts_enabled) {
        return false;
    }
    if (quiet) {
        return false;
    }
    if (alert_fsm_muted(now_ms)) {
        return false;
    }
    return true;
}

/**
 * alert_fsm_process_sound — если уровень плохой и звук разрешён, запустить писк.
 *
 * Warning: один импульс 200 мс, не чаще WARN_BEEP_PERIOD_MS (5 мин).
 * Alarm: тройка импульсов, не чаще ALARM_BEEP_PERIOD_MS (30 с).
 * Сами импульсы доигрывает buzzer_poll() в том же обороте main.
 * Normal/Warmup — сразу выход, PWM здесь не глушим (это делает poll).
 *
 * @param level          текущий уровень (уже 0/1/2 из update)
 * @param sound_allowed  из sound_allowed()
 * @param now_ms         HAL_GetTick()
 */
void alert_fsm_process_sound(alert_level_t level, bool sound_allowed, uint32_t now_ms)
{
    if (!sound_allowed || level == ALERT_NORMAL || level == ALERT_WARMUP) {
        return;
    }
    if (level == ALERT_WARNING) {
        if (s_last_warn_beep == 0u || (now_ms - s_last_warn_beep) >= WARN_BEEP_PERIOD_MS) {
            buzzer_pattern_warning();
            s_last_warn_beep = now_ms;
        }
    } else if (level == ALERT_ALARM) {
        if (s_last_alarm_beep == 0u || (now_ms - s_last_alarm_beep) >= ALARM_BEEP_PERIOD_MS) {
            buzzer_pattern_alarm();
            s_last_alarm_beep = now_ms;
        }
    }
}
