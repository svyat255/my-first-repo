#ifndef CCS811_H
#define CCS811_H

#include <stdint.h>
#include <stdbool.h>

#define CCS811_I2C_ADDR     0x5A

bool ccs811_init(void);
bool ccs811_read(uint16_t *eco2, uint16_t *tvoc);
bool ccs811_set_env_data(float temp_c, float humidity);
void ccs811_reset_baseline(void);
bool ccs811_data_ready(void);

#endif
