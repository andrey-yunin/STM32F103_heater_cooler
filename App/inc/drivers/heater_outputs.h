/*
 *  heater_outputs.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * heater_outputs.h
 *
 * Управление двумя внешними SSR-25DD через GPIO.
 * Драйвер выполняет только включение/выключение.
 * Температурное регулирование находится у Дирижёра.
 */

#ifndef INC_DRIVERS_HEATER_OUTPUTS_H_
#define INC_DRIVERS_HEATER_OUTPUTS_H_

#include <stdbool.h>
#include <stdint.h>

// --- Локальные идентификаторы выходов ---
/*
 * Индексы GPIO-драйвера, не номера каналов CAN.
 * COUNT задаёт размер таблицы и не является выходом.
 */
typedef enum {
	HEATER_OUTPUT_SAMPLE_DISK = 0,
	HEATER_OUTPUT_SCANNER_GLASS,
	HEATER_OUTPUT_COUNT
} HeaterOutput_t;


// --- Управление отдельным выходом ---

/* Включает GPIO; false при недопустимом идентификаторе. */
bool HeaterOutputs_Enable(HeaterOutput_t output);

/* Выключает GPIO; false при недопустимом идентификаторе. */
bool HeaterOutputs_Disable(HeaterOutput_t output);


/*
 * Принудительно выключает оба выхода.
 * Не использует RTOS, очереди и CAN.
 * Вызывается после инициализации GPIO.
 */
void HeaterOutputs_AllOff(void);

#endif /* INC_DRIVERS_HEATER_OUTPUTS_H_ */
