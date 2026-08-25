/*
 * SCD41 (Sensirion) на I2C1 (PB6 SCL / PB7 SDA), 7-битный адрес 0x62.
 *
 * Датчик сам крутит periodic measurement (~5 с на точку). Мы только:
 * шлём 16-битные команды big-endian и читаем слова «2 байта + CRC».
 * HAL ждёт 8-битный адрес: SCD41_ADDR = 0x62 << 1.
 */
#include "scd41.h"
#include "app_config.h"
#include "i2c.h"

#define SCD41_CMD_START_PERIODIC    0x21B1u
#define SCD41_CMD_READ_MEASUREMENT  0xEC05u
#define SCD41_CMD_STOP_PERIODIC     0x3F86u
#define SCD41_CMD_GET_DATA_READY    0xE4B8u
#define SCD41_CMD_FRC               0x362Fu
#define SCD41_ADDR                  (SCD41_I2C_ADDR << 1)

/**
 * crc8 — контрольная сумма Sensirion на буфере.
 *
 * CRC-8, полином 0x31, старт 0xFF, биты не отражаются.
 * После каждых двух байт данных датчик шлёт третий — этот CRC.
 * Несовпадение = битая шина или чужой чип, измерение выбрасываем.
 *
 * @param data  байты слова (обычно ровно 2)
 * @param len   длина
 * @return      8-битная сумма как в даташите SCD4x
 */
static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFu;
    uint8_t i, b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8u; b++) {
            if (crc & 0x80u) {
                crc = (uint8_t)((crc << 1) ^ 0x31u);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * write_cmd — отправить команду без аргумента (stop / start / get_ready / read).
 *
 * @param cmd  16-битный код из даташита, старший байт уходит первым
 * @return     true, если HAL_I2C_Master_Transmit на I2C1 вернул HAL_OK
 *
 * Ловушка: только hi2c1. Таймаут 200 мс, не HAL_MAX_DELAY.
 */
static bool write_cmd(uint16_t cmd)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(cmd >> 8);
    buf[1] = (uint8_t)(cmd & 0xFFu);
    return HAL_I2C_Master_Transmit(&hi2c1, SCD41_ADDR, buf, 2, 200) == HAL_OK;
}

/**
 * write_cmd_arg — команда + 16-битный аргумент + CRC аргумента (нужно FRC).
 *
 * Кадр: [cmd_hi][cmd_lo][arg_hi][arg_lo][crc8(arg)].
 *
 * @param cmd  например 0x362F (forced recalibration)
 * @param arg  целевые ppm (мы всегда передаём 400)
 * @return     true при успешном I2C transmit
 *
 * Ловушка: CRC считается только по аргументу, не по cmd.
 */
static bool write_cmd_arg(uint16_t cmd, uint16_t arg)
{
    uint8_t buf[5];
    buf[0] = (uint8_t)(cmd >> 8);
    buf[1] = (uint8_t)(cmd & 0xFFu);
    buf[2] = (uint8_t)(arg >> 8);
    buf[3] = (uint8_t)(arg & 0xFFu);
    buf[4] = crc8(&buf[2], 2);
    return HAL_I2C_Master_Transmit(&hi2c1, SCD41_ADDR, buf, 5, 200) == HAL_OK;
}

/**
 * scd41_init — остановить старый periodic (если был) и запустить новый.
 *
 * После reset чип может ещё мерить. Stop + пауза 500 мс — требование даташита,
 * иначе Start periodic не примется. Start не даёт сразу валидный CO2:
 * первые десятки секунд — warmup в alert_fsm.
 *
 * @return true, если Start periodic ушёл по I2C (датчик может ещё не быть готов)
 */
bool scd41_init(void)
{
    (void)write_cmd(SCD41_CMD_STOP_PERIODIC);
    HAL_Delay(500);
    if (!write_cmd(SCD41_CMD_START_PERIODIC)) {
        return false;
    }
    HAL_Delay(1);
    return true;
}

/**
 * scd41_reinit_periodic — то же, что init; оставлено на случай сбоя шины.
 *
 * Ловушка: внутри Delay 500 мс — только из main, не из IRQ.
 */
void scd41_reinit_periodic(void)
{
    (void)scd41_init();
}

/**
 * data_ready — спросить чип, есть ли свежее измерение в буфере.
 *
 * Команда get data ready, ответ: 2 байта + CRC. Готово, если младшие 11 бит ≠ 0.
 * false бывает часто (ещё не прошло ~5 с) — это не поломка.
 *
 * @return true только если CRC сошёлся и флаг ready выставлен
 */
static bool data_ready(void)
{
    uint8_t rx[3];
    uint16_t word;
    if (!write_cmd(SCD41_CMD_GET_DATA_READY)) {
        return false;
    }
    HAL_Delay(1);
    if (HAL_I2C_Master_Receive(&hi2c1, SCD41_ADDR, rx, 3, 200) != HAL_OK) {
        return false;
    }
    if (crc8(rx, 2) != rx[2]) {
        return false;
    }
    word = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    return (word & 0x07FFu) != 0u;
}

/**
 * scd41_read — снять одну точку: CO2 ppm, температура, влажность.
 *
 * Если data_ready=false, сразу выходим, не трогая прошлые g_co2 в main.
 * Кадр измерения: 9 байт = три слова (CO2, raw T, raw RH), у каждого свой CRC.
 *
 * Температура: T = −45 + 175 * raw / 65535, в RAM кладём десятые °C (int16),
 * чтобы на Cortex-M3 без FPU не возить float до UART.
 * RH = 100 * raw / 65535, обрезаем до 0…100.
 * CO2 0 или > 5000 считаем мусором (открытая камера / сбой).
 *
 * @param co2_ppm   выход, ppm
 * @param temp_x10  выход, десятые градуса (231 = 23.1 °C)
 * @param rh        выход, %
 * @return          true только при всех CRC и разумном CO2
 */
bool scd41_read(uint16_t *co2_ppm, int16_t *temp_x10, uint8_t *rh)
{
    uint8_t rx[9];
    uint16_t raw_t, raw_rh;
    if (!data_ready()) {
        return false;
    }
    if (!write_cmd(SCD41_CMD_READ_MEASUREMENT)) {
        return false;
    }
    HAL_Delay(1);
    if (HAL_I2C_Master_Receive(&hi2c1, SCD41_ADDR, rx, 9, 200) != HAL_OK) {
        return false;
    }
    if (crc8(rx, 2) != rx[2] || crc8(&rx[3], 2) != rx[5] || crc8(&rx[6], 2) != rx[8]) {
        return false;
    }
    *co2_ppm = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    raw_t = (uint16_t)(((uint16_t)rx[3] << 8) | rx[4]);
    raw_rh = (uint16_t)(((uint16_t)rx[6] << 8) | rx[7]);
    *temp_x10 = (int16_t)(-450 + ((int32_t)1750 * (int32_t)raw_t) / 65535);
    *rh = (uint8_t)(((uint32_t)100u * raw_rh) / 65535u);
    if (*co2_ppm == 0u || *co2_ppm > 5000u) {
        return false;
    }
    return true;
}

/**
 * scd41_frc_calibrate — Forced Recalibration: «считай текущий воздух за target ppm».
 *
 * Имеет смысл только на улице (~400 ppm). В комнате испортит шкалу.
 * FRC запрещён во время periodic: stop → 500 мс → команда с аргументом →
 * 400 мс работы чипа → прочитать слово результата → снова start.
 * 0xFFFF в ответе = отказ. При любой ошибке пытаемся вернуть periodic,
 * чтобы монитор не остался немым.
 *
 * Блокирует main на ~1 с (HAL_Delay). Вызывать только из суперцикла, не из IRQ.
 *
 * @param target_ppm  обычно 400
 * @return            true, если CRC ок и результат не 0xFFFF
 */
bool scd41_frc_calibrate(uint16_t target_ppm)
{
    uint8_t rx[3];
    uint16_t result;
    if (!write_cmd(SCD41_CMD_STOP_PERIODIC)) {
        return false;
    }
    HAL_Delay(500);
    if (!write_cmd_arg(SCD41_CMD_FRC, target_ppm)) {
        (void)write_cmd(SCD41_CMD_START_PERIODIC);
        return false;
    }
    HAL_Delay(400);
    if (HAL_I2C_Master_Receive(&hi2c1, SCD41_ADDR, rx, 3, 200) != HAL_OK) {
        (void)write_cmd(SCD41_CMD_START_PERIODIC);
        return false;
    }
    (void)write_cmd(SCD41_CMD_START_PERIODIC);
    if (crc8(rx, 2) != rx[2]) {
        return false;
    }
    result = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    return result != 0xFFFFu;
}
