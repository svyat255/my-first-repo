#ifndef CONFIG_H
#define CONFIG_H

/* I2C pins */
#define I2C_SCL_PIN         GPIO_PIN_6
#define I2C_SCL_PORT        GPIOB
#define I2C_SDA_PIN         GPIO_PIN_7
#define I2C_SDA_PORT        GPIOB

/* UART ESP8266 */
#define ESP_UART            USART1

/* Buzzer TIM1 CH1 on PA8 */
#define BUZZER_PIN          GPIO_PIN_8
#define BUZZER_PORT         GPIOA

/* Button */
#define BUTTON_PIN          GPIO_PIN_0
#define BUTTON_PORT         GPIOB

/* Default thresholds ppm */
#define DEFAULT_CO2_WARN    800
#define DEFAULT_CO2_CRIT    1200

/* Default quiet hours */
#define DEFAULT_QUIET_START 22
#define DEFAULT_QUIET_END   7

/* Flash settings page (last 1KB of 64KB flash) */
#define SETTINGS_FLASH_ADDR 0x0800FC00

/* Measurement interval ms */
#define MEASURE_INTERVAL_MS 5000

/* UART line buffer */
#define UART_LINE_MAX       256

#endif
