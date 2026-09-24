/*
 * task_watchdog.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 * Сбор отметок прогресса критических задач Heater/Cooler.
 * Обслуживает аппаратный IWDG, пока не достигнут порог отказа клиентов.
 *
 * Программный watchdog-supervisor Heater/Cooler.
 * Проверяет прогресс задач и выполняет фатальное отключение.
 */

// --- Зависимости ---
#include "tasks/task_watchdog.h"
#include "cmsis_os.h"
#include "main.h"

#include <stdbool.h>
#include <stdint.h>

/* Handle создан CubeMX в main.c. Обслуживает IWDG только supervisor. */
extern IWDG_HandleTypeDef hiwdg;

// --- Счётчики прогресса ---

/*
 * Каждый клиент изменяет только свой счётчик.
 * Supervisor читает значения, но не обнуляет их.
 */
static volatile uint32_t app_watchdog_heartbeats[APP_WDG_CLIENT_COUNT];

// --- Публикация прогресса ---

/*
 * Вызывается только соответствующей задачей-клиентом.
 * Не обслуживает IWDG и не обращается к RTOS.
 */
void AppWatchdog_Heartbeat(AppWatchdogClient_t client) {
	if ((uint32_t) client < (uint32_t) APP_WDG_CLIENT_COUNT) {
		app_watchdog_heartbeats[client]++;

	}
}

// --- Проверка всех клиентов ---

/*
 * Изменившийся счётчик сбрасывает пропуски только своего клиента.
 * Неизменившийся увеличивает число последовательных пропусков.
 * После достижения порога результат проверки становится отрицательным.
 */
static bool AppWatchdog_AllClientsHealthy(
		uint32_t previous[APP_WDG_CLIENT_COUNT],
		uint8_t missed[APP_WDG_CLIENT_COUNT]) {
	bool all_healthy = true;

	for (uint32_t i = 0U; i < (uint32_t) APP_WDG_CLIENT_COUNT; i++) {
		uint32_t current = app_watchdog_heartbeats[i];

		if (current != previous[i]) {
			missed[i] = 0U;
		} else if (missed[i] < APP_WATCHDOG_MAX_MISSED_CHECKS) {
			missed[i]++;
		}

		if (missed[i] >= APP_WATCHDOG_MAX_MISSED_CHECKS) {
			all_healthy = false;
		}

		previous[i] = current;
	}

	return all_healthy;
}

// --- Основной цикл supervisor ---

/*
 * Первое окно даёт клиентам время начать работу.
 * Порог пропусков действует и при старте: бесконечной поблажки нет.
 * При отказе Error_Handler отключает выходы и не возвращается.
 */
void app_start_task_watchdog(void *argument) {
	uint32_t previous[APP_WDG_CLIENT_COUNT] = { 0 };
	uint8_t missed[APP_WDG_CLIENT_COUNT] = { 0 };

	(void) argument;

	const uint32_t period_ticks = (APP_WATCHDOG_SUPERVISOR_PERIOD_MS
			* osKernelGetTickFreq()) / 1000U;

	/* Нулевой период недопустим. */
	if (period_ticks == 0U) {
		Error_Handler();
	}

	/*
	 * IWDG запущен до планировщика.
	 * Однократное обновление перед первым окном проверки.
	 * Прогресс клиентов здесь ещё не проверен.
	 */
	HAL_IWDG_Refresh(&hiwdg);

	for (;;) {
		/* Supervisor освобождает процессор на время ожидания. */
		if (osDelay(period_ticks) != osOK) {
			Error_Handler();
		}

		if (!AppWatchdog_AllClientsHealthy(previous, missed)) {
			/* Отключаем выходы и останавливаемся без refresh. */
			Error_Handler();
		}

		/*
		 * Порог отказа не достигнут — обновляем IWDG.
		 * Один пропуск допускается; два подряд вызывают отказ.
		 */
		HAL_IWDG_Refresh(&hiwdg);
	}

}
