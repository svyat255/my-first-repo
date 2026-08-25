#ifndef NOTIFY_H
#define NOTIFY_H

#include "app_settings.h"
#include "uart_bridge.h"

/* Один пуш Telegram/ntfy при входе в Alarm (alert становится 2). */

/**
 * notify_on_alert — пуш только на фронте входа в Alarm.
 * Сразу выход, если телеметрия невалидна или stale. poll UART до и после HTTPS.
 */
void notify_on_alert(const AppSettings &s, Telemetry &t, int prev_alert);

#endif
