#ifndef SCD41_H
#define SCD41_H

#include <stdint.h>
#include <stdbool.h>

/* Sensirion SCD41, I2C1 PB6/PB7, адрес 0x62. temp_x10 — десятые °C. */

/**
 * scd41_init — stop periodic + пауза 500 мс + start periodic.
 * @return true, если Start ушёл по I2C1. Ловушка: первые точки ещё мусор — warmup в alert_fsm.
 */
bool scd41_init(void);

/**
 * scd41_read — снять одну точку CO2 / T / RH, если data_ready.
 * @param co2_ppm   выход, ppm
 * @param temp_x10  выход, десятые °C (231 = 23.1)
 * @param rh        выход, %
 * @return          false = не ready / CRC / ppm вне 1…5000; прошлые значения в main не трогать
 * Ловушка: только I2C1 (hi2c1), не hi2c2.
 */
bool scd41_read(uint16_t *co2_ppm, int16_t *temp_x10, uint8_t *rh);

/**
 * scd41_frc_calibrate — Forced Recalibration: «считай воздух за target ppm».
 * @param target_ppm  обычно 400 (улица). @return true, если CRC ок и не 0xFFFF
 * Ловушка: блокирует ~1 с (Delay+I2C). Только из main, не из IRQ. В комнате испортит шкалу.
 */
bool scd41_frc_calibrate(uint16_t target_ppm);

/**
 * scd41_reinit_periodic — повторный start после сбоя шины.
 * Ловушка: внутри снова Delay 500 мс — не из IRQ.
 */
void scd41_reinit_periodic(void);

#endif
