#ifndef CLOCK_FALLBACK_H
#define CLOCK_FALLBACK_H

#include <stdbool.h>

/**
 * clock_try_hsi_pll — запасной такт, если HSE 8 МГц на Blue Pill мёртвый.
 *
 * CubeMX Generate сотрёт тело SystemClock_Config; эту функцию не тронет.
 * Сначала явно гасит PLL (RCC_PLL_OFF), затем HSI/2 × 16 = 64 МГц.
 *
 * @return true, если оба OscConfig прошли; false → вызывающей стороне Error_Handler
 *
 * Ловушка: вызывать только из SystemClock_Config после провала HSE+PLL,
 * до HAL_RCC_ClockConfig. SYSCLK в этот момент ещё HSI с reset.
 */
bool clock_try_hsi_pll(void);

#endif
