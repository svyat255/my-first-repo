#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include "config.h"
#include "app_settings.h"
#include "lcd_i2c.h"
#include "uart_bridge.h"
#include "ntp_sync.h"
#include "ota.h"
#include "web_ui.h"
#include "notify.h"

/** g_settings — пороги, quiet, tz и токены из NVS; пишет веб POST, читают UART/notify/ntp. */
static AppSettings g_settings;
/** g_tel — последняя телеметрия STM32; пишет uart_bridge_poll, читают LCD/веб/notify. */
static Telemetry g_tel;
/** g_last_push — millis() последнего uart_bridge_send_settings; пишет loop и start_services. */
static uint32_t g_last_push;
/** g_last_lcd — millis() последней отрисовки LCD; пишет loop. */
static uint32_t g_last_lcd;
/** g_last_ntp — millis() последнего ntp_begin; пишет loop и start_services. */
static uint32_t g_last_ntp;
/** g_prev_alert — прошлый уровень тревоги для фронта Alarm; пишет loop, читает notify. */
static int g_prev_alert;
/**
 * g_services_up — mDNS/NTP/OTA/веб и первый push уже подняты после STA.
 * Пишет start_network_services, читает loop (не поднимать сервисы дважды).
 */
static bool g_services_up;
/**
 * g_wm — WiFiManager на весь run: неблокирующий портал CO2-Setup.
 * process() каждый loop, пока пользователь вводит SSID; после коннекта — дешёвый no-op.
 */
static WiFiManager g_wm;

/**
 * format_lcd — две строки по 16 символов для 1602 из последней телеметрии.
 *
 * Нет пакета > 10 с: «NO STM32 LINK» и IP (чтобы открыть веб без mDNS).
 * Строка 1: "{ppm}ppm [OK|WARN|ALRM]" — слово ALARM не влезает.
 * Строка 2 при Alarm: «OPEN WINDOW!», иначе T, RH и время с STM32.
 *
 * @param l1,l2  буферы, обычно 20 байт (snprintf режет)
 * @param n      sizeof одного буфера
 */
static void format_lcd(char *l1, char *l2, size_t n)
{
    if (uart_bridge_stale(g_tel)) {
        snprintf(l1, n, "NO STM32 LINK");
        snprintf(l2, n, "%s", WiFi.localIP().toString().c_str());
        return;
    }
    const char *tag = "[OK]";
    if (g_tel.alert == 1) {
        tag = "[WARN]";
    } else if (g_tel.alert >= 2) {
        tag = "[ALRM]";
    }
    snprintf(l1, n, "%uppm %s", g_tel.co2, tag);
    if (g_tel.alert >= 2) {
        snprintf(l2, n, "OPEN WINDOW!");
    } else {
        snprintf(l2, n, "%0.1fC %u%% %s", g_tel.temp, g_tel.rh, g_tel.time.c_str());
    }
}

/**
 * start_network_services — один раз, когда WiFi.status()==WL_CONNECTED.
 *
 * mDNS co2-sensor.local, NTP, ArduinoOTA, веб :80 и первый push настроек
 * на STM32 (пороги + epoch, если NTP уже успел). Пока портал CO2-Setup
 * открыт, сюда не заходим — UART тем временем крутится в loop().
 */
static void start_network_services()
{
    if (g_services_up) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    MDNS.begin(HOSTNAME);
    ntp_begin(g_settings.tz_hours);
    ota_begin();
    web_begin(g_settings, g_tel);
    uart_bridge_send_settings(g_settings, ntp_epoch_utc());
    g_last_push = millis();
    g_last_ntp = millis();
    g_services_up = true;
}

/**
 * setup — один раз при старте ESP32 (Arduino).
 *
 * Порядок: NVS → LCD → UART2 → WiFiManager. Портал неблокирующий:
 * autoConnect не держит setup() на всём вводе SSID, байты STM32 не копятся
 * в кольце без poll. Нет сохранённой сети — точка CO2-Setup, имя на LCD.
 *
 * Serial 115200 — USB-монитор, это не линия на STM32 (та — UART2).
 */
void setup()
{
    Serial.begin(115200);
    settings_load(g_settings);
    lcd_init();
    uart_bridge_begin();
    Serial.println(F("UART STM32: GPIO16/17 и запас GPIO25/26 (WROVER → 25/26)"));

    WiFi.mode(WIFI_STA);
    g_wm.setHostname(HOSTNAME);
    g_wm.setConfigPortalBlocking(false);
    lcd_show("WiFi setup", SETUP_AP_NAME);
    g_wm.autoConnect(SETUP_AP_NAME);
    start_network_services();

    g_last_lcd = 0;
    g_prev_alert = 0;
}

/**
 * loop — бесконечный цикл ESP32, аналог while(1) на STM32.
 *
 * Каждый оборот: портал Wi‑Fi (process) и байты UART — даже пока пользователь
 * вводит пароль на CO2-Setup. Сервисы сети — только после STA.
 * Раз в 1 с — LCD: на портале имя AP, иначе телеметрия.
 * notify_on_alert: poll UART до и после HTTPS, чтобы FIFO не ронялся.
 * g_prev_alert обновляем только при valid, чтобы пустой старт не был «уже Alarm».
 * Раз в 60 с снова шлём настройки: STM32 мог перезагрузиться.
 * Раз в сутки — configTime заново.
 */
void loop()
{
    g_wm.process();
    uart_bridge_poll(g_tel);
    start_network_services();

    if (g_services_up) {
        ota_poll();
        web_poll();
    }

    uint32_t now = millis();
    if (now - g_last_lcd >= LCD_REFRESH_MS) {
        if (!g_services_up) {
            lcd_show("WiFi setup", SETUP_AP_NAME);
        } else {
            char l1[20];
            char l2[20];
            format_lcd(l1, l2, sizeof(l1));
            lcd_show_raw(l1, l2);
        }
        g_last_lcd = now;
    }

    if (g_services_up) {
        uart_bridge_poll(g_tel);
        notify_on_alert(g_settings, g_tel, g_prev_alert);
        uart_bridge_poll(g_tel);
        if (g_tel.valid) {
            g_prev_alert = g_tel.alert;
        }

        if (now - g_last_push >= SETTINGS_PUSH_MS) {
            uart_bridge_send_settings(g_settings, ntp_epoch_utc());
            g_last_push = now;
        }

        if (now - g_last_ntp >= NTP_SYNC_MS) {
            ntp_begin(g_settings.tz_hours);
            g_last_ntp = now;
        }
    }
}
