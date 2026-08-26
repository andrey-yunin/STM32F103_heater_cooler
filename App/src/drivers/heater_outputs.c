/*
 * heater_outputs.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */


#include "drivers/heater_outputs.h"

#include "main.h"

/*
 * Период time-proportional PWM для GPIO-нагревателей.
 * Внутри этого окна duty задаёт долю времени активного GPIO-уровня.
 */
#define HEATER_OUTPUT_WINDOW_MS 100U

/* Таблица GPIO-портов в соответствии с логическими индексами выходов. */
static GPIO_TypeDef *const heater_ports[HEATER_OUTPUT_COUNT] = {
		heater_1_GPIO_Port,
		heater_2_GPIO_Port
};

/* Таблица GPIO-пинов в соответствии с логическими индексами выходов. */
static const uint16_t heater_pins[HEATER_OUTPUT_COUNT] = {
		heater_1_Pin,
		heater_2_Pin
};

/*
 * Текущий duty каждого выхода и его разрешённое состояние.
 * Duty хранится в процентах от 0 до 100, а не как значение таймера.
 */
static uint8_t heater_duty[HEATER_OUTPUT_COUNT];
static bool heater_enabled[HEATER_OUTPUT_COUNT];

/* Проверяет индекс до обращения к таблицам портов и пинов. */
static bool HeaterOutputs_IsValid(uint8_t output)
{
	return output < HEATER_OUTPUT_COUNT;
}

/* Устанавливает физический уровень GPIO выбранного нагревателя. */
static void HeaterOutputs_Write(uint8_t output,
								GPIO_PinState state)
{
	HAL_GPIO_WritePin(heater_ports[output],
					heater_pins[output],
					state);
}

/*
 * Преобразует duty и текущую фазу окна в физический уровень GPIO.
 * Например, при duty=30 GPIO активен первые 30 мс каждого 100-мс окна.
 */
static void HeaterOutputs_Apply(uint8_t output,
								uint32_t phase_ms)
{
	uint32_t on_time_ms;

	if (!heater_enabled[output] ||
			(heater_duty[output] == HEATER_OUTPUT_DUTY_MIN_PERCENT))
		{
		HeaterOutputs_Write(output, GPIO_PIN_RESET);
		return;
		}

	if (heater_duty[output] >= HEATER_OUTPUT_DUTY_MAX_PERCENT)
	{
		HeaterOutputs_Write(output, GPIO_PIN_SET);
        return;
	}

	on_time_ms =
			((uint32_t)heater_duty[output] * HEATER_OUTPUT_WINDOW_MS) / 100U;

	HeaterOutputs_Write(output,
			(phase_ms < on_time_ms)
			? GPIO_PIN_SET
			: GPIO_PIN_RESET);
}

bool HeaterOutputs_Enable(uint8_t output)
{
	/* Разрешаем выход и сразу применяем его сохранённый duty. */
	if (!HeaterOutputs_IsValid(output))
	{
		return false;
	}

	heater_enabled[output] = true;
    HeaterOutputs_Apply(output, 0U);

    return true;
}

bool HeaterOutputs_Disable(uint8_t output)
{
	/* Сначала запрещаем программную генерацию, затем физически сбрасываем GPIO. */
	if (!HeaterOutputs_IsValid(output))
	{
		return false;
	}

    heater_enabled[output] = false;
    HeaterOutputs_Write(output, GPIO_PIN_RESET);
    return true;
}

bool HeaterOutputs_SetDuty(uint8_t output,
						uint8_t duty_percent)
{
	/* Некорректный индекс или duty не должен попасть в аппаратный слой. */
	if (!HeaterOutputs_IsValid(output) ||
			(duty_percent > HEATER_OUTPUT_DUTY_MAX_PERCENT))
		{
		return false;
		}

	heater_duty[output] = duty_percent;

/* Нулевой duty является безусловной командой выключения выхода. */
	if (duty_percent == HEATER_OUTPUT_DUTY_MIN_PERCENT)
		{
		HeaterOutputs_Write(output, GPIO_PIN_RESET);
		}
	return true;
}

void HeaterOutputs_Tick(uint32_t now_ms)
{
	uint32_t phase_ms;

	/* Все выходы используют одну общую временную фазу окна. */
    phase_ms = now_ms % HEATER_OUTPUT_WINDOW_MS;

    for (uint8_t output = 0U;
    		output < HEATER_OUTPUT_COUNT;
    		output++)
    {
    	HeaterOutputs_Apply(output, phase_ms);
    }
}

void HeaterOutputs_AllOff(void)
{
	/* Safe-state: сбрасываем состояние и физический уровень каждого выхода. */
	for (uint8_t output = 0U;
			output < HEATER_OUTPUT_COUNT;
			output++)
	{
		heater_enabled[output] = false;
        heater_duty[output] = HEATER_OUTPUT_DUTY_MIN_PERCENT;

        HeaterOutputs_Write(output, GPIO_PIN_RESET);
	}
}

uint8_t HeaterOutputs_GetDuty(uint8_t output)
{
	/* Для ошибочного индекса возвращаем безопасное значение duty=0. */
	if (!HeaterOutputs_IsValid(output))
	{
		return HEATER_OUTPUT_DUTY_MIN_PERCENT;
	}
	return heater_duty[output];
}
