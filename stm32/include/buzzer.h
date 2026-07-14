#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);
void buzzer_on(uint16_t freq_hz);
void buzzer_off(void);
void buzzer_beep(uint16_t freq_hz, uint16_t duration_ms);
void buzzer_pattern_warning(void);
void buzzer_pattern_critical(void);

#endif
