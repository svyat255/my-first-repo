# Монтаж и схема подключения

Итоговая конфигурация — см. [`PLAN.md`](../PLAN.md).

## Компоненты

| Компонент | Модель | Хост |
|-----------|--------|------|
| МК | STM32F103C8T6 (Blue Pill) | — |
| CO2 | Sensirion SCD41 (I2C, 0x62) | STM32 I2C1 |
| RTC | DS3231 (I2C, 0x68) + CR2032 | STM32 I2C2 |
| Зуммер | KY-006 (HW-508), пассивный | STM32 PA2 |
| Дисплей | LCD 1602 + PCF8574 (0x27 / 0x3F) | ESP32 I2C |
| WiFi | ESP32 DevKit | — |
| Питание | USB 5 V → AMS1117-3.3 (≥ 1 A) | общая шина |

Кнопки нет — управление с HTML.

## STM32 I2C1 — SCD41 (PB6 / PB7)

| Модуль | SCL | SDA | VCC | GND |
|--------|-----|-----|-----|-----|
| SCD41 | PB6 | PB7 | 3.3 V | GND |

Подтяжки 4.7 kΩ на SCL и SDA к 3.3 V, если нет на модуле.

## STM32 I2C2 — DS3231 (PB10 / PB11)

| Модуль | SCL | SDA | VCC | GND |
|--------|-----|-----|-----|-----|
| DS3231 | PB10 | PB11 | 3.3 V | GND |

Подтяжки 4.7 kΩ на SCL и SDA к 3.3 V, если нет на модуле.

## ESP32 I2C — LCD 1602

| Модуль | SCL | SDA | VCC | GND |
|--------|-----|-----|-----|-----|
| LCD 1602 + PCF8574 | GPIO22 | GPIO21 | 3.3 V* | GND |

\* Если backpack только 5 V — VCC от 5 V rail, GND общий.

Подтяжки 4.7 kΩ на GPIO21/22 к 3.3 V, если нет на модуле.

## UART STM32 ↔ ESP32

| STM32 | ESP32 |
|-------|-------|
| PA9 (USART1 TX) | GPIO16 (RX2) |
| PA10 (USART1 RX) | GPIO17 (TX2) |
| GND | GND |

115200 baud, 8N1.

## KY-006 (HW-508)

| Pin | Подключение |
|-----|-------------|
| + | 3.3 V |
| S | **PA2** через 100 Ω |
| − | GND |

PWM TIM2_CH3, тон 2–4 kHz.

## Сводка пинов

```
STM32:
  PA2  — KY-006 SIG
  PA9  — UART TX → ESP32 GPIO16
  PA10 — UART RX ← ESP32 GPIO17
  PB6  — I2C1 SCL → SCD41
  PB7  — I2C1 SDA → SCD41
  PB10 — I2C2 SCL → DS3231
  PB11 — I2C2 SDA → DS3231

ESP32:
  GPIO16 — UART RX2
  GPIO17 — UART TX2
  GPIO21 — LCD SDA
  GPIO22 — LCD SCL
```

## Проверка после сборки

1. Шина 3.3 V стабильна при включённом WiFi ESP32.
2. I2C1 scan (STM32): SCD41 **0x62**.
3. I2C2 scan (STM32): DS3231 **0x68**.
4. I2C scan (ESP32): LCD **0x27** или **0x3F**.
5. SCD41: прогрев ~60 s; выдох → рост CO2 в UART и на LCD.
6. ESP32: WiFi, `http://co2-sensor.local` или IP роутера; mute/beep с страницы.
