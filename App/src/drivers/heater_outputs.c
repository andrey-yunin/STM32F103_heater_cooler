/*
 * heater_outputs.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#include "drivers/heater_outputs.h"
#include "app_safety.h"

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

	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	/*
	 * Проверка запрета и включение неделимы для задач:
	 * PrepareReset не сможет вклиниться между ними.
	 */
	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	HAL_GPIO_WritePin(heater_outputs[output].port, heater_outputs[output].pin,
			GPIO_PIN_SET);

	__DSB();
	__set_PRIMASK(saved_primask);
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

// --- Общее безопасное отключение нагревателей ---

/*
 * Обеспечивает LOW на обоих выходах SSR до или после MX_GPIO_Init().
 * Оба выхода текущей платы находятся на GPIOA.
 * Остальные пины, включая CAN STB, не изменяются.
 */
void HeaterOutputs_AllOff(void) {
	GPIO_InitTypeDef gpio = { 0 };

	// --- Подготовка порта ---

	__HAL_RCC_GPIOA_CLK_ENABLE();

	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;

	// --- Отключение выходов ---

	/* LOW записывается до переключения пина в выходной режим. */
	for (unsigned int output = 0U; output < (unsigned int) HEATER_OUTPUT_COUNT;
			output++) {
		gpio.Pin = heater_outputs[output].pin;

		HAL_GPIO_WritePin(heater_outputs[output].port,
				heater_outputs[output].pin, GPIO_PIN_RESET);

		HAL_GPIO_Init(heater_outputs[output].port, &gpio);
	}
}

