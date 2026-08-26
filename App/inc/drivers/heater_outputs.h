/*
 *  heater_outputs.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Контракт управления двумя GPIO-нагревателями:
 * PA4 — sample disk;
 * PA5 — scanner glass.
 *
 */

#ifndef INC_DRIVERS_HEATER_OUTPUTS_H_
#define INC_DRIVERS_HEATER_OUTPUTS_H_

#include <stdbool.h>
#include <stdint.h>


/* Логическая карта двух GPIO-нагревателей платы. */

#define HEATER_OUTPUT_COUNT 2U
#define HEATER_OUTPUT_SAMPLE_DISK 0U
#define HEATER_OUTPUT_SCANNER_GLASS 1U

#define HEATER_OUTPUT_DUTY_MIN_PERCENT 0U
#define HEATER_OUTPUT_DUTY_MAX_PERCENT 100U

/*
 * Управление GPIO и программным time-proportional PWM.
 *
 * Duty — процент времени, в течение которого выход включён
 * внутри одного 100-мс окна:
 *   0%   — выход постоянно выключен;
 *   50%  — выход включён половину окна;
 *   100% — выход постоянно включён.
 *
 * Duty не является температурой, напряжением или током.
 */

/* Разрешает работу выбранного логического выхода. */
bool HeaterOutputs_Enable(uint8_t output);

/* Немедленно отключает выбранный логический выход. */
bool HeaterOutputs_Disable(uint8_t output);

/* Сохраняет требуемый duty в диапазоне 0..100 процентов. */
bool HeaterOutputs_SetDuty(uint8_t output,
							uint8_t duty_percent);

/*
 * Вызывается периодически из доменной задачи.
 * Реализует time-proportional PWM для GPIO-выходов.
 */
void HeaterOutputs_Tick(uint32_t now_ms);

/*
 * Немедленно выключает оба GPIO-выхода и сбрасывает duty.
 */
void HeaterOutputs_AllOff(void);

/* Возвращает последнее принятое значение duty выбранного выхода. */
uint8_t HeaterOutputs_GetDuty(uint8_t output);


#endif /* INC_DRIVERS_HEATER_OUTPUTS_H_ */
