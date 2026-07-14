#ifndef SETTINGS_H
#define SETTINGS_H

#include "alert_logic.h"

bool settings_load(alert_settings_t *s);
void settings_save(const alert_settings_t *s);

#endif
