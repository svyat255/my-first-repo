#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <Arduino.h>

/* 16×2 через PCF8574. Нет ACK на 0x27 и 0x3F — дальше lcd_show молчит. */

/** lcd_init — I2C GPIO21/22, найти backpack, стартовый текст CO2 monitor / boot... */
void lcd_init();
/** lcd_show — две строки Arduino String (портал Wi‑Fi, ошибки). */
void lcd_show(const String &line1, const String &line2);
/** lcd_show_raw — две C-строки, выровненные в 16 символов. */
void lcd_show_raw(const char *l1, const char *l2);

#endif
