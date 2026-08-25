#ifndef CONFIG_H
#define CONFIG_H

/*
 * Пины и периоды ESP32.
 * UART2 — линия на STM32 (не USB Serial). I2C — только LCD.
 */

#define STM32_UART_RX       16          /* UART2 RX ← STM32 PA9; на WROVER этот пин мёртв */
#define STM32_UART_TX       17          /* UART2 TX → STM32 PA10 */
#define STM32_UART_RX_B     25          /* запасной RX: не PSRAM, есть на 30- и 38-pin */
#define STM32_UART_TX_B     26          /* запасной TX */
#define STM32_UART_BAUD     115200

#define LCD_SDA             21
#define LCD_SCL             22
#define LCD_ADDR_PRIMARY    0x27        /* типичный PCF8574 backpack */
#define LCD_ADDR_FALLBACK   0x3F        /* второй частый адрес */

#define HOSTNAME            "co2-sensor" /* mDNS и ArduinoOTA */
#define SETUP_AP_NAME       "CO2-Setup"  /* WiFiManager, первый запуск */

#define TELEMETRY_STALE_MS  10000       /* нет JSON → LCD «NO STM32 LINK» */
#define SETTINGS_PUSH_MS    60000       /* повтор порогов+epoch на STM32 */
#define LCD_REFRESH_MS      1000
#define NTP_SYNC_MS         86400000UL  /* раз в сутки заново configTime */

#define FW_VERSION          "1.0.3"

#endif
