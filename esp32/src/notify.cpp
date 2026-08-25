#include "notify.h"
#include "uart_bridge.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/**
 * http_get_or_post — один HTTP(S) запрос, ошибки глотаем.
 *
 * Нет Wi‑Fi — сразу выход. HTTPS: setInsecure() (на устройстве нет CA-хранилища).
 * ntfy: POST text/plain. Telegram: GET sendMessage в URL.
 *
 * @param url    полный URL
 * @param body   тело POST (для GET можно пусто)
 * @param post   true = POST, false = GET
 * @param https  true = WiFiClientSecure
 */
static void http_get_or_post(const String &url, const String &body, bool post, bool https)
{
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    if (https) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        if (!http.begin(client, url)) {
            return;
        }
        if (post) {
            http.addHeader("Content-Type", "text/plain; charset=utf-8");
            http.POST(body);
        } else {
            http.GET();
        }
        http.end();
        return;
    }
    HTTPClient http;
    if (!http.begin(url)) {
        return;
    }
    if (post) {
        http.addHeader("Content-Type", "text/plain; charset=utf-8");
        http.POST(body);
    } else {
        http.GET();
    }
    http.end();
}

/**
 * notify_on_alert — пуш в Telegram/ntfy только на входе в Alarm.
 *
 * Сразу выход, если !t.valid или stale: битый/старый JSON не должен слать «ALARM 0 ppm».
 * poll UART в начале и после HTTPS: длинный TLS иначе переполняет FIFO STM32.
 * Условие пуша: t.alert ≥ 2 и prev_alert < 2. Пока держится Alarm — тишина.
 * Спад в Warning и новый скачок — снова сообщение.
 * Пустые token/topic — соответствующий канал пропускаем.
 *
 * @param s           NVS (токены)
 * @param t           живая телеметрия main (poll пишет сюда же)
 * @param prev_alert  прошлое значение из main (ещё не обновлённое)
 */
void notify_on_alert(const AppSettings &s, Telemetry &t, int prev_alert)
{
    uart_bridge_poll(t);
    if (!t.valid || uart_bridge_stale(t)) {
        return;
    }
    if (t.alert < 2 || prev_alert >= 2) {
        return;
    }
    String msg = "CO2 ALARM: ";
    msg += t.co2;
    msg += " ppm";
    if (s.ntfy_topic.length() > 0) {
        http_get_or_post("https://ntfy.sh/" + s.ntfy_topic, msg, true, true);
    }
    if (s.telegram_token.length() > 0 && s.telegram_chat.length() > 0) {
        String url = "https://api.telegram.org/bot";
        url += s.telegram_token;
        url += "/sendMessage?chat_id=";
        url += s.telegram_chat;
        url += "&text=";
        url += msg;
        url.replace(" ", "%20");
        http_get_or_post(url, "", false, true);
    }
    uart_bridge_poll(t);
}
