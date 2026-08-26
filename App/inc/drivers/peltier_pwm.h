/*
 * peltier_pwm.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Контракт управления двумя PWM-каналами Пельтье.
 *
 *
 */

#ifndef INC_DRIVERS_PELTIER_PWM_H_
#define INC_DRIVERS_PELTIER_PWM_H_

#include <stdbool.h>
#include <stdint.h>

// --- Локальные ограничения PWM ---

#define PELTIER_PWM_CHANNEL_COUNT 2U
#define PELTIER_PWM_DUTY_MIN_PERCENT 0U
#define PELTIER_PWM_DUTY_MAX_PERCENT 100U

// --- Управление PWM ---

/*
 * CubeMX уже инициализирует TIM2/TIM3.
 * Функция запускает только PWM-выходы драйвера.
   */
bool PeltierPwm_Start(void);

/*
 * Останавливает PWM и переводит оба канала в duty=0.
 */
void PeltierPwm_Stop(void);

/*
 * Устанавливает duty конкретного канала в диапазоне 0..100%.
 */
bool PeltierPwm_SetDuty(uint8_t channel,
						uint8_t duty_percent);

/*
 * Немедленно устанавливает duty=0 на обоих каналах.
 */
void PeltierPwm_AllOff(void);

/*
 * Возвращает последнее принятое драйвером значение duty.
 */
uint8_t PeltierPwm_GetDuty(uint8_t channel);


#endif /* INC_DRIVERS_PELTIER_PWM_H_ */
