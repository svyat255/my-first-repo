#include "ota.h"
#include <ArduinoOTA.h>
#include "config.h"

/**
 * ota_begin — ArduinoOTA под hostname co2-sensor.
 *
 * Это прошивка по Wi‑Fi из PlatformIO (`pio run -t upload`), не HTML /update.
 * Вызвать один раз после появления сети.
 */
void ota_begin()
{
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();
}

/**
 * ota_poll — отдать библиотеке квант времени на приём кусков прошивки.
 *
 * Без вызова каждый loop() OTA «зависнет» или не стартует.
 */
void ota_poll()
{
    ArduinoOTA.handle();
}
