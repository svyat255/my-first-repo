#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <Arduino.h>

/** ntp_begin — configTime с серверами NTP и смещением tz_hours·3600. */
void ntp_begin(int8_t tz_hours);
/** ntp_ready — true, когда Unix-время уже не «1970» (порог 1.7e9). */
bool ntp_ready();
/** ntp_epoch_utc — Unix UTC для поля epoch; 0 пока нет NTP (STM32 нуль в DS3231 не пишет). */
uint32_t ntp_epoch_utc();
/** ntp_hhmm — локальные часы ESP «HH:MM» или «--:--»; LCD берёт время со STM32. */
String ntp_hhmm();

#endif
