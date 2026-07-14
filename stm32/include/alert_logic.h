#ifndef ALERT_LOGIC_H
#define ALERT_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ALERT_NORMAL = 0,
    ALERT_WARNING = 1,
    ALERT_CRITICAL = 2
} alert_level_t;

typedef struct {
    uint16_t co2_warn;
    uint16_t co2_crit;
    uint8_t quiet_start;
    uint8_t quiet_end;
    uint8_t time_hour;
    uint8_t time_min;
    bool time_valid;
    bool quiet_remote;
    bool manual_mute;
} alert_settings_t;

void alert_init(void);
void alert_load_defaults(alert_settings_t *s);
void alert_apply_remote(alert_settings_t *s, const alert_settings_t *remote);
alert_level_t alert_get_level(uint16_t co2);
bool alert_audio_allowed(const alert_settings_t *s);
bool alert_is_quiet_hours(const alert_settings_t *s);
void alert_update_time(alert_settings_t *s, uint8_t hour, uint8_t min);
void alert_process(alert_settings_t *s, uint16_t co2, alert_level_t level);
void alert_test_beep(void);

#endif
