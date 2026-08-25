#ifndef APP_SETTINGS_ESP_H
#define APP_SETTINGS_ESP_H

#include <Arduino.h>

/* NVS namespace "co2". Пороги ещё пушатся на STM32 по UART. */

/**
 * AppSettings — копия настроек в RAM.
 * Пишут settings_load / POST /api/settings; читают UART-push, NTP, notify, GET формы.
 */
struct AppSettings {
    uint16_t co2_warn = 800;     /**< порог warning, ppm; NVS «warn», уходит на STM32 */
    uint16_t co2_crit = 1200;    /**< порог alarm, ppm; NVS «crit», уходит на STM32 */
    uint8_t quiet_start = 22;    /**< час начала тихих часов 0–23; NVS «qstart» */
    uint8_t quiet_end = 7;       /**< час конца тихих часов 0–23; NVS «qend» */
    int8_t tz_hours = 3;         /**< смещение UTC, часы; NVS «tz», поле tz в пакете настроек */
    String telegram_token;       /**< токен бота; только NVS, в лог и на STM32 не идёт */
    String telegram_chat;        /**< chat_id Telegram; только NVS */
    String ntfy_topic;           /**< топик ntfy.sh; только NVS */
};

/** settings_load — прочитать namespace NVS «co2» в s. */
void settings_load(AppSettings &s);
/** settings_save — записать s в NVS «co2». */
void settings_save(const AppSettings &s);

#endif
