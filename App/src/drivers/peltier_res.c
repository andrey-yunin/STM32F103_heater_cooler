/*
 * peltier_res.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 *
 * Драйвер чтения аппаратной RES-защиты двух каналов Пельтье.
 *
 * Карта входов:
 *   локальный канал 0 — PA2;
 *   локальный канал 1 — PA3.
 *
 * RES active-low:
 *   GPIO_PIN_RESET — активная fault-защита;
 *   GPIO_PIN_SET   — вход находится в безопасном состоянии.
 */

#include "drivers/peltier_res.h"

#include "main.h"

/*
 * Порты RES-входов.
 *
 * Используем имена, сгенерированные CubeMX в main.h.
 * Это сохраняет связь драйвера с настройкой .ioc.
 */
static GPIO_TypeDef *const peltier_res_ports[PELTIER_RES_CHANNEL_COUNT] =
												{
													puls_res_1_GPIO_Port,
													puls_res_2_GPIO_Port
												};

/*
 * Пины RES-входов в соответствии с логическими каналами Пельтье.
 */
static const uint16_t peltier_res_pins[
		PELTIER_RES_CHANNEL_COUNT] = {
		puls_res_1_Pin,
		puls_res_2_Pin
};

/* Проверяет локальный номер канала до обращения к таблицам GPIO. */
static bool PeltierRes_IsValid(uint8_t channel)
{
	return channel < PELTIER_RES_CHANNEL_COUNT;
}

bool PeltierRes_IsFault(uint8_t channel)
{
	GPIO_PinState pin_state;

	/*
	 * Некорректный канал трактуется как fault.
     *
     * Это безопасное поведение: при ошибочном обращении
     * нельзя считать аппаратную защиту исправной.
     */


	if (!PeltierRes_IsValid(channel))
	{
  		return true;
  	}

  	/* Читаем физический уровень соответствующего RES-входа. */
  	pin_state = HAL_GPIO_ReadPin(peltier_res_ports[channel],
  								 peltier_res_pins[channel]);

  	/*
  	 * RES active-low:
  	 * нулевой уровень означает сработавшую защиту.
  	 */
  	return pin_state == GPIO_PIN_RESET;
}

bool PeltierRes_AllClear(void)
{
	/*
  	 * Проверяем оба силовых канала.
  	 *
  	 * Если хотя бы один RES-вход активен,
  	 * вся проверка считается неуспешной.
  	 */
  	for (uint8_t channel = 0U;
  			channel < PELTIER_RES_CHANNEL_COUNT;
  			channel++)
  	{
  		if (PeltierRes_IsFault(channel))
  		{
  			return false;
  		}
  	}

  	return true;
}
