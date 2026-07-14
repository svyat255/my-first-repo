#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "alert_logic.h"

typedef struct {
    uint16_t co2;
    uint16_t tvoc;
    alert_level_t alert;
} telemetry_t;

void uart_protocol_init(void);
void uart_protocol_poll(alert_settings_t *settings);
void uart_protocol_send_telemetry(const telemetry_t *t);
bool uart_protocol_pending_beep(void);

#endif
