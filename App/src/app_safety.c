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

void AppSafety_AllOff(void)
{
	/*
  	 * Останавливаем оба PWM-канала Пельтье.
  	 *
  	 * PeltierPwm_Stop() устанавливает duty=0,
  	 * сбрасывает CCR и останавливает таймеры.
  	 */
  	PeltierPwm_Stop();

  	/*
  	 * Выключаем оба GPIO-нагревателя
  	 * и сбрасываем их duty.
  	 */
  	HeaterOutputs_AllOff();

  	/*
  	 * STB CAN-трансивера здесь не изменяется.
  	 * Исполнитель должен сохранить CAN-связь
  	 * для передачи fault и диагностики.
  	 */
}


