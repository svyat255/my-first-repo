#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Заводские константы прошивки STM32.
 * Живые пороги после первого сохранения лежат во flash (app_settings)
 * и приходят с ESP32 по UART. Этот файл — «что считать нормой с завода».
 */

/* 7-битные адреса I2C. HAL хочет 8 бит: в драйверах делается << 1. */
#define SCD41_I2C_ADDR          0x62u   /* датчик CO2 на I2C1 PB6/PB7 */
#define DS3231_I2C_ADDR         0x68u   /* часы на I2C2 PB10/PB11 */

#define DEFAULT_CO2_WARN        800u    /* ppm: вход в Warning */
#define DEFAULT_CO2_CRIT        1200u   /* ppm: вход в Alarm */
#define HYSTERESIS_PPM          50u     /* выход: CO2 < порог−50, чтобы не дребезжать */
#define WARMUP_MS               60000u  /* после старта и FRC: звука по порогам нет */
#define MEASURE_PERIOD_MS       5000u   /* как часто спрашиваем SCD41 */
#define TELEMETRY_PERIOD_MS     5000u   /* JSON после 20 с; первые 20 с main шлёт раз в 1 с */
#define ESP_TIMEOUT_MS          120000u /* «ESP32 молчит» (esp_link_esp_alive) */

#define DEFAULT_QUIET_START     22u     /* час начала тихих часов */
#define DEFAULT_QUIET_END       7u      /* час конца; 22→7 через полночь */
#define DEFAULT_TZ_HOURS        3       /* UTC+3 при записи epoch в DS3231 */

#define WARN_BEEP_PERIOD_MS     300000u /* 1 писк Warning не чаще чем раз в 5 мин */
#define ALARM_BEEP_PERIOD_MS    30000u  /* тройка Alarm не чаще чем раз в 30 с */
#define BEEP_PULSE_MS           200u    /* длина одного писка */
#define TEST_BEEP_MS            1000u   /* кнопка «Тест звука» на вебе */

/* Последняя 1 КБ страница F103C8. Линкер FLASH = 63K, код сюда не залезет. */
#define SETTINGS_FLASH_ADDR     0x0800FC00u
#define SETTINGS_MAGIC          0xC0C0A11Au /* если не совпало — заводские умолчания */

#define UART_LINE_MAX           256     /* одна JSON-строка + '\0' */

#endif
