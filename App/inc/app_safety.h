/*
 * app_safety.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Общий аппаратный safe-off API Heater/Cooler.
 *
 */

#ifndef INC_APP_SAFETY_H_
#define INC_APP_SAFETY_H_

// --- Общий safe-state ---

/*
 * Выключает все PWM, GPIO-нагреватели и силовые enable.
 *
 * Функция не принимает решений о температуре
 * и не отправляет CAN-ответы.
 */
void AppSafety_AllOff(void);

#endif /* INC_APP_SAFETY_H_ */
