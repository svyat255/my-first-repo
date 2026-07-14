#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>

#define SSD1306_I2C_ADDR    0x3C

bool ssd1306_init(void);
void ssd1306_clear(void);
void ssd1306_set_cursor(uint8_t x, uint8_t y);
void ssd1306_write_str(const char *str);
void ssd1306_write_int(int32_t val);
void ssd1306_display(void);

#endif
