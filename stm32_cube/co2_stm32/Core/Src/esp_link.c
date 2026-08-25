/*
 * USART1 JSON с ESP32 (PA9 TX / PA10 RX, 115200).
 *
 * Приём: HAL в IRQ кладёт по байту в s_line; по '\n' копия в s_pending.
 * Разбор JSON — только esp_link_poll() из main: I2C/flash/Delay в IRQ нельзя.
 * Отдача: запись USART1->DR по TXE (не HAL_UART_Transmit — он ломает Receive_IT).
 */
#include "esp_link.h"
#include "app_config.h"
#include "rtc_ds3231.h"
#include "alert_fsm.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** s_line — собираемая в IRQ строка до '\n'. Пишет esp_link_rx_byte, копирует в s_pending. */
static char s_line[UART_LINE_MAX];
/** s_len — сколько байт уже в s_line. Пишет/читает только IRQ (esp_link_rx_byte). */
static uint16_t s_len;
/** s_ready — 1, когда s_pending можно забирать. Пишет IRQ, читает/сбрасывает esp_link_poll. */
static volatile uint8_t s_ready;
/** s_pending — готовая строка для main. Пишет IRQ, копирует poll в локальный буфер. */
static char s_pending[UART_LINE_MAX];
/** s_rx_byte — слот HAL_UART_Receive_IT на 1 байт. Пишет HAL в IRQ, читает RxCpltCallback. */
static uint8_t s_rx_byte;
/** s_last_rx_ms — HAL_GetTick последней разобранной строки. Пишет poll, читает esp_link_esp_alive. */
static uint32_t s_last_rx_ms;
/** s_want_beep — веб-тест звука. Пишет handle_cmd, снимает esp_link_take_beep в main. */
static bool s_want_beep;
/** s_want_frc — калибровка SCD41. Пишет handle_cmd, снимает esp_link_take_frc в main. */
static bool s_want_frc;

/**
 * uart1_wait_sr — ждать флаг USART1 SR (TXE или TC).
 *
 * @param flag        USART_SR_TXE или USART_SR_TC
 * @param timeout_ms  потолок ожидания по HAL_GetTick
 * @return            true, если флаг встал
 *
 * Ловушка: не вызывать из IRQ — крутит SysTick. TXE ≠ TC: после последнего
 * DR нужен TC, иначе кадр ещё в сдвиговом регистре.
 */
static bool uart1_wait_sr(uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((huart1.Instance->SR & flag) == 0U) {
        if ((HAL_GetTick() - start) > timeout_ms) {
            return false;
        }
    }
    return true;
}

/**
 * uart1_putc — один байт в USART1->DR, когда TXE=1.
 *
 * @param b  байт кадра
 * @return   true, если записали DR
 *
 * Ловушка: не трогает huart1.gState — иначе живой Receive_IT даст HAL_BUSY.
 */
static bool uart1_putc(uint8_t b)
{
    if (!uart1_wait_sr(USART_SR_TXE, 20U)) {
        return false;
    }
    huart1.Instance->DR = b;
    return true;
}

/**
 * uart1_write — отдать целый кадр записью DR, не через HAL_UART_Transmit.
 *
 * HAL_UART_Transmit после Receive_IT часто возвращает HAL_BUSY: TX и RX
 * делят handle. Тогда JSON молчит, хотя приём в IRQ жив.
 *
 * @param data  ASCII, не обязан быть NUL-terminated
 * @param n     длина; для телеметрии кадр должен уже содержать '\n'
 *
 * Ловушка: при таймауте TXE не оставляем ESP32 без '\n' — парсер вечно
 * ждёт конец строки. Если ещё ничего не ушло — не начинаем обрубок.
 */
static void uart1_write(const uint8_t *data, uint16_t n)
{
    uint16_t i;
    uint16_t sent = 0;

    if (data == NULL || n == 0u) {
        return;
    }

    for (i = 0; i < n; i++) {
        if (!uart1_putc(data[i])) {
            if (sent > 0u) {
                (void)uart1_putc((uint8_t)'\n');
                (void)uart1_wait_sr(USART_SR_TC, 20U);
            }
            return;
        }
        sent++;
    }
    (void)uart1_wait_sr(USART_SR_TC, 20U);
}

/**
 * esp_link_init — обнулить буферы и запустить приём первого байта в IRQ.
 *
 * HAL_UART_Receive_IT на 1 байт: каждый принятый байт → RxCpltCallback.
 *
 * Ловушка: без повторного Receive_IT после колбэка приём останавливается навсегда.
 */
void esp_link_init(void)
{
    s_len = 0;
    s_ready = 0;
    s_last_rx_ms = 0;
    s_want_beep = false;
    s_want_frc = false;
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

/**
 * esp_link_send_hello — короткая строка, чтобы ESP32 увидел хоть какие-то байты.
 *
 * Не JSON и не телеметрия. Повторять из main первые секунды после reset.
 *
 * Ловушка: писать через uart1_write, не HAL_UART_Transmit — Receive_IT уже жив.
 */
void esp_link_send_hello(void)
{
    const char boot[] = "STM32 boot\r\n";
    uart1_write((const uint8_t *)boot, (uint16_t)(sizeof(boot) - 1u));
}

/**
 * HAL_UART_RxCpltCallback — слабый колбэк HAL: байт уже в s_rx_byte.
 *
 * Только USART1: сложить байт в строку и снова заказать 1 байт.
 *
 * @param huart  какой UART доложил (нас интересует только USART1)
 *
 * Ловушка: не парсить JSON здесь — это IRQ, strstr/flash/Delay недопустимы.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        esp_link_rx_byte(s_rx_byte);
        HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    }
}

/**
 * HAL_UART_ErrorCallback — overrun/framing на линии.
 *
 * Чистит ORE и перезапускает Receive_IT. Если HAL ещё BUSY — сброс RxState и ещё раз.
 *
 * @param huart  какой UART (только USART1)
 *
 * Ловушка: никаких I2C / HAL_Delay / flash — это IRQ.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        if (HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1) != HAL_OK) {
            huart1.RxState = HAL_UART_STATE_READY;
            huart1.ErrorCode = HAL_UART_ERROR_NONE;
            __HAL_UNLOCK(&huart1);
            (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
        }
    }
}

/**
 * esp_link_rx_byte — собрать одну JSON-строку из потока байт (вызывается из IRQ).
 *
 * '\r' игнор (ESP println даёт \r\n). '\n' = конец: если предыдущую строку
 * main ещё не забрал (s_ready=1), новая теряется — один слот намеренно.
 *
 * @param b  очередной байт USART1
 *
 * Ловушка: длиннее UART_LINE_MAX−1 — сброс, ждём следующий перевод строки.
 */
void esp_link_rx_byte(uint8_t b)
{
    if (b == '\r') {
        return;
    }
    if (b == '\n') {
        if (s_len > 0u && !s_ready) {
            s_line[s_len] = '\0';
            memcpy(s_pending, s_line, s_len + 1u);
            s_ready = 1;
        }
        s_len = 0;
        return;
    }
    if (s_len + 1u < UART_LINE_MAX) {
        s_line[s_len++] = (char)b;
    } else {
        s_len = 0;
    }
}

/**
 * json_get_int — вытащить целое после "ключ": из кустарного JSON.
 *
 * Не RFC: пробелы после двоеточия допустимы, кавычек у числа нет.
 *
 * @param j    вся строка пакета
 * @param key  имя поля без кавычек
 * @param out  результат
 * @return     true, если ключ найден и strtol съел хотя бы одну цифру
 *
 * Ловушка: не умеет экспоненту и дробь — для co2_warn / epoch / minutes хватает.
 */
static bool json_get_int(const char *j, const char *key, int *out)
{
    char pat[32];
    const char *p;
    char *end;
    long v;
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(j, pat);
    if (p == NULL) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ') {
        p++;
    }
    v = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = (int)v;
    return true;
}

/**
 * json_get_bool — то же для true/false без кавычек.
 *
 * @param j    пакет
 * @param key  имя поля
 * @param out  результат
 * @return     true только если после ключа буквально true или false
 *
 * Ловушка: "true" в кавычках не распознаем — веб шлёт без кавычек.
 */
static bool json_get_bool(const char *j, const char *key, bool *out)
{
    char pat[32];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(j, pat);
    if (p == NULL) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ') {
        p++;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

/**
 * json_get_cmd — строка команды из "cmd":"beep".
 *
 * Ищем ровно шаблон с кавычкой после двоеточия (как шлёт веб).
 *
 * @param j    пакет
 * @param out  буфер имени команды
 * @param n    размер out
 * @return     true, если имя непустое
 *
 * Ловушка: пробел между : и " не поддерживается.
 */
static bool json_get_cmd(const char *j, char *out, uint8_t n)
{
    const char *p = strstr(j, "\"cmd\":\"");
    uint8_t i = 0;
    if (p == NULL) {
        return false;
    }
    p += 7;
    while (*p != '\0' && *p != '"' && i + 1u < n) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0u;
}

/**
 * apply_settings — пакет без "cmd": пороги, тихие часы, tz, epoch, alerts.
 *
 * Каждое поле опционально. Диапазоны режут мусор с веба.
 * Если warn ≥ crit — crit поднимаем на +100.
 *
 * @param j  JSON-строка (локальная копия из poll)
 * @param s  живые настройки main (RAM, и при реальном изменении — flash)
 *
 * Ловушка: epoch только в DS3231. settings_save лишь если warn/crit/quiet/tz/alerts
 * реально изменились — иначе erase страницы раз в минуту убьёт flash.
 */
static void apply_settings(const char *j, app_settings_t *s)
{
    int v;
    bool b;
    bool dirty = false;
    if (json_get_int(j, "co2_warn", &v) && v >= 400 && v <= 4000) {
        if (s->co2_warn != (uint16_t)v) {
            s->co2_warn = (uint16_t)v;
            dirty = true;
        }
    }
    if (json_get_int(j, "co2_crit", &v) && v >= 500 && v <= 5000) {
        if (s->co2_crit != (uint16_t)v) {
            s->co2_crit = (uint16_t)v;
            dirty = true;
        }
    }
    if (s->co2_warn >= s->co2_crit) {
        uint16_t raised = (uint16_t)(s->co2_warn + 100u);
        if (s->co2_crit != raised) {
            s->co2_crit = raised;
            dirty = true;
        }
    }
    if (json_get_int(j, "quiet_start", &v) && v >= 0 && v <= 23) {
        if (s->quiet_start != (uint8_t)v) {
            s->quiet_start = (uint8_t)v;
            dirty = true;
        }
    }
    if (json_get_int(j, "quiet_end", &v) && v >= 0 && v <= 23) {
        if (s->quiet_end != (uint8_t)v) {
            s->quiet_end = (uint8_t)v;
            dirty = true;
        }
    }
    if (json_get_int(j, "tz", &v) && v >= -12 && v <= 14) {
        if (s->tz_hours != (int8_t)v) {
            s->tz_hours = (int8_t)v;
            dirty = true;
        }
    }
    if (json_get_int(j, "epoch", &v) && v > 1700000000) {
        (void)ds3231_set_epoch_utc((uint32_t)v, s->tz_hours);
    }
    if (json_get_bool(j, "alerts_enabled", &b)) {
        if (s->alerts_enabled != b) {
            s->alerts_enabled = b;
            dirty = true;
        }
    }
    if (dirty) {
        settings_save(s);
    }
}

/**
 * handle_cmd — пакет с "cmd": мгновенные действия с веба.
 *
 * beep/frc только ставят флаг: main снимет его take_* и сделает работу
 * с Delay/I2C. mute 1…1440 мин. set_time — отдельная запись DS3231.
 *
 * @param j    весь JSON (для minutes / enabled / epoch)
 * @param cmd  уже вытащенное имя
 * @param s    настройки (set_alerts пишет flash)
 *
 * Ловушка: неизвестная cmd тихо игнорируется; FRC/beep не делать здесь.
 */
static void handle_cmd(const char *j, const char *cmd, app_settings_t *s)
{
    int v;
    bool b;
    if (strcmp(cmd, "beep") == 0) {
        s_want_beep = true;
    } else if (strcmp(cmd, "mute") == 0) {
        v = 60;
        (void)json_get_int(j, "minutes", &v);
        if (v < 1) {
            v = 1;
        }
        if (v > 24 * 60) {
            v = 24 * 60;
        }
        alert_fsm_set_mute(HAL_GetTick(), (uint32_t)v);
    } else if (strcmp(cmd, "unmute") == 0) {
        alert_fsm_unmute();
    } else if (strcmp(cmd, "set_alerts") == 0) {
        if (json_get_bool(j, "enabled", &b) && s->alerts_enabled != b) {
            s->alerts_enabled = b;
            settings_save(s);
        }
    } else if (strcmp(cmd, "set_time") == 0) {
        if (json_get_int(j, "epoch", &v) && v > 1700000000) {
            (void)ds3231_set_epoch_utc((uint32_t)v, s->tz_hours);
        }
    } else if (strcmp(cmd, "frc_calibrate") == 0) {
        s_want_frc = true;
    }
}

/**
 * esp_link_poll — забрать одну готовую строку и разобрать в контексте main.
 *
 * Нет s_ready — сразу выход. Копия s_pending под запретом IRQ, затем s_ready=0.
 * Есть "cmd" → handle_cmd, иначе apply_settings.
 *
 * @param s  настройки приложения (могут записаться во flash)
 *
 * Ловушка: парсить s_pending напрямую нельзя — IRQ может перезаписать его
 * в середине strstr. Парсим только локальную копию.
 */
void esp_link_poll(app_settings_t *s)
{
    char cmd[24];
    char local[UART_LINE_MAX];

    __disable_irq();
    if (!s_ready) {
        __enable_irq();
        return;
    }
    memcpy(local, s_pending, UART_LINE_MAX);
    s_ready = 0;
    __enable_irq();

    s_last_rx_ms = HAL_GetTick();
    if (json_get_cmd(local, cmd, sizeof(cmd))) {
        handle_cmd(local, cmd, s);
    } else {
        apply_settings(local, s);
    }
}

/**
 * esp_link_send_telemetry — одна JSON-строка STM32 → ESP32 (LCD и /api/status).
 *
 * temp из десятых: 231 → "23.1". Отрицательная дробь нормализуется.
 * alert уже 0/1/2. quiet — ночь ИЛИ mute (как решил main). time с DS3231.
 *
 * @param co2       последние ppm (нули, если SCD41 ещё не ответил)
 * @param temp_x10  десятые °C
 * @param rh        %
 * @param alert     0/1/2
 * @param quiet     ночь или mute
 * @param t         снимок DS3231 (может быть invalid → "--:--")
 *
 * Ловушка: слать только если snprintf влез в буфер (n > 0 && n < sizeof).
 * Иначе обрубок без '\n'. TX = запись DR, не HAL_UART_Transmit.
 */
void esp_link_send_telemetry(uint16_t co2, int16_t temp_x10, uint8_t rh,
                             alert_level_t alert, bool quiet, const rtc_time_t *t)
{
    char line[UART_LINE_MAX];
    char hhmm[8];
    int t_i = temp_x10 / 10;
    int t_f = temp_x10 % 10;
    int n;
    if (t_f < 0) {
        t_f = -t_f;
    }
    ds3231_format_hhmm(t, hhmm, sizeof(hhmm));
    n = snprintf(line, sizeof(line),
                 "{\"co2\":%u,\"temp\":%d.%d,\"rh\":%u,\"alert\":%d,\"quiet\":%s,\"time\":\"%s\"}\n",
                 co2, t_i, t_f, rh, (int)alert, quiet ? "true" : "false", hhmm);
    if (n > 0 && n < (int)sizeof(line)) {
        uart1_write((const uint8_t *)line, (uint16_t)n);
    }
}

/**
 * esp_link_esp_alive — ESP32 ещё шлёт пакеты (окно ESP_TIMEOUT_MS = 120 с).
 *
 * Сейчас main этим флагом звук не глушит: автономность важнее.
 *
 * @param now_ms  HAL_GetTick()
 * @return        false, если пакетов не было вовсе или пауза слишком длинная
 *
 * Ловушка: s_last_rx_ms==0 сразу после boot — это «ещё не говорили», не таймаут.
 */
bool esp_link_esp_alive(uint32_t now_ms)
{
    if (s_last_rx_ms == 0u) {
        return false;
    }
    return (now_ms - s_last_rx_ms) < ESP_TIMEOUT_MS;
}

/**
 * esp_link_take_beep — атомарно снять флаг веб-теста.
 *
 * @return true один раз после команды beep, пока main не вызовет buzzer_test_beep
 *
 * Ловушка: вызывать только из суперцикла; в IRQ писк с Delay нельзя.
 */
bool esp_link_take_beep(void)
{
    if (!s_want_beep) {
        return false;
    }
    s_want_beep = false;
    return true;
}

/**
 * esp_link_take_frc — то же для калибровки SCD41.
 *
 * @return true один раз; main должен вызвать scd41_frc_calibrate(400)
 *
 * Ловушка: FRC ~1 с Delay+I2C — только из main после этого флага.
 */
bool esp_link_take_frc(void)
{
    if (!s_want_frc) {
        return false;
    }
    s_want_frc = false;
    return true;
}
