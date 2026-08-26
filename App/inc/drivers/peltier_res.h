/*
 * peltier_res.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Контракт чтения active-low RES-защиты каналов Пельтье.
 *
 */

#ifndef INC_DRIVERS_PELTIER_RES_H_
#define INC_DRIVERS_PELTIER_RES_H_

#include <stdbool.h>
#include <stdint.h>

// --- Локальные ограничения RES ---

#define PELTIER_RES_CHANNEL_COUNT 2U

// --- Чтение аппаратной защиты ---

/*
 * true означает активную fault-защиту:
 * соответствующий RES-вход находится в GPIO_PIN_RESET.
 */
bool PeltierRes_IsFault(uint8_t channel);

/*
 * true только если оба RES-входа находятся в безопасном состоянии.
 */
bool PeltierRes_AllClear(void);

#endif /* INC_DRIVERS_PELTIER_RES_H_ */
