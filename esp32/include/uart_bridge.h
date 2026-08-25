#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <Arduino.h>
#include "app_settings.h"

/**
 * Telemetry — последняя разобранная телеметрия STM32 (JSON с полем "co2").
 * Пишет parse_telemetry через uart_bridge_poll; читают LCD, веб, notify.
 */
struct Telemetry {
    uint16_t co2 = 0;       /**< ppm CO2 из последнего валидного JSON; пишет parse_telemetry, читают LCD/веб/notify */
    float temp = 0;         /**< температура °C; пишет parse_telemetry, читают LCD/веб */
    uint8_t rh = 0;         /**< относительная влажность %; пишет parse_telemetry, читают LCD/веб */
    int alert = 0;          /**< 0 OK, 1 WARN, 2 ALARM; пишет parse_telemetry, читают LCD/веб/notify */
    bool quiet = false;     /**< тихие часы или mute на STM32; пишет parse_telemetry, читает веб */
    String time = "--:--";  /**< HH:MM с DS3231; пишет parse_telemetry, читают LCD/веб */
    uint32_t rx_ms = 0;     /**< millis() момента последнего валидного JSON; пишет parse_telemetry, читает stale */
    bool valid = false;     /**< true после первого удачного JSON с "co2"; пишет parse_telemetry, читают все потребители */
};

/** uart_bridge_begin — открыть UART2 (GPIO16/17) и запасной UART1 (GPIO25/26). */
void uart_bridge_begin();
/** uart_bridge_poll — вычитать оба UART в t; GPIO16/17 как в config.h, GPIO25/26 параллельно. */
void uart_bridge_poll(Telemetry &t);
/** uart_bridge_send_settings — JSON порогов/quiet/tz и опционально epoch, без поля cmd. */
void uart_bridge_send_settings(const AppSettings &s, uint32_t epoch);
/** uart_bridge_send_cmd — одна JSON-строка + перевод строки в оба TX. */
void uart_bridge_send_cmd(const String &json_line);
/** uart_bridge_stale — нет валидного JSON или старше TELEMETRY_STALE_MS. */
bool uart_bridge_stale(const Telemetry &t);
/** uart_bridge_rx_bytes — сумма байт GPIO16/UART2 и GPIO25/UART1 с момента begin. */
uint32_t uart_bridge_rx_bytes();
/** uart_bridge_rx_gpio16 — байты, принятые UART2 (нога RX из config.h = GPIO16). */
uint32_t uart_bridge_rx_gpio16();
/** uart_bridge_rx_gpio25 — байты, принятые запасным UART1 (GPIO25). */
uint32_t uart_bridge_rx_gpio25();
/** uart_bridge_rx_lines — число строк, закрытых '\\n', с обоих портов. */
uint32_t uart_bridge_rx_lines();
/** uart_bridge_boot_seen — true, если на линии встречалась подстрока «STM32 boot». */
bool uart_bridge_boot_seen();
/** uart_bridge_uart2_swapped — всегда false: авто-swap RX/TX убран, пины как в config.h. */
bool uart_bridge_uart2_swapped();
/** uart_bridge_last_raw — последняя строка до '\\n' (может быть не JSON); для /api/status. */
String uart_bridge_last_raw();
/**
 * uart_bridge_loopback_probe — послать {"ping":1} в оба TX и посчитать прирост байт.
 * poll пишет в тот же Telemetry, что main/веб; живую телеметрию dummy не затирает.
 */
void uart_bridge_loopback_probe(Telemetry &t, uint32_t *delta16, uint32_t *delta25);

#endif
