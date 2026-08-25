#ifndef WEB_UI_H
#define WEB_UI_H

#include "app_settings.h"
#include "uart_bridge.h"

/* HTTP :80 — страница, JSON API, POST /update. web_poll() каждый loop. */

/** web_begin — повесить обработчики :80 на живые s и t из main (указатели на весь run). */
void web_begin(AppSettings &s, Telemetry &t);
/** web_poll — разобрать один HTTP-запрос, если клиент уже подключился. */
void web_poll();

#endif
