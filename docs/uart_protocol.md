# UART протокол STM32 ↔ ESP32

## Общие правила

- Интерфейс: STM32 USART1 ↔ ESP32 UART2, **115200** baud, 8N1.
- Формат: одна JSON-строка на сообщение, завершается `\n` (0x0A).
- Кодировка: ASCII.
- Максимальная длина строки: 256 байт.

## STM32 → ESP32 (телеметрия)

Каждые 5 s или при смене уровня тревоги.

```json
{"co2":845,"temp":23.1,"rh":45,"alert":1,"quiet":false}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `co2` | int | CO2, ppm (SCD41 NDIR) |
| `temp` | float | Температура, °C |
| `rh` | int | Относительная влажность, % |
| `alert` | int | 0 = normal, 1 = warning, 2 = alarm |
| `quiet` | bool | Тихие часы активны на STM32 |

## ESP32 → STM32 (время и настройки)

При старте, при изменении настроек, раз в 60 s.

```json
{"time":"22:15","quiet":true,"co2_warn":800,"co2_crit":1200,"quiet_start":22,"quiet_end":7,"epoch":1719320000}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `time` | string | Локальное время HH:MM |
| `quiet` | bool | Звук запрещён |
| `co2_warn` | int | Порог warning, ppm |
| `co2_crit` | int | Порог alarm, ppm |
| `quiet_start` | int | Час начала (0–23) |
| `quiet_end` | int | Час конца (0–23) |
| `epoch` | int | Unix time для DS3231 |

### Расчёт quiet (через полночь, 22–07)

```
quiet = (hour >= quiet_start) OR (hour < quiet_end)
```

## Команды ESP32 → STM32

```json
{"cmd":"beep"}
{"cmd":"set_time","epoch":1719320000}
```

| cmd | Действие |
|-----|----------|
| `beep` | Тест KY-006, 1 s, игнор quiet |
| `set_time` | Запись epoch в DS3231 |

## Инициализация

1. STM32: I2C, SCD41, LCD, DS3231, KY-006.
2. ESP32: WiFi, NTP.
3. ESP32 → `set_time` → STM32 → DS3231.
4. ESP32 → настройки порогов и quiet hours.
5. STM32 → телеметрия каждые 5 s.

## Ошибки

- Битый JSON — пакет игнорируется.
- Нет UART от ESP32 &gt; 120 s — STM32 использует DS3231 + настройки из flash.

## Примеры

```
STM32: {"co2":420,"temp":22.0,"rh":48,"alert":0,"quiet":false}
ESP:   {"time":"14:30","quiet":false,"co2_warn":800,"co2_crit":1200,"quiet_start":22,"quiet_end":7,"epoch":1719320000}
ESP:   {"cmd":"beep"}
STM32: {"co2":1350,"temp":24.1,"rh":52,"alert":2,"quiet":true}
```
