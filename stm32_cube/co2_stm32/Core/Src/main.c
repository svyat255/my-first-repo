/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "app_config.h"
#include "app_settings.h"
#include "scd41.h"
#include "rtc_ds3231.h"
#include "buzzer.h"
#include "alert_fsm.h"
#include "quiet_hours.h"
#include "esp_link.h"
#include "clock_fallback.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/** g_settings — живые пороги/тихие часы/tz в RAM. Пишет settings_load и esp_link_poll, читает суперцикл и alert_fsm. */
static app_settings_t g_settings;
/** g_last_measure — HAL_GetTick последнего опроса SCD41. Пишет суперцикл, читает он же (период 5 с). */
static uint32_t g_last_measure;
/** g_last_telem — HAL_GetTick последней JSON-строки на ESP32. Пишет/читает суперцикл. */
static uint32_t g_last_telem;
/** g_last_rtc — HAL_GetTick последнего ds3231_read. Пишет/читает суперцикл (не чаще 1 Гц). */
static uint32_t g_last_rtc;
/** g_co2 — последние удачные ppm. Пишет scd41_read-ветка, читает телеметрия. */
static uint16_t g_co2;
/** g_temp_x10 — десятые °C (231 = 23.1). Пишет scd41_read-ветка, читает телеметрия. */
static int16_t g_temp_x10;
/** g_rh — последняя влажность %. Пишет scd41_read-ветка, читает телеметрия. */
static uint8_t g_rh;
/** g_alert — 0/1/2 для JSON (warmup снаружи = 0). Пишет alert_fsm_update, читает телеметрия и process_sound. */
static alert_level_t g_alert;
/** g_rtc — последний снимок DS3231. Пишет ds3231_read раз в секунду, читают quiet_hours и телеметрия. */
static rtc_time_t g_rtc;
/** g_have_sample — true после первого удачного SCD41. Пишет суперцикл; сейчас только флаг «есть чем кормить JSON». */
static bool g_have_sample;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/*
 * main() — единственная «задача» STM32 (RTOS нет).
 *
 * Сначала CubeMX включает железо: GPIO, I2C1 (SCD41), I2C2 (DS3231),
 * TIM2 PWM на PA2 (зуммер), USART1 115200 на PA9/PA10 (ESP32).
 * Затем мы грузим настройки из flash и входим в while(1):
 *   1) забрать JSON с ESP32 (mute / beep / FRC / пороги / время);
 *   2) раз в 5 с прочитать SCD41 и обновить уровень тревоги;
 *   3) при необходимости поставить писк (сам PWM гасит buzzer_poll);
 *   4) телеметрия: 1 с первые 20 с, затем раз в 5 с (ESP32 мог пропустить boot).
 *
 * HAL_GetTick() — миллисекунды от SysTick, общий метроном периодов и mute.
 * Никогда не вызывайте scd41_frc_calibrate и длинный beep из UART-IRQ:
 * там HAL_Delay и I2C, это сломает приём байт.
 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  /* Печать в тот же USART1, что и JSON: в мониторе сразу видно, что прошивка жива. */
  {
    const char boot[] = "STM32 boot\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)boot, sizeof(boot) - 1u, 100);
  }
  settings_load(&g_settings);   /* пороги/тихие часы с последней страницы flash */
  buzzer_init();                /* PWM на PA2, скважность 0 = тишина */
  (void)ds3231_init();          /* false не фатален: quiet_hours тогда не глушит день */
  (void)scd41_init();           /* false — измерения будут пустые, цикл всё равно крутится */
  alert_fsm_init();             /* 60 с warmup: в JSON alert=0, звука по порогам нет */
  esp_link_init();              /* запускает приём 1 байта в прерывании USART1 */
  g_last_measure = 0;           /* 0 = «ещё не мерили» → первый опрос сразу */
  g_last_telem = 0;
  g_last_rtc = 0;
  g_co2 = 0;
  g_temp_x10 = 0;
  g_rh = 0;
  g_alert = ALERT_NORMAL;
  g_have_sample = false;
  memset(&g_rtc, 0, sizeof(g_rtc));
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /*
     * Один оборот суперцикла. Порядок важен:
     * сначала команды с веба (калибровка/beep), потом измерение и звук.
     * Телеметрию шлём даже если SCD41 не ответил — ESP32 рисует прошлые ppm.
     */
    {
      uint32_t now = HAL_GetTick();
      bool quiet;
      bool sound_ok;
      /** last_led — HAL_GetTick последнего Toggle PC13. Пишет/читает этот оборот суперцикла. */
      static uint32_t last_led;
      /** last_hello — HAL_GetTick последнего «STM32 boot». Пишет/читает этот оборот. */
      static uint32_t last_hello;

      /* Раз в 0.5 с моргнуть PC13: не мигает → STM32 не в суперцикле (BOOT0 / не прошит / Error_Handler). */
      if (last_led == 0u || (now - last_led) >= 500u) {
        last_led = now;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
      }

      /* Первые 8 с чаще шлём hello: ESP32 мог ещё быть в WiFiManager и пропустить boot. */
      if (now < 8000u && (last_hello == 0u || (now - last_hello) >= 250u)) {
        last_hello = now;
        esp_link_send_hello();
      }

      /* Разобрать одну готовую JSON-строку от ESP32 (настройки или "cmd"). */
      esp_link_poll(&g_settings);

      /* beep/FRC нельзя делать в IRQ UART — флаги снимаем здесь. */
      if (esp_link_take_beep()) {
        buzzer_test_beep(TEST_BEEP_MS); /* тест с HTML, игнор quiet/mute */
      }
      if (esp_link_take_frc()) {
        (void)scd41_frc_calibrate(400); /* «считай этот воздух за 400 ppm» */
        alert_fsm_reset_warmup();       /* снова 60 с без тревоги по порогам */
      }

      /* I2C2 раз в секунду: каждый оборот while(1) только убивает шину и DS3231. */
      if (g_last_rtc == 0u || (now - g_last_rtc) >= 1000u) {
        g_last_rtc = now;
        (void)ds3231_read(&g_rtc);
      }
      /* На LCD «тихо» = ночь или mute. Звук режется только ночью/mute/alerts off. */
      quiet = quiet_hours_active(&g_settings, &g_rtc)
           || alert_fsm_muted(now);
      sound_ok = alert_fsm_sound_allowed(&g_settings, quiet_hours_active(&g_settings, &g_rtc), now);

      if (g_last_measure == 0u || (now - g_last_measure) >= MEASURE_PERIOD_MS) {
        uint16_t co2;
        int16_t tx10;
        uint8_t rh;
        g_last_measure = now;
        if (scd41_read(&co2, &tx10, &rh)) {
          g_co2 = co2;
          g_temp_x10 = tx10;
          g_rh = rh;
          g_have_sample = true;
          g_alert = alert_fsm_update(co2, &g_settings, now);
        }
        /* Если SCD41 не готов — оставляем прошлые g_co2 и шлём их дальше. */
      }

      alert_fsm_process_sound(g_alert, sound_ok, now); /* ставит паттерн, не блокирует */
      buzzer_poll(); /* выключает PWM по таймеру; тройной alarm идёт отсюда */

      if (g_last_telem == 0u || (now - g_last_telem) >= ((now < 20000u) ? 1000u : TELEMETRY_PERIOD_MS)) {
        g_last_telem = now;
        esp_link_send_telemetry(g_co2, g_temp_x10, g_rh, g_alert, quiet, &g_rtc);
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    /* HSE часто мёртвый. Тело fallback в clock_fallback.c (Generate не сотрёт). */
    if (!clock_try_hsi_pll())
    {
      Error_Handler();
    }
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* После CubeMX Generate снова заменить Error_Handler() после OscConfig на clock_try_hsi_pll(). */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Быстрое мигание PC13: прошивка дошла до фатальной ошибки, не до суперцикла. */
  __disable_irq();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  {
    GPIO_InitTypeDef led = {0};
    led.Pin = GPIO_PIN_13;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &led);
  }
  while (1)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    for (volatile uint32_t i = 0; i < 200000u; i++) {
    }
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
