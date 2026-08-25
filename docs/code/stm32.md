# Прошивка STM32

Прикладной код проекта CubeIDE `stm32_cube/co2_stm32`. Драйверы HAL/CMSIS в `Drivers/` не документируются: их пишет ST.

Определение 1: CubeIDE (STM32CubeIDE) — среда ST: чертёж CubeMX, компилятор, отладка через ST-Link. Импортируйте папку `co2_stm32` как existing project.

Определение 2: USER CODE — зоны `USER CODE BEGIN/END`. Generate Code их сохраняет. Файлы `scd41.c`, `alert_fsm.c` и остальные прикладные Generate не трогает: они не из CubeMX.

Определение 3: HAL (Hardware Abstraction Layer) — вызовы вроде `HAL_I2C_Master_Transmit`. Драйверы датчика вызывают HAL; пороги и писки — свой C.

Тактование: сначала HSE 8 МГц × PLL 9 = **72 МГц**. Если внешний кварц не стартует (часто на дешёвом Blue Pill), `SystemClock_Config` переходит на HSI/2 × 16 = **64 МГц**, чтобы UART не умер в `Error_Handler`. USART1 всё равно 115200: HAL считает делитель после выбора PLL.

## Карта файлов

| Файл | Роль |
|------|------|
| `Core/Src/main.c` | Init периферии + суперцикл |
| `Core/Inc/app_config.h` | Константы порогов, периодов, адресов I2C, адрес flash |
| `Core/Inc/app_settings.h` + `app_settings.c` | Структура настроек, load/save flash |
| `scd41.c` / `.h` | Sensirion SCD41 на I2C1 |
| `rtc_ds3231.c` / `.h` | DS3231 на I2C2 |
| `buzzer.c` / `.h` | PWM TIM2_CH3, паттерны писков |
| `alert_fsm.c` / `.h` | Warmup / Normal / Warning / Alarm, mute |
| `quiet_hours.c` / `.h` | Ночной запрет звука по часу DS3231 |
| `esp_link.c` / `.h` | UART JSON, прерывание RX |
| `usart.c` | MX USART1 + NVIC в USER CODE |
| `tim.c` | TIM2 PWM ~2 кГц, период 499, compare 250 = 50 % |
| `stm32f1xx_it.c` | `USART1_IRQHandler` → `HAL_UART_IRQHandler` |
| `i2c.c`, `gpio.c` | Скелет CubeMX, без прикладной логики |

## `app_config.h` — константы

Определение 4: гистерезис (hysteresis, зона нечувствительности) — 50 ppm. Выход из Warning только если CO2 < warn−50, из Alarm — если CO2 < crit−50. Иначе на 800 ppm зуммер щёлкал бы каждые 5 с.

| Макрос | Значение | Смысл |
|--------|----------|-------|
| `SCD41_I2C_ADDR` | 0x62 | 7-битный адрес; в драйвере сдвиг `<< 1` для HAL |
| `DS3231_I2C_ADDR` | 0x68 | То же для I2C2 |
| `DEFAULT_CO2_WARN` / `CRIT` | 800 / 1200 | Пороги с завода |
| `HYSTERESIS_PPM` | 50 | См. выше |
| `WARMUP_MS` | 60000 | SCD41 + стабилизация после FRC |
| `MEASURE_PERIOD_MS` | 5000 | Период опроса датчика |
| `TELEMETRY_PERIOD_MS` | 5000 | Период JSON на ESP32 |
| `ESP_TIMEOUT_MS` | 120000 | «ESP32 молчит» (флаг `esp_link_esp_alive`) |
| `DEFAULT_QUIET_START/END` | 22 / 7 | Тихие часы через полночь |
| `DEFAULT_TZ_HOURS` | 3 | UTC+3 при записи epoch в DS3231 |
| `WARN_BEEP_PERIOD_MS` | 300000 | 5 мин между warning-писками |
| `ALARM_BEEP_PERIOD_MS` | 30000 | 30 с между тройками alarm |
| `BEEP_PULSE_MS` | 200 | Длина одного писка |
| `TEST_BEEP_MS` | 1000 | Тест с веба |
| `SETTINGS_FLASH_ADDR` | 0x0800FC00 | Последняя 1 КБ страница 64 КБ flash |
| `SETTINGS_MAGIC` | 0xC0C0A11A | Если не совпало — заводские умолчания |
| `UART_LINE_MAX` | 256 | Буфер одной JSON-строки |

## `main.c` — суперцикл

После `MX_*_Init`: печать `STM32 boot\r\n`, `settings_load`, `buzzer_init`, `ds3231_init`, `scd41_init`, `alert_fsm_init`, `esp_link_init`.

Глобальные `g_co2`, `g_temp_x10`, `g_rh`, `g_alert`, `g_rtc`, `g_have_sample` — последнее удачное измерение. Если `scd41_read` вернул false, телеметрия всё равно уходит со старыми числами (или нулями до первого успеха).

`temp_x10` — десятые доли °C целым числом (231 = 23.1 °C), чтобы не таскать float на Cortex-M3 без обязательного FPU-кода в телеметрии (дробь собирается в `snprintf`).

Определение 5: FPU (Floating Point Unit) — блок вещественной арифметики. У F103 его нет; float в C эмулируется программно. Поэтому температура хранится как `int16_t` десятых.

## `scd41.c` — датчик CO2

Определение 6: SCD41 (Sensirion CO2 / humidity / temperature) — NDIR-датчик на I2C, адрес 0x62. Отдаёт ppm, температуру и RH. Шина: I2C1 PB6/PB7.

Определение 7: NDIR (Non-Dispersive Infrared) — измерение CO2 по поглощению ИК, не «электрохимическая ячейка». Нужен прогрев порядка минуты.

Определение 8: CRC-8 Sensirion — контрольная сумма полином 0x31, init 0xFF, на каждые 2 байта слова. Без совпадения CRC измерение отбрасывается.

Команды (16-бит, MSB first):

| Код | Имя | Когда |
|-----|-----|-------|
| 0x3F86 | stop periodic | init и перед FRC |
| 0x21B1 | start periodic | после stop, период измерений датчика |
| 0xE4B8 | get data ready | младшие 11 бит ≠ 0 → можно читать |
| 0xEC05 | read measurement | 9 байт: CO2, T, RH по 2+CRC |
| 0x362F | FRC | аргумент target ppm + CRC |

Формулы (как в даташите):

- T°C = −45 + 175 × raw_t / 65535 → в коде десятые: `−450 + 1750 × raw / 65535`
- RH% = 100 × raw_rh / 65535
- CO2 0 или > 5000 ppm → отказ (мусор / нет воздуха в камере)

`scd41_frc_calibrate(400)`: stop → 500 мс → FRC → 400 мс → прочитать слово результата (0xFFFF = ошибка) → снова start periodic.

`HAL_Delay` внутри init/FRC блокирует главный цикл на доли секунды — это сознательно, не из прерывания.

## `rtc_ds3231.c` — часы

Определение 9: RTC (Real-Time Clock) — часы, которые идут от батарейки CR2032, когда 3.3 В снято. Тихие часы без Wi‑Fi держатся на DS3231.

Определение 10: BCD (Binary-Coded Decimal) — две десятичные цифры в одном байте (0x23 = 23 часа). DS3231 хранит регистры так; `bin2bcd` / `bcd2bin` переводят.

| Функция | Действие |
|---------|----------|
| `ds3231_init` | `HAL_I2C_IsDeviceReady` на I2C2 |
| `ds3231_read` | регистр 0x00, 3 байта sec/min/hour, бит 12/24h маской 0x3F |
| `ds3231_set_epoch_utc` | epoch + tz×3600 → календарь → запись 7 регистров с 0x00 |
| `ds3231_format_hhmm` | `"22:15"` или `"--:--"` если `!valid` |

`unix_to_hms` считает дни с 1970, високосные, weekday для DS3231 (1 = воскресенье). Год пишется как 00–99 от 2000; если расчёт уехал за 2099, подставляется 26.

Запись идёт **локальным** временем (epoch UTC плюс `tz_hours`). `quiet_hours` смотрит `t->hour` как стенные часы.

## `buzzer.c` — звук

Определение 11: PWM (Pulse Width Modulation) — меандр с частотой тона и скважностью. Пассивному KY-006 нужна волна ~2 кГц, не постоянные 3.3 В.

TIM2: PSC=71, ARR=499 → 1 МГц / 500 = **2 кГц**. `CCR3=250` — 50 % (писк), `0` — тишина. `buzzer_init` стартует PWM сразу с нулевым compare.

| Функция | Поведение |
|---------|-----------|
| `buzzer_test_beep(ms)` | Вкл, выкл через `s_off_at` в `poll` |
| `buzzer_pattern_warning` | Один импульс 200 мс |
| `buzzer_pattern_alarm` | Три импульса: on 200 мс, пауза до следующего старта 400 мс |
| `buzzer_poll` | Обязателен каждый оборот `main`; иначе alarm не допищит |

Тест с веба бьёт `buzzer_test_beep` напрямую, минуя FSM.

## `alert_fsm.c` — пороги и mute

Определение 12: FSM тревоги — состояния `ALERT_WARMUP (-1)`, `NORMAL (0)`, `WARNING (1)`, `ALARM (2)`. В JSON наружу warmup отдаётся как 0.

Переходы (`alert_fsm_update`):

```
warmup < 60 с           → наружу NORMAL, внутри WARMUP
NORMAL/WARMUP + CO2≥crit → ALARM
NORMAL/WARMUP + CO2≥warn → WARNING
WARNING + CO2≥crit       → ALARM
WARNING + CO2 < warn−50  → NORMAL
ALARM + CO2 < crit−50    → WARNING если ещё ≥warn, иначе NORMAL
```

Mute: `alert_fsm_set_mute(now, minutes)` ставит `s_mute_until`. Потолок минут на стороне UART — 24 часа.

`alert_fsm_sound_allowed`: false, если `!alerts_enabled`, quiet, или mute.

`alert_fsm_process_sound`: при Warning не чаще 5 мин вызывает `buzzer_pattern_warning`; при Alarm не чаще 30 с — `buzzer_pattern_alarm`.

`alert_fsm_reset_warmup` после FRC: снова 60 с без звука по порогам.

## `quiet_hours.c`

Одна функция `quiet_hours_active(settings, rtc)`:

- часы невалидны → **false** (не глушить весь день);
- `start == end` → выключено;
- `start > end` (22→7) → `hour >= start || hour < end`;
- иначе обычный интервал в пределах суток.

Определение 13: тихие часы (quiet hours) — окно, когда зуммер молчит, а LCD и веб живут. Считаются по **часу** DS3231, минуты не участвуют: в 22:00 уже тихо, в 07:00 уже нет.

В `main` флаг `quiet` для телеметрии = quiet hours **или** mute, чтобы на LCD было «тихо».

## `esp_link.c` — UART

Определение 14: прерывание USART (USART interrupt) — каждый принятый байт вызывает `HAL_UART_RxCpltCallback` → `esp_link_rx_byte` → снова `HAL_UART_Receive_IT` на 1 байт. Строка копируется в `s_pending`, флаг `s_ready`; разбор в `esp_link_poll` уже в `main` (не в IRQ).

Сборка строки: `\r` игнор, `\n` конец, переполнение 256 → сброс. Если предыдущая строка ещё не разобрана, новая с `\n` теряется (один слот).

Парсер — поиск `"ключ":` через `strstr`. Это не RFC JSON: пробелы после `:` допускаются у int/bool, строки cmd только в кавычках сразу после двоеточия.

`handle_cmd`:

| cmd | Эффект |
|-----|--------|
| `beep` | `s_want_beep` |
| `mute` | минуты 1…1440, `alert_fsm_set_mute` |
| `unmute` | снять mute |
| `set_alerts` | `alerts_enabled` + save |
| `set_time` | `ds3231_set_epoch_utc` |
| `frc_calibrate` | `s_want_frc` |

Без `cmd` — `apply_settings`: warn 400–4000, crit 500–5000, если warn≥crit то crit=warn+100; quiet 0–23; tz −12…14; epoch > 1.7e9; `alerts_enabled`. Любое изменение → `settings_save`.

Телеметрия:

```json
{"co2":845,"temp":23.1,"rh":45,"alert":1,"quiet":false,"time":"22:15"}
```

TX блокирующий `HAL_UART_Transmit`, таймаут 100 мс.

## `app_settings.c` — flash

Структура `settings_blob_t`: magic, warn, crit, quiet_start/end, tz, alerts_enabled, reserved. Выравнивание 4 байта.

`settings_load`: всегда сначала defaults, затем overlay если magic верный и пороги разумные.

`settings_save`: Unlock → erase 1 page → program halfwords → Lock. Ошибка erase/program — тихий return, RAM-копия в `g_settings` уже новая.

Определение 15: halfword program — запись flash F103 по 16 бит. Байтовую запись HAL не умеет.

## CubeMX-скелет, который нельзя «просто править»

| Файл | Важно для приложения |
|------|----------------------|
| `tim.c` | PSC 71, ARR 499, CH3 PWM, PA2 |
| `usart.c` USER | `HAL_NVIC_EnableIRQ(USART1_IRQn)` — иначе RX не живёт |
| `stm32f1xx_it.c` USER | `USART1_IRQHandler` вызывает HAL |
| `i2c.c` | `hi2c1` SCD41, `hi2c2` DS3231 |
| `Error_Handler` | IRQ off, вечный цикл — смотреть в отладчике |

## Определения

1. CubeIDE  
2. USER CODE  
3. HAL  
4. гистерезис  
5. FPU  
6. SCD41  
7. NDIR  
8. CRC-8 Sensirion  
9. RTC  
10. BCD  
11. PWM  
12. FSM тревоги  
13. тихие часы  
14. прерывание USART  
15. halfword program  
