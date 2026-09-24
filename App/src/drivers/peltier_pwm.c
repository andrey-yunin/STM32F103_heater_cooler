/*
 * peltier_pwm.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Драйвер двух PWM-каналов Пельтье.
 * Полученный duty только преобразуется в значение CCR таймера.
 *
 */

#include "drivers/peltier_pwm.h"
#include "app_safety.h"

#include "main.h"

/* Таймеры объявлены CubeMX в main.c. */
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

/* Оба канала используют TIM_CHANNEL_1. */
#define PELTIER_PWM_TIMER_CHANNEL TIM_CHANNEL_1

/* Таблица таймеров по логическому номеру канала Пельтье. */
static TIM_HandleTypeDef *const peltier_timers[PELTIER_PWM_CHANNEL_COUNT] = {
		&htim2, &htim3 };

/* Последнее принятое значение duty каждого канала. */
static uint8_t peltier_duty[PELTIER_PWM_CHANNEL_COUNT];

/* Проверяет логический номер канала до обращения к таблице таймеров. */
static bool PeltierPwm_IsValid(uint8_t channel) {
	return channel < PELTIER_PWM_CHANNEL_COUNT;
}

/*
 * Преобразует duty в значение регистра сравнения таймера.
 *
 * CubeMX настроил оба таймера с периодом 15999.
 * Поэтому duty=0 даёт CCR=0, а duty=100 — CCR=15999.
 */
static void PeltierPwm_ApplyDuty(uint8_t channel, uint8_t duty_percent) {
	TIM_HandleTypeDef *timer;
	uint32_t period;
	uint32_t compare;

	timer = peltier_timers[channel];
	period = timer->Init.Period;

	/*
	 * Значение compare рассчитывается в пределах периода таймера.
	 * Промежуточное значение duty формирует соответствующую
	 * длительность активного уровня PWM.
	 */
	compare = ((period + 1U) * duty_percent) / 100U;

	/* Не допускаем выхода CCR за предел допустимого периода. */
	if (compare > period) {
		compare = period;
	}

	__HAL_TIM_SET_COMPARE(timer, PELTIER_PWM_TIMER_CHANNEL, compare);
}

bool PeltierPwm_Start(void) {
	/*
	 * Безопасный запуск начинается с нулевого duty.
	 * Фактическую мощность после запуска задаёт отдельная команда
	 * SET_POWER через PeltierPwm_SetDuty().
	 */
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	/* После начала подготовки reset запуск таймеров запрещён. */
	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	/* Оба таймера запускаются с нулевым duty. */
	PeltierPwm_AllOff();

	if (HAL_TIM_PWM_Start(&htim2,
	PELTIER_PWM_TIMER_CHANNEL) != HAL_OK) {
		__DSB();
		__set_PRIMASK(saved_primask);
		return false;
	}

	if (HAL_TIM_PWM_Start(&htim3,
	PELTIER_PWM_TIMER_CHANNEL) != HAL_OK) {
		/* Сохраняем отключение при частичном запуске. */
		(void) HAL_TIM_PWM_Stop(&htim2, PELTIER_PWM_TIMER_CHANNEL);
		PeltierPwm_AllOff();

		__DSB();
		__set_PRIMASK(saved_primask);
		return false;
	}

	__DSB();
	__set_PRIMASK(saved_primask);
	return true;

}

// --- Остановка PWM, включая неполный startup ---

/*
 * Обнуляет сохранённый duty обоих каналов.
 * К регистрам обращается только после назначения ожидаемого Instance.
 * До назначения Instance соответствующий таймер этим драйвером
 * ещё не запускался.
 *
 * RTOS, ожидания и CAN не используются.
 * Функция не запрещает последующий запуск доменной задачей.
 */
void PeltierPwm_Stop(void) {
	// --- Сброс программного состояния драйвера ---

	for (uint8_t channel = 0U; channel < PELTIER_PWM_CHANNEL_COUNT; channel++) {
		peltier_duty[channel] = PELTIER_PWM_DUTY_MIN_PERCENT;
	}

	// --- Отключение первого канала ---

	/*
	 * Instance назначается раньше завершения HAL init.
	 * Поэтому обеспечиваем тактирование перед доступом к регистрам.
	 */
	if (htim2.Instance == TIM2) {
		__HAL_RCC_TIM2_CLK_ENABLE();
		__HAL_TIM_SET_COMPARE(&htim2, PELTIER_PWM_TIMER_CHANNEL, 0U);
		(void) HAL_TIM_PWM_Stop(&htim2, PELTIER_PWM_TIMER_CHANNEL);
	}

	// --- Отключение второго канала ---

	/* Первый таймер может быть готов, когда второй ещё не настроен. */
	if (htim3.Instance == TIM3) {
		__HAL_RCC_TIM3_CLK_ENABLE();
		__HAL_TIM_SET_COMPARE(&htim3, PELTIER_PWM_TIMER_CHANNEL, 0U);
		(void) HAL_TIM_PWM_Stop(&htim3, PELTIER_PWM_TIMER_CHANNEL);
	}
}

bool PeltierPwm_SetDuty(uint8_t channel, uint8_t duty_percent) {
	/* Некорректный канал или duty не передаются в аппаратный слой. */
	if (!PeltierPwm_IsValid(channel)
			|| (duty_percent > PELTIER_PWM_DUTY_MAX_PERCENT)) {
		return false;
	}

	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	/*
	 * Ненулевой duty после подготовки reset запрещён.
	 * Нулевой duty разрешён для RES и остальных путей отключения.
	 */
	if ((duty_percent != 0U) && AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	peltier_duty[channel] = duty_percent;
	PeltierPwm_ApplyDuty(channel, duty_percent);

	__DSB();
	__set_PRIMASK(saved_primask);
	return true;

}

// --- Обнуление PWM без остановки таймеров ---

/*
 * Используется при штатном запуске после инициализации TIM2/TIM3.
 * Обнуляет сохранённый duty и CCR обоих каналов.
 * Для раннего аварийного отключения используется PeltierPwm_Stop().
 */
void PeltierPwm_AllOff(void) {
	for (uint8_t channel = 0U; channel < PELTIER_PWM_CHANNEL_COUNT; channel++) {
		peltier_duty[channel] = PELTIER_PWM_DUTY_MIN_PERCENT;
		PeltierPwm_ApplyDuty(channel, PELTIER_PWM_DUTY_MIN_PERCENT);
	}
}

uint8_t PeltierPwm_GetDuty(uint8_t channel) {
	/* Для ошибочного канала возвращаем безопасное значение duty=0. */
	if (!PeltierPwm_IsValid(channel)) {
		return PELTIER_PWM_DUTY_MIN_PERCENT;
	}

	return peltier_duty[channel];
}
