/*
 * heater_outputs.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#include "drivers/heater_outputs.h"

#include "main.h"

// --- Аппаратная карта выходов ---

/* Порт и пин составляют единое описание GPIO одного выхода. */
typedef struct {
	GPIO_TypeDef *port;
	uint16_t pin;
} HeaterOutputGpio_t;

/* Именованные индексы явно связывают назначение с подключением. */
static const HeaterOutputGpio_t heater_outputs[HEATER_OUTPUT_COUNT] = {
		[HEATER_OUTPUT_SAMPLE_DISK] = { .port = heater_1_GPIO_Port, .pin =
				heater_1_Pin }, [HEATER_OUTPUT_SCANNER_GLASS] = { .port =
				heater_2_GPIO_Port, .pin = heater_2_Pin } };

// --- Проверка идентификатора ---
/*
 * Enum в C не исключает передачи постороннего значения.
 * Проверка выполняется до обращения к аппаратной таблице.
 * Приведение к unsigned также исключает отрицательные значения.
 */
static bool HeaterOutputs_IsValid(HeaterOutput_t output) {
	return (unsigned int) output < (unsigned int) HEATER_OUTPUT_COUNT;
}

// --- Включение выхода ---

/*
 * Доменная задача вызывает функцию после проверки команды.
 * GPIO остаётся в HIGH до DISABLE или общего safe-off.
 * HAL-вызов задаёт уровень, но не проверяет состояние нагрузки.
 */
bool HeaterOutputs_Enable(HeaterOutput_t output) {
	if (!HeaterOutputs_IsValid(output)) {
		return false;
	}

	HAL_GPIO_WritePin(heater_outputs[output].port, heater_outputs[output].pin,
			GPIO_PIN_SET);
	return true;
}

// --- Отключение выхода ---

/*
 * Немедленно снимает управляющий уровень выбранного SSR.
 * Повторное отключение безопасно и не требует знания
 * предыдущего состояния канала.
 */
bool HeaterOutputs_Disable(HeaterOutput_t output) {
	if (!HeaterOutputs_IsValid(output)) {
		return false;
	}

	HAL_GPIO_WritePin(heater_outputs[output].port, heater_outputs[output].pin,
			GPIO_PIN_RESET);
	return true;
}

// --- Общее безопасное отключение ---

/*
 * Принудительно сбрасывает оба выхода независимо
 * от состояния доменной задачи.
 * GPIO должны быть инициализированы; RTOS не требуется.
 */
void HeaterOutputs_AllOff(void) {
	for (unsigned int output = 0U; output < (unsigned int) HEATER_OUTPUT_COUNT;
			output++) {
		HAL_GPIO_WritePin(heater_outputs[output].port,
				heater_outputs[output].pin, GPIO_PIN_RESET);
	}
}
