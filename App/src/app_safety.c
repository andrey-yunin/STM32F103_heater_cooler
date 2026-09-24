/*
 * app_safety.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Единая аппаратная процедура safe-off платы Heater/Cooler.
 *
 * Safe-off выполняется для тепловых выходов:
 *   1. останавливаются PWM-каналы Пельтье;
 *   2. GPIO-нагреватели переводятся в выключенное состояние.
 *
 * STB CAN-трансивера не изменяется, чтобы сохранить
 * связь для передачи fault и диагностической информации.
 *
 * Функция не принимает решений о температуре и не формирует CAN-ответы.
 */

#include "app_safety.h"

#include "drivers/heater_outputs.h"
#include "drivers/peltier_pwm.h"

#include "main.h"

/* Инициализируется при старте MCU; программного снятия запрета нет. */
static volatile bool app_safety_reset_pending = false;

void AppSafety_AllOff(void) {
	/*
	 * Останавливаем оба PWM-канала Пельтье.
	 *
	 * PeltierPwm_Stop() устанавливает duty=0,
	 * сбрасывает CCR и останавливает таймеры.
	 */
	PeltierPwm_Stop();

	/*
	 * Снимаем управляющий уровень обоих SSR.
	 * GPIO-драйвер обеспечивает LOW и при раннем startup.
	 */

	HeaterOutputs_AllOff();

	/*
	 * STB CAN-трансивера здесь не изменяется.
	 * Исполнитель должен сохранить CAN-связь
	 * для передачи fault и диагностики.
	 */
}

bool AppSafety_IsResetPending(void) {
	return app_safety_reset_pending;
}

void AppSafety_PrepareReset(void) {
	/*
	 * Сохраняем состояние IRQ: нельзя безусловно разрешать
	 * прерывания, если вызывающий код уже запретил их.
	 */
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	/*
	 * Запрет и аппаратное отключение выполняются вместе.
	 * Здесь нет Flash, CAN, RTOS-ожиданий или задержек.
	 */
	app_safety_reset_pending = true;
	AppSafety_AllOff();
	__DSB();

	__set_PRIMASK(saved_primask);
}

