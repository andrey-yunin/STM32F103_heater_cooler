/*
 * app_safety.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Общий аппаратный safe-off API Heater/Cooler.
 *
 *
 * Аппаратное отключение и подготовка Heater/Cooler к reset.
 */

#ifndef INC_APP_SAFETY_H_
#define INC_APP_SAFETY_H_

#include <stdbool.h>

/*
 * Отключает PWM Пельтье и GPIO-нагреватели.
 * Не устанавливает постоянный запрет повторного включения.
 * Не использует RTOS и не отправляет CAN-ответы.
 */
void AppSafety_AllOff(void);

/*
 * Фиксирует запрет активации и отключает выходы.
 * Запрет снимается только перезапуском MCU.
 * Вызывается из задачи после проверки сервисного ключа.
 */
void AppSafety_PrepareReset(void);

/*
 * Показывает, началась ли подготовка к reset.
 * Проверку и последующее аппаратное включение необходимо
 * защищать от переключения задач как единую операцию.
 */
bool AppSafety_IsResetPending(void);

#endif /* INC_APP_SAFETY_H_ */
