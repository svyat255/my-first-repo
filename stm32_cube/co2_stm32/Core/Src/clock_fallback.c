/*
 * Запасной системный такт Blue Pill: внутренний HSI вместо мёртвого HSE.
 * Вынесено из SystemClock_Config — CubeMX Generate это тело не сотрёт.
 */
#include "clock_fallback.h"
#include "main.h"

/**
 * clock_try_hsi_pll — см. clock_fallback.h: PLL OFF, затем HSI/2 × 16.
 *
 * @return true при успешном втором OscConfig
 *
 * Ловушка: не менять PLL source, пока PLL ещё ON — HAL вернёт ошибку
 * и снова уйдём в Error_Handler (нет UART, «STM32 не отвечает»).
 */
bool clock_try_hsi_pll(void)
{
    RCC_OscInitTypeDef osc = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    osc.HSEState = RCC_HSE_OFF;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL = RCC_PLL_MUL16;
    return HAL_RCC_OscConfig(&osc) == HAL_OK;
}
