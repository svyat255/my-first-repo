# Прошивка ESP32

PlatformIO, каталог `esp32/`. Плата `esp32dev`, Arduino, раздел `min_spiffs.csv` (место под OTA).

Определение 1: PlatformIO — сборка/заливка из `platformio.ini` командами `pio run -t upload` и `pio device monitor`. Не Arduino IDE: зависимости `lib_deps` ставятся сами.

Определение 2: Arduino framework на ESP32 — `setup()` / `loop()`, `HardwareSerial`, `millis()`. Это не RTOS-задача пользователя, хотя под капотом FreeRTOS есть.

Зависимости: `LiquidCrystal_I2C`, `WiFiManager`.

Пины и периоды — `include/config.h`.

## Карта файлов

| Файл | Роль |
|------|------|
| `src/main.cpp` | Wi‑Fi, LCD-кадр, периодический push настроек |
| `include/config.h` | GPIO UART/LCD, hostname, таймауты |
| `app_settings.cpp` | NVS namespace `co2` |
| `uart_bridge.cpp` | UART2 JSON к STM32 |
| `lcd_i2c.cpp` | LCD 1602 через PCF8574 |
| `web_ui.cpp` | HTML + REST + POST `/update` |
| `ntp_sync.cpp` | `configTime` / Unix epoch |
| `notify.cpp` | Telegram и ntfy при входе в Alarm |
| `ota.cpp` | ArduinoOTA по сети (hostname `co2-sensor`) |

## `config.h`

| Макрос | Значение | Смысл |
|--------|----------|-------|
| `STM32_UART_RX/TX` | 16 / 17 | UART2: RX←PA9, TX→PA10 (на WROVER мёртвые) |
| `STM32_UART_RX_B/TX_B` | 25 / 26 | Запасной UART1, без PSRAM |
| `STM32_UART_BAUD` | 115200 | Как USART1 |
| `LCD_SDA/SCL` | 21 / 22 | Шитый I2C DevKit |
| `LCD_ADDR_PRIMARY/FALLBACK` | 0x27 / 0x3F | Два типичных адреса backpack |
| `HOSTNAME` | `co2-sensor` | mDNS и ArduinoOTA |
| `SETUP_AP_NAME` | `CO2-Setup` | Captive AP первого запуска |
| `TELEMETRY_STALE_MS` | 10000 | LCD «NO STM32 LINK» |
| `SETTINGS_PUSH_MS` | 60000 | Повтор настроек+epoch на STM32 |
| `LCD_REFRESH_MS` | 1000 | Перерисовка, даже если JSON реже |
| `NTP_SYNC_MS` | 86400000 | Раз в сутки заново NTP |
| `FW_VERSION` | `1.0.3` | В `/api/status` и на странице |

## `main.cpp`

Определение 3: mDNS (multicast DNS) — имя `http://co2-sensor.local` в LAN без помнить IP. Нужна поддержка .local на телефоне (часто iOS/macOS; Android — зависит).

`format_lcd`:

- stale → строка 1 `NO STM32 LINK`, строка 2 = IP;
- иначе строка 1 `{ppm}ppm [OK|WARN|ALRM]` (слово ALARM не влезает в 16 символов);
- Alarm: строка 2 `OPEN WINDOW!`;
- иначе `{T}C {RH}% {HH:MM}`.

`g_prev_alert` запоминает прошлый уровень, чтобы `notify_on_alert` стрельнул один раз на фронте 0/1 → 2.

WiFiManager неблокирующий (`setConfigPortalBlocking(false)`): каждый `loop` вызывает `wm.process()` и `uart_bridge_poll()`, пока пользователь вводит SSID на AP `CO2-Setup`. LCD в это время показывает имя AP. mDNS, NTP, OTA, веб и первый push настроек — один раз, когда `WiFi.status()==WL_CONNECTED` (флаг `g_services_up`).

## `app_settings.cpp`

Определение 4: Preferences — обёртка NVS. `p.begin("co2", true)` — только чтение; `false` — запись. Ключи короткие: `warn`, `crit`, `qstart`, `qend`, `tz`, `tg_token`, `tg_chat`, `ntfy`.

Структура `AppSettings` на ESP32 шире STM32: токены уведомлений STM32 не видит.

Если warn ≥ crit при загрузке — crit = warn + 100.

## `uart_bridge.cpp`

`HardwareSerial stm32Serial(2)` — второй аппаратный UART ESP32.

Парсер телеметрии: `indexOf("\"co2\":")` обязателен, иначе строка не телеметрия (например, отладочный `STM32 boot`). Остальные поля опциональны. `"quiet":true` ищется как подстрока.

`uart_bridge_send_settings` собирает объект без `cmd`; epoch добавляется только если NTP уже дал > 1.7e9.

`uart_bridge_send_cmd` — `println`, то есть JSON + `\r\n`. STM32 `\r` отбрасывает.

`uart_bridge_stale`: нет ни одного валидного пакета или старше 10 с.

Счётчики `rx_gpio16` и `rx_gpio25` раздельные: ноль на 16 при байтах на 25 = WROVER/PSRAM, провод надо на GPIO25/26. Авто-swap RX/TX UART2 по `millis()>10 с` **нет**: он ломал верную проводку PA9→GPIO16 после длинного портала. GPIO16/17 остаются как в `config.h`; GPIO25/26 слушаются параллельно. Петля на странице шлёт `{"ping":1}` (без `co2`) в тот же `Telemetry`, что main. Каждая строка дублируется в USB Serial (`[STM32 UART] …`).

Определение 5: UART2 (Serial2) — отдельный контроллер ESP32. UART0 занят USB-монитором 115200 (`Serial.begin` в `setup`). Не путать логи USB с линией на STM32.

## `lcd_i2c.cpp`

Определение 6: PCF8574 — I2C-расширитель на «рюкзаке» 1602, типичные адреса 0x27 и 0x3F. ESP32 сначала пробует 0x27, при NACK — 0x3F; оба молчат — указатель `lcd` остаётся nullptr, `lcd_show` тихо ничего не делает.

`fit16` дополняет пробелами или режет до 16 символов, чтобы не оставался хвост предыдущей длинной строки.

Старт: `CO2 monitor` / `boot...`, затем `WiFi setup` / `CO2-Setup`.

## `web_ui.cpp`

Определение 7: REST API (Representational State Transfer) — HTTP-методы на URL: GET читает JSON, POST меняет состояние. UI — одна страница `/` из PROGMEM.

Определение 8: PROGMEM — константы в flash, не в DRAM. HTML лежит в `INDEX_HTML[]`.

| Метод | Путь | Действие |
|-------|------|----------|
| GET | `/` | HTML (RU): статус, mute, настройки, FRC, OTA-файл |
| GET | `/api/status` | co2, temp, rh, alert, quiet, time, stale, ip, version |
| GET | `/api/settings` | пороги, quiet, tz, токены, version |
| POST | `/api/settings` | тело JSON → NVS + `ntp_begin` + UART settings |
| POST | `/api/beep` | `{"cmd":"beep"}` |
| POST | `/api/mute` | mute 60 мин |
| POST | `/api/unmute` | unmute |
| POST | `/api/calibrate` | `frc_calibrate` (в UI есть `confirm`) |
| POST | `/api/uart_loop` | `{"ping":1}` в оба TX, прирост байт RX |
| POST | `/update` | multipart прошивка ESP32; reboot только если `Update.end` успешен |

Парсер POST настроек тот же кустарный `jsonInt`/`jsonStr`. Пустой `plain` пробует arg `body`.

OTA-файл: колбэк проверяет `Update.begin()`; `ESP.restart()` только после успешного `Update.end`. При FAIL ответ 500, рестарта нет.

Страница: «STM32 не отвечает» / «нет связи» только если `rx_bytes===0`. Если байты есть, но stale — тексты boot / не JSON / тишина >10 с.

`web_poll` = `server.handleClient()` каждый `loop`.

Страница опрашивает `/api/status` каждые 2 с.

## `ntp_sync.cpp`

`configTime(tz*3600, 0, "pool.ntp.org", "time.google.com")` — смещение зоны сразу в ESP. `time()` на ESP-IDF после этого обычно UTC; в UART уходит `ntp_epoch_utc()` как Unix UTC, а локализацию делает STM32 через `tz_hours` при записи DS3231.

Если NTP ещё не пришёл, epoch = 0 и в JSON настроек поле `epoch` не добавляется — STM32 не затрёт DS3231 нулём.

`ntp_hhmm()` использует `getLocalTime` (сейчас не рисуется на LCD: время берётся из телеметрии STM32).

## `notify.cpp`

Определение 9: ntfy — HTTP-пуш на `https://ntfy.sh/{topic}`. Топик задаётся в настройках, без пароля в v1.

Определение 10: Telegram Bot API — GET `sendMessage` с token и chat_id. Пробелы в тексте заменяются на `%20`, остальное не URL-encode.

Срабатывание: живая телеметрия (`valid` и не stale), `t.alert >= 2` и `prev_alert < 2` (фронт входа в Alarm). Пока держится Alarm, повторных пушей нет. Спад в Warning и новый Alarm — снова пуш. `uart_bridge_poll` до и после HTTPS, чтобы FIFO UART не ронялся на длинном TLS.

TLS: `WiFiClientSecure.setInsecure()` — сертификат сервера не проверяется (простота на устройстве без хранилища CA).

Если Wi‑Fi нет — тихий return.

## `ota.cpp`

Определение 11: ArduinoOTA — прошивка по Wi‑Fi из PlatformIO/`pio run -t upload` когда устройство в той же сети и hostname резолвится. Это отдельно от HTML `/update` (загрузка `.bin` браузером).

`ota_begin` ставит hostname и `begin()`. `ota_poll` → `ArduinoOTA.handle()`.

## Типичные правки

| Хочу | Куда |
|------|------|
| Другие пины UART/LCD | `config.h` |
| Текст LCD | `format_lcd` в `main.cpp` |
| Новое поле API | `web_ui.cpp` INDEX + handlers |
| Другой интервал mute с кнопки | строка `minutes`: 60 в `web_ui.cpp` |
| Пороги по умолчанию | `app_settings` ESP + `app_config.h` STM32 |
| Отключить ntfy | пустой topic в веб-форме |

## Определения

1. PlatformIO  
2. Arduino framework  
3. mDNS  
4. Preferences  
5. UART2  
6. PCF8574  
7. REST API  
8. PROGMEM  
9. ntfy  
10. Telegram Bot API  
11. ArduinoOTA  
