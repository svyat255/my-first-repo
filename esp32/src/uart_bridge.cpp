#include "uart_bridge.h"
#include "config.h"

/** stm32Uart2 — аппаратный UART2, RX=GPIO16 TX=GPIO17 как в config.h. Пишет begin, читает poll. */
static HardwareSerial stm32Uart2(2);
/** stm32Uart1 — запасной UART1 GPIO25/26 (WROVER: 16/17 = PSRAM). Пишет begin, читает poll. */
static HardwareSerial stm32Uart1(1);
/** s_line_a — незавершённая строка с UART2; пишет feed_byte, сбрасывает на '\\n'. */
static String s_line_a;
/** s_line_b — незавершённая строка с UART1 GPIO25; пишет feed_byte, сбрасывает на '\\n'. */
static String s_line_b;
/** s_last_raw — последняя закрытая строка (телеметрия, boot или мусор); пишет feed_byte, читает веб. */
static String s_last_raw;
/** s_rx_gpio16 — счётчик байт UART2 с begin; пишет feed_byte, читают статус/петля. */
static uint32_t s_rx_gpio16;
/** s_rx_gpio25 — счётчик байт UART1 GPIO25 с begin; пишет feed_byte, читают статус/петля. */
static uint32_t s_rx_gpio25;
/** s_rx_lines — число строк, закрытых '\\n'; пишет feed_byte, читает /api/status. */
static uint32_t s_rx_lines;
/** s_boot_seen — встречалась подстрока «STM32 boot»; пишет feed_byte, читает веб. */
static bool s_boot_seen;

/**
 * parse_telemetry — разобрать JSON STM32 в структуру Telemetry.
 *
 * Обязателен ключ "co2", иначе это не телеметрия (например «STM32 boot» или {"ping":1}).
 * Остальные поля опциональны. "quiet":true ищем подстрокой.
 * При успехе: rx_ms=millis(), valid=true — отсчёт stale начинается отсюда.
 * Нет ключа "co2" → t не трогаем (живая телеметрия не затирается ping/boot).
 *
 * @param line  одна строка без '\\n'
 * @param t     перезаписывается на месте только при успехе
 * @return      false → t не трогаем
 */
static bool parse_telemetry(const String &line, Telemetry &t)
{
    int i;
    i = line.indexOf("\"co2\":");
    if (i < 0) {
        return false;
    }
    t.co2 = (uint16_t)line.substring(i + 6).toInt();
    i = line.indexOf("\"temp\":");
    if (i >= 0) {
        t.temp = line.substring(i + 7).toFloat();
    }
    i = line.indexOf("\"rh\":");
    if (i >= 0) {
        t.rh = (uint8_t)line.substring(i + 5).toInt();
    }
    i = line.indexOf("\"alert\":");
    if (i >= 0) {
        t.alert = line.substring(i + 8).toInt();
    }
    t.quiet = line.indexOf("\"quiet\":true") >= 0;
    i = line.indexOf("\"time\":\"");
    if (i >= 0) {
        int e = line.indexOf('"', i + 8);
        if (e > i) {
            t.time = line.substring(i + 8, e);
        }
    }
    t.rx_ms = millis();
    t.valid = true;
    return true;
}

/**
 * feed_byte — один байт с выбранного UART в свой буфер строки.
 *
 * Два порта нельзя мешать в одну строку: иначе GPIO16 и GPIO25 склеят мусор.
 *
 * @param c     принятый символ
 * @param line  буфер этой линии
 * @param rx    счётчик байт этой линии
 * @param t     телеметрия, если строка оказалась JSON с co2
 */
static void feed_byte(char c, String &line, uint32_t *rx, Telemetry &t)
{
    (*rx)++;
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        s_rx_lines++;
        s_last_raw = line;
        if (line.indexOf("STM32 boot") >= 0) {
            s_boot_seen = true;
        }
        Serial.print(F("[STM32 UART] "));
        Serial.println(line);
        parse_telemetry(line, t);
        line = "";
    } else if (line.length() < 255) {
        line += c;
    } else {
        line = "";
    }
}

/**
 * uart_bridge_begin — открыть сразу два аппаратных UART на STM32.
 *
 * UART2: GPIO16 RX / GPIO17 TX — как в схеме. На ESP32-WROVER эти ноги
 * сидят на PSRAM: байт не будет, хотя провод целый.
 * UART1: GPIO25 RX / GPIO26 TX — запас. Команды уходят в оба TX.
 * RX/TX UART2 не свапаются сами: правильная проводка PA9→GPIO16 после
 * длинного портала Wi‑Fi должна остаться рабочей.
 */
void uart_bridge_begin()
{
    stm32Uart2.begin(STM32_UART_BAUD, SERIAL_8N1, STM32_UART_RX, STM32_UART_TX);
    stm32Uart1.begin(STM32_UART_BAUD, SERIAL_8N1, STM32_UART_RX_B, STM32_UART_TX_B);
    s_line_a.reserve(256);
    s_line_b.reserve(256);
}

/**
 * uart_bridge_poll — вычитать UART2 (GPIO16/17) и UART1 (GPIO25/26) в t.
 *
 * Оба RX слушаем параллельно. Пины UART2 всегда как в config.h, без авто-swap.
 */
void uart_bridge_poll(Telemetry &t)
{
    while (stm32Uart2.available()) {
        feed_byte((char)stm32Uart2.read(), s_line_a, &s_rx_gpio16, t);
    }
    while (stm32Uart1.available()) {
        feed_byte((char)stm32Uart1.read(), s_line_b, &s_rx_gpio25, t);
    }
}

/**
 * uart_bridge_send_cmd — одна JSON-строка на STM32 + CRLF, сразу в оба TX.
 *
 * Неизвестно, какой провод воткнут: 17 или 26. Лишний TX в воздухе безвреден.
 */
void uart_bridge_send_cmd(const String &json_line)
{
    stm32Uart2.println(json_line);
    stm32Uart1.println(json_line);
}

/**
 * uart_bridge_send_settings — пакет без "cmd": пороги, quiet, tz, опционально epoch.
 *
 * STM32 воспринимает это как apply_settings. epoch добавляем только если NTP
 * уже дал Unix > 1.7e9, иначе DS3231 не затрём нулём.
 */
void uart_bridge_send_settings(const AppSettings &s, uint32_t epoch)
{
    String line = "{\"co2_warn\":";
    line += s.co2_warn;
    line += ",\"co2_crit\":";
    line += s.co2_crit;
    line += ",\"quiet_start\":";
    line += s.quiet_start;
    line += ",\"quiet_end\":";
    line += s.quiet_end;
    line += ",\"tz\":";
    line += s.tz_hours;
    if (epoch > 1700000000UL) {
        line += ",\"epoch\":";
        line += epoch;
    }
    line += "}";
    uart_bridge_send_cmd(line);
}

/**
 * uart_bridge_stale — телеметрия слишком старая для LCD/веба.
 *
 * @return true, если пакетов не было или старше TELEMETRY_STALE_MS (10 с)
 */
bool uart_bridge_stale(const Telemetry &t)
{
    if (!t.valid) {
        return true;
    }
    return (millis() - t.rx_ms) > TELEMETRY_STALE_MS;
}

/** uart_bridge_rx_bytes — сумма байт обоих портов; 0 = STM32 молчит или не те ноги. */
uint32_t uart_bridge_rx_bytes()
{
    return s_rx_gpio16 + s_rx_gpio25;
}

/** uart_bridge_rx_gpio16 — байты с UART2 (RX = GPIO16 из config.h). */
uint32_t uart_bridge_rx_gpio16()
{
    return s_rx_gpio16;
}

/** uart_bridge_rx_gpio25 — байты с запасного UART1 (GPIO25). На WROVER смотрите сюда. */
uint32_t uart_bridge_rx_gpio25()
{
    return s_rx_gpio25;
}

/** uart_bridge_rx_lines — сколько строк закрыто '\\n' с обоих UART. */
uint32_t uart_bridge_rx_lines()
{
    return s_rx_lines;
}

/** uart_bridge_boot_seen — STM32 хотя бы раз прислал текстовый boot, не обязательно JSON. */
bool uart_bridge_boot_seen()
{
    return s_boot_seen;
}

/**
 * uart_bridge_uart2_swapped — авто-swap по millis()>10 с убран.
 *
 * Раньше после длинного WiFiManager «тишина 10 с» ломала верную проводку PA9→GPIO16.
 * GPIO16/17 остаются как в config.h; запас GPIO25/26 слушается параллельно.
 */
bool uart_bridge_uart2_swapped()
{
    return false;
}

/** uart_bridge_last_raw — последняя строка UART для отладки на странице. */
String uart_bridge_last_raw()
{
    return s_last_raw;
}

/**
 * uart_bridge_loopback_probe — петля ESP32: {"ping":1} в оба TX, прирост байт на RX.
 *
 * Без поля "co2" парсер телеметрии строку игнорирует — живой JSON STM32 не затирается.
 * STM32 пакет без cmd и без порогов тоже игнорирует (apply_settings не dirty).
 * Не сырой LOOP: это не JSON и засоряло бы лог как «не команда».
 * poll в тот же Telemetry, что main и web_begin (не dummy).
 *
 * Замкните GPIO16–GPIO17 или GPIO25–GPIO26 на секунду и нажмите кнопку на странице.
 *
 * @param t        живая телеметрия main
 * @param delta16  прирост UART2
 * @param delta25  прирост UART1
 */
void uart_bridge_loopback_probe(Telemetry &t, uint32_t *delta16, uint32_t *delta25)
{
    uint32_t a0 = s_rx_gpio16;
    uint32_t b0 = s_rx_gpio25;
    uart_bridge_send_cmd("{\"ping\":1}");
    delay(50);
    uart_bridge_poll(t);
    *delta16 = s_rx_gpio16 - a0;
    *delta25 = s_rx_gpio25 - b0;
}
