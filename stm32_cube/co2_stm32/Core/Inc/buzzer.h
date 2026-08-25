#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>

/* KY-006, PA2, PWM ~2 кГц. buzzer_poll() вызывать из суперцикла. */

/**
 * buzzer_init — Start PWM TIM2_CH3 и сразу Pulse=0.
 * Ловушка: CubeMX ставит Pulse=250 — без обнуления PA2 запищит на старте.
 */
void buzzer_init(void);

/**
 * buzzer_off — мгновенная тишина и отмена недоигранной тройки Alarm.
 */
void buzzer_off(void);

/**
 * buzzer_on — тон 50% (не ставит таймер выключения).
 * Ловушка: без последующего s_off_at в pattern/test PWM останется включённым.
 */
void buzzer_on(void);

/**
 * buzzer_test_beep — писк с веба, игнор quiet/mute.
 * @param duration_ms  обычно TEST_BEEP_MS = 1000
 * Ловушка: сбрасывает тройку Alarm; выключает poll() по s_off_at.
 */
void buzzer_test_beep(uint32_t duration_ms);

/**
 * buzzer_poll — доиграть Alarm и выключить PWM по таймеру.
 * Вызывать каждый оборот while(1). Ловушка: без частого вызова тройка сольётся в гудок.
 */
void buzzer_poll(void);

/**
 * buzzer_pattern_warning — один импульс 200 мс (уровень Warning).
 * Ловушка: частоту (5 мин) держит alert_fsm, не эта функция.
 */
void buzzer_pattern_warning(void);

/**
 * buzzer_pattern_alarm — заказать три импульса; сами они идут в poll().
 * Ловушка: PWM не включает сразу — первый импульс на ближайшем poll().
 */
void buzzer_pattern_alarm(void);

#endif
