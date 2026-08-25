/*
 * Пассивный KY-006 на PA2 = TIM2_CH3 PWM.
 * CubeMX: PSC=71, ARR=499 → тик 1 МГц / 500 = 2 кГц (слышимый тон).
 * CCR=250 ≈ 50% — писк; CCR=0 — тишина (таймер при этом крутится).
 * Ни одна публичная функция не крутит Delay: длительность писка доигрывает poll().
 */
#include "buzzer.h"
#include "tim.h"

/** s_off_at — HAL_GetTick выключения PWM; 0 = уже тихо. Пишут pattern/test, читает poll. */
static uint32_t s_off_at;
/** s_alarm_left — сколько импульсов Alarm ещё поставить. Пишут pattern_alarm/off, читает poll. */
static uint8_t s_alarm_left;
/** s_next_pulse — HAL_GetTick старта следующего импульса Alarm. Пишут pattern_alarm и poll. */
static uint32_t s_next_pulse;

/**
 * set_pulse — скважность PWM канала 3 (0…ARR).
 *
 * @param pulse  0 = выкл, 250 = рабочий тон
 *
 * Ловушка: таймер должен уже крутиться (buzzer_init → PWM_Start).
 */
static void set_pulse(uint16_t pulse)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, pulse);
}

/**
 * buzzer_init — запустить PWM и сразу заглушить.
 *
 * Без Start таймер не тактирует пин. Pulse 0 обязателен, иначе после
 * Generate Code на PA2 сразу пойдёт 2 кГц (CubeMX ставит Pulse=250).
 */
void buzzer_init(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    set_pulse(0);
    s_off_at = 0;
    s_alarm_left = 0;
}

/**
 * buzzer_off — мгновенно тишина и отмена недоигранной тройки Alarm.
 */
void buzzer_off(void)
{
    set_pulse(0);
    s_off_at = 0;
    s_alarm_left = 0;
}

/**
 * buzzer_on — включить тон (не ставит таймер выключения — это делают pattern/test).
 */
void buzzer_on(void)
{
    set_pulse(250);
}

/**
 * buzzer_test_beep — писк с веба на duration_ms, игнорирует quiet/mute.
 *
 * Сбрасывает s_alarm_left: тест важнее недоигранного Alarm.
 * Выключение сделает poll(), когда наступит s_off_at.
 *
 * @param duration_ms  обычно TEST_BEEP_MS = 1000
 */
void buzzer_test_beep(uint32_t duration_ms)
{
    buzzer_on();
    s_off_at = HAL_GetTick() + duration_ms;
    s_alarm_left = 0;
}

/**
 * buzzer_pattern_warning — один импульс 200 мс (уровень Warning).
 *
 * Вызывается из alert_fsm не чаще раза в 5 мин. poll() снимет PWM.
 */
void buzzer_pattern_warning(void)
{
    buzzer_on();
    s_off_at = HAL_GetTick() + 200u;
    s_alarm_left = 0;
}

/**
 * buzzer_pattern_alarm — заказать три импульса; сами они идут в poll().
 *
 * Ритм: on 200 мс, следующий старт через 400 мс (пауза 200 мс между писками).
 * Не включает PWM сразу — первый импульс начнётся на ближайшем poll()
 * (обычно в том же обороте main, s_next_pulse = сейчас).
 */
void buzzer_pattern_alarm(void)
{
    s_alarm_left = 3;
    s_next_pulse = HAL_GetTick();
}

/**
 * buzzer_poll — обязательный шаг суперцикла: доиграть Alarm и выключить PWM.
 *
 * Без частого вызова тройка Alarm сольётся в один длинный тон или не доиграет.
 * Сравнение (int32_t)(now - deadline) корректно через переполнение tick.
 *
 * Вызывать каждый оборот while(1), даже если «сейчас тихо».
 */
void buzzer_poll(void)
{
    uint32_t now = HAL_GetTick();
    if (s_alarm_left > 0u) {
        if ((int32_t)(now - s_next_pulse) >= 0) {
            buzzer_on();
            s_off_at = now + 200u;
            s_next_pulse = now + 400u;
            s_alarm_left--;
        }
    }
    if (s_off_at != 0u && (int32_t)(now - s_off_at) >= 0) {
        set_pulse(0);
        s_off_at = 0;
    }
}
