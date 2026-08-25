#include "lcd_i2c.h"
#include "config.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/** lcd — драйвер 1602 после удачного ACK; nullptr если нет backpack. Пишет lcd_init, читают lcd_show*. */
static LiquidCrystal_I2C *lcd = nullptr;

/**
 * fit16 — ровно 16 символов: добить пробелами или обрезать.
 *
 * Без пробелов справа на 1602 остаётся хвост предыдущей длинной строки
 * (например IP после «OPEN WINDOW!»).
 */
static String fit16(const String &in)
{
    if (in.length() <= 16) {
        String out = in;
        while (out.length() < 16) {
            out += ' ';
        }
        return out;
    }
    return in.substring(0, 16);
}

/**
 * lcd_init — I2C GPIO21/22, найти backpack 0x27 или 0x3F, включить подсветку.
 *
 * Оба адреса молчат → lcd остаётся nullptr, все lcd_show тихо no-op
 * (веб и UART при этом работают). Стартовый текст: CO2 monitor / boot...
 */
void lcd_init()
{
    Wire.begin(LCD_SDA, LCD_SCL);
    delay(50);
    uint8_t addr = LCD_ADDR_PRIMARY;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) {
        addr = LCD_ADDR_FALLBACK;
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) {
            return;
        }
    }
    lcd = new LiquidCrystal_I2C(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print("CO2 monitor");
    lcd->setCursor(0, 1);
    lcd->print("boot...");
}

/**
 * lcd_show_raw — напечатать две строки, выровняв через fit16.
 *
 * @param l1,l2  C-строки, могут быть короче/длиннее 16
 */
void lcd_show_raw(const char *l1, const char *l2)
{
    if (lcd == nullptr) {
        return;
    }
    lcd->setCursor(0, 0);
    lcd->print(fit16(String(l1)));
    lcd->setCursor(0, 1);
    lcd->print(fit16(String(l2)));
}

/**
 * lcd_show — обёртка для Arduino String (WiFi setup, ошибки).
 */
void lcd_show(const String &line1, const String &line2)
{
    lcd_show_raw(line1.c_str(), line2.c_str());
}
