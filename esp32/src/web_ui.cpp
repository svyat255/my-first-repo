#include "web_ui.h"
#include "config.h"
#include "uart_bridge.h"
#include "ntp_sync.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>

/** server — HTTP :80; пишет web_begin (on/begin), читает web_poll handleClient. */
static WebServer server(80);
/** s_settings — указатель на g_settings из main; пишет web_begin, читают handlers. */
static AppSettings *s_settings = nullptr;
/** s_tel — указатель на g_tel из main; пишет web_begin, читают status и петля UART. */
static Telemetry *s_tel = nullptr;
/**
 * s_update_ok — POST /update прошёл Update.begin и Update.end без ошибки.
 * Пишет upload-колбэк, читает ответный handler (рестарт только если true).
 */
static bool s_update_ok = false;

/** INDEX_HTML — страница в flash (PROGMEM), не в DRAM. Кнопки бьют POST /api/*. */
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="ru"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CO2 monitor</title>
<style>
body{font-family:sans-serif;max-width:42rem;margin:1rem auto;padding:0 1rem;background:#111;color:#eee}
h1{font-size:1.3rem} .card{background:#1c1c1c;padding:1rem;border-radius:8px;margin:1rem 0}
.big{font-size:2.4rem} label{display:block;margin:.6rem 0} input{padding:.3rem}
button{padding:.5rem .8rem;margin:.2rem} .ok{color:#8f8}.warn{color:#fc3}.alarm{color:#f66}
</style></head><body>
<h1>CO2 monitor</h1>
<div class="card">
  <div id="co2" class="big">—</div>
  <div id="meta">T — · RH — · —</div>
  <div id="st">ожидание STM32…</div>
  <div id="uart" style="margin-top:.6rem;font-size:.85rem;color:#aaa"></div>
</div>
<div class="card">
  <button onclick="post('/api/beep')">Тест звука</button>
  <button onclick="post('/api/mute')">Mute 60 мин</button>
  <button onclick="post('/api/unmute')">Unmute</button>
  <button onclick="uartLoop()">Петля UART ESP32</button>
  <div id="loop" style="margin-top:.4rem;font-size:.85rem;color:#aaa"></div>
</div>
<div class="card">
  <h2>Настройки</h2>
  <label>Warning ppm <input id="warn" type="number"></label>
  <label>Alarm ppm <input id="crit" type="number"></label>
  <label>Тихие часы с <input id="qs" type="number" min="0" max="23"> до <input id="qe" type="number" min="0" max="23"></label>
  <label>Часовой пояс UTC<input id="tz" type="number" min="-12" max="14"></label>
  <label>Telegram bot token <input id="tgt" type="password"></label>
  <label>Telegram chat id <input id="tgc"></label>
  <label>ntfy topic <input id="ntfy"></label>
  <button onclick="save()">Сохранить</button>
</div>
<div class="card">
  <h2>Калибровка SCD41</h2>
  <p>Только на свежем воздухе (~400 ppm), 3 минуты.</p>
  <button onclick="if(confirm('FRC 400 ppm?'))post('/api/calibrate')">FRC 400 ppm</button>
</div>
<div class="card">
  <h2>OTA</h2>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware">
    <button type="submit">Прошить ESP32</button>
  </form>
  <div id="ver"></div>
</div>
<script>
async function refresh(){
  const r=await fetch('/api/status'); const j=await r.json();
  const el=document.getElementById('co2');
  const noRx=j.rx_bytes===0;
  el.textContent=noRx?'нет связи':(j.valid?j.co2+' ppm':'—');
  el.className='big '+(j.alert==2?'alarm':j.alert==1?'warn':'ok');
  document.getElementById('meta').textContent=
    j.temp+'C · '+j.rh+'% · '+j.time+(j.quiet?' · тихо':'');
  let st;
  if(noRx){
    st='STM32 не отвечает';
  }else if(j.stale){
    if(!j.valid && j.boot) st='STM32 boot, нет JSON';
    else if(!j.valid) st='не JSON с полем co2';
    else st='тишина >10 с';
  }else{
    st='STM32 OK';
  }
  document.getElementById('st').textContent=st+' · WiFi '+j.ip+' · v'+j.version;
  let u='GPIO16/17: '+j.rx_gpio16+' байт · GPIO25/26: '+j.rx_gpio25+' байт';
  if(j.swap) u+=' · UART2 RX↔TX';
  if(j.last) u+=' · последняя: '+j.last;
  if(j.stale){
    if(noRx) u+=' · 0 байт на обоих портах: STM32 молчит или провод не на 16 и не на 25. Смотрите PC13, BOOT0=0, PA9≈3.3В, общий GND. Для WROVER переставьте PA9→GPIO25, PA10←GPIO26.';
    else if(!j.valid && j.boot) u+=' · STM32 загрузился, JSON телеметрии ещё нет';
    else if(!j.valid) u+=' · байты есть, но это не JSON с полем co2';
    else u+=' · пакеты были, потом тишина >10 с';
  }
  document.getElementById('uart').textContent=u;
}
async function loadSet(){
  const r=await fetch('/api/settings'); const j=await r.json();
  warn.value=j.co2_warn; crit.value=j.co2_crit; qs.value=j.quiet_start; qe.value=j.quiet_end;
  tz.value=j.tz; tgt.value=j.telegram_token; tgc.value=j.telegram_chat; ntfy.value=j.ntfy_topic;
  ver.textContent='firmware '+j.version;
}
async function post(u){ await fetch(u,{method:'POST'}); }
async function uartLoop(){
  const r=await fetch('/api/uart_loop',{method:'POST'}); const j=await r.json();
  document.getElementById('loop').textContent=
    'Петля: GPIO16/17 +'+j.delta16+' · GPIO25/26 +'+j.delta25+
    ((j.delta16||j.delta25)?' — UART ESP32 жив, замкните RX-TX этой пары':' — замкните 16 с 17 или 25 с 26 и нажмите ещё раз. Ноль = не те пины.');
}
async function save(){
  await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({co2_warn:+warn.value,co2_crit:+crit.value,quiet_start:+qs.value,
      quiet_end:+qe.value,tz:+tz.value,telegram_token:tgt.value,telegram_chat:tgc.value,ntfy_topic:ntfy.value})});
  alert('Сохранено');
}
loadSet(); setInterval(refresh,2000); refresh();
</script></body></html>
)HTML";

/**
 * jsonEscape — экранировать " и \\ в токене для GET /api/settings.
 * Без этого кавычка в bot token ломает JSON на странице.
 */
static String jsonEscape(const String &in)
{
    /* Экранировать " и \\ ; управляющие символы → '?', иначе JSON страницы сломается. */
    String o;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if ((unsigned char)c < 32) {
            o += '?';
            continue;
        }
        if (c == '"' || c == '\\') {
            o += '\\';
        }
        o += c;
    }
    return o;
}

/**
 * jsonInt — целое после "ключ": в теле POST. Нет ключа → def (оставить старое).
 *
 * Парсер кустарный: toInt() ест цифры с позиции ключа, хвост JSON не мешает.
 */
static int jsonInt(const String &body, const char *key, int def)
{
    String pat = String("\"") + key + "\":";
    int i = body.indexOf(pat);
    if (i < 0) {
        return def;
    }
    return body.substring(i + pat.length()).toInt();
}

/**
 * jsonStr — строка в кавычках после "ключ":". Нет закрывающей кавычки → "".
 */
static String jsonStr(const String &body, const char *key)
{
    String pat = String("\"") + key + "\":\"";
    int i = body.indexOf(pat);
    if (i < 0) {
        return "";
    }
    int a = i + pat.length();
    int e = body.indexOf('"', a);
    if (e < 0) {
        return "";
    }
    return body.substring(a, e);
}

/**
 * web_begin — повесить обработчики HTTP :80 на те же AppSettings/Telemetry, что в main.
 *
 * /              HTML из PROGMEM (кнопки → fetch POST)
 * GET  /api/status     ppm, stale, IP, версия — страница опрашивает каждые 2 с
 * GET  /api/settings   форма порогов и токенов
 * POST /api/settings   NVS + ntp_begin + UART на STM32
 * POST /api/beep|mute|unmute|calibrate  JSON cmd на STM32
 * POST /update         .bin в flash ESP32; reboot только после успешного Update.end
 *
 * Указатели s_settings/s_tel обязаны жить весь run (это глобалы main).
 */
void web_begin(AppSettings &s, Telemetry &t)
{
    s_settings = &s;
    s_tel = &t;

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    /* Живые ppm/T/RH с STM32 + счётчики UART, чтобы отличить обрыв от мусора. */
    server.on("/api/status", HTTP_GET, []() {
        String last = uart_bridge_last_raw();
        if (last.length() > 80) {
            last = last.substring(0, 80);
        }
        String j = "{\"co2\":";
        j += s_tel->co2;
        j += ",\"temp\":";
        j += String(s_tel->temp, 1);
        j += ",\"rh\":";
        j += s_tel->rh;
        j += ",\"alert\":";
        j += s_tel->alert;
        j += ",\"quiet\":";
        j += s_tel->quiet ? "true" : "false";
        j += ",\"time\":\"";
        j += jsonEscape(s_tel->time);
        j += "\",\"stale\":";
        j += uart_bridge_stale(*s_tel) ? "true" : "false";
        j += ",\"valid\":";
        j += s_tel->valid ? "true" : "false";
        j += ",\"boot\":";
        j += uart_bridge_boot_seen() ? "true" : "false";
        j += ",\"rx_bytes\":";
        j += uart_bridge_rx_bytes();
        j += ",\"rx_gpio16\":";
        j += uart_bridge_rx_gpio16();
        j += ",\"rx_gpio25\":";
        j += uart_bridge_rx_gpio25();
        j += ",\"swap\":";
        j += uart_bridge_uart2_swapped() ? "true" : "false";
        j += ",\"rx_lines\":";
        j += uart_bridge_rx_lines();
        j += ",\"last\":\"";
        j += jsonEscape(last);
        j += "\",\"ip\":\"";
        j += WiFi.localIP().toString();
        j += "\",\"version\":\"";
        j += FW_VERSION;
        j += "\"}";
        server.send(200, "application/json", j);
    });

    server.on("/api/settings", HTTP_GET, []() { /* форма: пороги, tz, токены */
        String j = "{\"co2_warn\":";
        j += s_settings->co2_warn;
        j += ",\"co2_crit\":";
        j += s_settings->co2_crit;
        j += ",\"quiet_start\":";
        j += s_settings->quiet_start;
        j += ",\"quiet_end\":";
        j += s_settings->quiet_end;
        j += ",\"tz\":";
        j += s_settings->tz_hours;
        j += ",\"telegram_token\":\"";
        j += jsonEscape(s_settings->telegram_token);
        j += "\",\"telegram_chat\":\"";
        j += jsonEscape(s_settings->telegram_chat);
        j += "\",\"ntfy_topic\":\"";
        j += jsonEscape(s_settings->ntfy_topic);
        j += "\",\"version\":\"";
        j += FW_VERSION;
        j += "\"}";
        server.send(200, "application/json", j);
    });

    server.on("/api/settings", HTTP_POST, []() {
        /* Сохранить в NVS и сразу отдать STM32, чтобы пороги совпали. */
        String body = server.arg("plain");
        if (body.length() == 0) {
            body = server.arg("body");
        }
        s_settings->co2_warn = (uint16_t)jsonInt(body, "co2_warn", s_settings->co2_warn);
        s_settings->co2_crit = (uint16_t)jsonInt(body, "co2_crit", s_settings->co2_crit);
        s_settings->quiet_start = (uint8_t)jsonInt(body, "quiet_start", s_settings->quiet_start);
        s_settings->quiet_end = (uint8_t)jsonInt(body, "quiet_end", s_settings->quiet_end);
        s_settings->tz_hours = (int8_t)jsonInt(body, "tz", s_settings->tz_hours);
        String tok = jsonStr(body, "telegram_token");
        String chat = jsonStr(body, "telegram_chat");
        String ntfy = jsonStr(body, "ntfy_topic");
        if (body.indexOf("telegram_token") >= 0) {
            s_settings->telegram_token = tok;
        }
        if (body.indexOf("telegram_chat") >= 0) {
            s_settings->telegram_chat = chat;
        }
        if (body.indexOf("ntfy_topic") >= 0) {
            s_settings->ntfy_topic = ntfy;
        }
        settings_save(*s_settings);
        ntp_begin(s_settings->tz_hours);
        uart_bridge_send_settings(*s_settings, ntp_epoch_utc());
        server.send(200, "application/json", "{\"ok\":true}");
    });

    /* Команды на STM32: JSON с полем cmd (см. docs/uart_protocol.md). */
    server.on("/api/uart_loop", HTTP_POST, []() {
        uint32_t d16 = 0;
        uint32_t d25 = 0;
        uart_bridge_loopback_probe(*s_tel, &d16, &d25);
        String j = "{\"delta16\":";
        j += d16;
        j += ",\"delta25\":";
        j += d25;
        j += "}";
        server.send(200, "application/json", j);
    });
    server.on("/api/beep", HTTP_POST, []() {
        uart_bridge_send_cmd("{\"cmd\":\"beep\"}");
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/mute", HTTP_POST, []() {
        uart_bridge_send_cmd("{\"cmd\":\"mute\",\"minutes\":60}");
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/unmute", HTTP_POST, []() {
        uart_bridge_send_cmd("{\"cmd\":\"unmute\"}");
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/calibrate", HTTP_POST, []() {
        uart_bridge_send_cmd("{\"cmd\":\"frc_calibrate\"}");
        server.send(200, "application/json", "{\"ok\":true}");
    });

    /* Загрузка .bin из браузера. Рестарт только после успешного Update.end. ArduinoOTA — ota.cpp. */
    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        if (s_update_ok) {
            server.send(200, "text/plain", "OK");
            delay(500);
            ESP.restart();
        } else {
            server.send(500, "text/plain", "FAIL");
        }
    }, []() {
        HTTPUpload &up = server.upload();
        if (up.status == UPLOAD_FILE_START) {
            s_update_ok = false;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                Update.printError(Serial);
            }
        } else if (up.status == UPLOAD_FILE_END) {
            s_update_ok = Update.end(true) && !Update.hasError();
            if (!s_update_ok) {
                Update.printError(Serial);
            }
        }
    });

    server.begin();
}

/**
 * web_poll — разобрать один HTTP-запрос, если клиент уже подключился.
 *
 * Без этого в loop() POST с телефона зависнет: сервер не увидит сокет.
 */
void web_poll()
{
    server.handleClient(); /* иначе POST с телефона зависнет в очереди */
}
