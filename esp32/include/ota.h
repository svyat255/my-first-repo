#ifndef OTA_H
#define OTA_H

/* ArduinoOTA по Wi‑Fi. Загрузка .bin из браузера — POST /update в web_ui. */

/** ota_begin — ArduinoOTA под HOSTNAME; вызвать один раз после появления STA. */
void ota_begin();
/** ota_poll — отдать библиотеке квант на приём кусков прошивки (каждый loop). */
void ota_poll();

#endif
