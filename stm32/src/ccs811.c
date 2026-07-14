#include "ccs811.h"
#include "stm32f1xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

#define CCS811_REG_STATUS       0x00
#define CCS811_REG_ALG_DATA     0x02
#define CCS811_REG_RAW_DATA     0x03
#define CCS811_REG_ENV_DATA     0x05
#define CCS811_REG_MEAS_MODE    0x01
#define CCS811_REG_APP_START    0xF4
#define CCS811_REG_SW_RESET     0xFF

#define CCS811_MODE_1SEC        0x10

static bool ccs811_write(uint8_t reg, const uint8_t *data, uint16_t len)
{
    uint8_t buf[8];
    if (len > 7) return false;
    buf[0] = reg;
    for (uint16_t i = 0; i < len; i++) buf[i + 1] = data[i];
    return HAL_I2C_Master_Transmit(&hi2c1, CCS811_I2C_ADDR << 1, buf, len + 1, 100) == HAL_OK;
}

static bool ccs811_read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Transmit(&hi2c1, CCS811_I2C_ADDR << 1, &reg, 1, 100) != HAL_OK)
        return false;
    return HAL_I2C_Master_Receive(&hi2c1, CCS811_I2C_ADDR << 1, data, len, 100) == HAL_OK;
}

bool ccs811_init(void)
{
    uint8_t status;
    if (!ccs811_read_reg(CCS811_REG_STATUS, &status, 1))
        return false;

    uint8_t app = 0x01;
    if (!ccs811_write(CCS811_REG_APP_START, &app, 1))
        return false;

    HAL_Delay(100);

    uint8_t mode = CCS811_MODE_1SEC;
    if (!ccs811_write(CCS811_REG_MEAS_MODE, &mode, 1))
        return false;

    HAL_Delay(100);
    return true;
}

bool ccs811_data_ready(void)
{
    uint8_t status;
    if (!ccs811_read_reg(CCS811_REG_STATUS, &status, 1))
        return false;
    return (status & 0x08) != 0;
}

bool ccs811_read(uint16_t *eco2, uint16_t *tvoc)
{
    uint8_t data[4];
    if (!ccs811_read_reg(CCS811_REG_ALG_DATA, data, 4))
        return false;

  uint16_t e = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t t = (uint16_t)((data[2] << 8) | data[3]);

    if (e == 0xFFFF || t == 0xFFFF)
        return false;

    *eco2 = e;
    *tvoc = t;
    return true;
}

bool ccs811_set_env_data(float temp_c, float humidity)
{
    int32_t temp_fix = (int32_t)((temp_c + 25.0f) * 512.0f);
    uint16_t hum_fix = (uint16_t)(humidity * 512.0f);

    uint8_t buf[4];
    buf[0] = (uint8_t)((hum_fix >> 8) & 0xFF);
    buf[1] = (uint8_t)(hum_fix & 0xFF);
    buf[2] = (uint8_t)((temp_fix >> 8) & 0xFF);
    buf[3] = (uint8_t)(temp_fix & 0xFF);
    return ccs811_write(CCS811_REG_ENV_DATA, buf, 4);
}

void ccs811_reset_baseline(void)
{
    uint8_t reset = 0x11;
    ccs811_write(CCS811_REG_SW_RESET, &reset, 1);
    HAL_Delay(100);
}
