#ifndef ESP_LINK_H
#define ESP_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "app_settings.h"
#include "alert_fsm.h"
#include "rtc_ds3231.h"

/* JSON по USART1. IRQ только копит строку; разбор — esp_link_poll() в main. */

/**
 * esp_link_init — обнулить буферы и заказать первый байт USART1 в IRQ.
 * Вызывать один раз после MX_USART1_UART_Init. Ловушка: без Receive_IT RX мёртв.
 */
void esp_link_init(void);

/**
 * esp_link_rx_byte — сложить байт в строку (из RxCpltCallback).
 * @param b  принятый байт. Ловушка: не парсить JSON в IRQ.
 */
void esp_link_rx_byte(uint8_t b);

/**
 * esp_link_poll — забрать одну готовую строку и разобрать в main.
 * @param s  настройки (могут уйти во flash). Ловушка: I2C/flash только отсюда, не из IRQ.
 */
void esp_link_poll(app_settings_t *s);

/**
 * esp_link_send_hello — «STM32 boot» записью DR, полный дуплекс с RX IT.
 * Повторять из main первые секунды. Ловушка: не HAL_UART_Transmit при живом Receive_IT.
 */
void esp_link_send_hello(void);

/**
 * esp_link_send_telemetry — JSON с ppm/T/RH/alert/quiet/time и '\n'.
 * @param co2       последние ppm (0, если SCD41 ещё молчит)
 * @param temp_x10  десятые °C
 * @param rh        %
 * @param alert     0/1/2
 * @param quiet     ночь или mute
 * @param t         снимок DS3231
 * Ловушка: не слать, если snprintf не влез в буфер.
 */
void esp_link_send_telemetry(uint16_t co2, int16_t temp_x10, uint8_t rh,
                             alert_level_t alert, bool quiet, const rtc_time_t *t);

/**
 * esp_link_esp_alive — были ли пакеты от ESP32 за ESP_TIMEOUT_MS.
 * @param now_ms  HAL_GetTick(). @return false, если тишина или ещё не было ни одного.
 */
bool esp_link_esp_alive(uint32_t now_ms);

/**
 * esp_link_take_beep — снять флаг {"cmd":"beep"}.
 * @return true один раз; дальше main зовёт buzzer_test_beep. Ловушка: не из IRQ.
 */
bool esp_link_take_beep(void);

/**
 * esp_link_take_frc — снять флаг frc_calibrate.
 * @return true один раз; main зовёт scd41_frc_calibrate(400). Ловушка: FRC ~1 с.
 */
bool esp_link_take_frc(void);

#endif
