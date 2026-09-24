/*
 * task_watchdog.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 *
 *
 * Контракт отметок прогресса критических задач Heater/Cooler.
 * Задачи сообщают о продвижении, но сами не обслуживают IWDG.
 */

#ifndef INC_TASKS_TASK_WATCHDOG_H_
#define INC_TASKS_TASK_WATCHDOG_H_

// --- Штатное ожидание событий ---

/*
 * Ограничивает блокировку CAN и Dispatcher при отсутствии команд.
 * Это интервал пробуждения задач, не срок аварийного отключения.
 */
#define APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS 500U

// --- Профиль supervisor для испытаний A ---

/*
 * Проверяем клиентов каждые 500 мс.
 * Отказ — два последовательных окна без прогресса одного клиента.
 */
#define APP_WATCHDOG_SUPERVISOR_PERIOD_MS 500U
#define APP_WATCHDOG_MAX_MISSED_CHECKS    2U

// --- Клиенты watchdog ---

/*
 * Каждому клиенту соответствует одна задача-писатель.
 * Supervisor не является клиентом самого себя.
 */
typedef enum {
	APP_WDG_CLIENT_CAN = 0,
	APP_WDG_CLIENT_DISPATCHER,
	APP_WDG_CLIENT_HEATER_COOLER,
	APP_WDG_CLIENT_COUNT
} AppWatchdogClient_t;

// --- Публикация прогресса ---

/*
 * Вызывается задачей при прохождении контрольной точки.
 * Не выключает выходы и не выполняет refresh IWDG.
 * Один идентификатор нельзя использовать из нескольких задач или ISR.
 */
void AppWatchdog_Heartbeat(AppWatchdogClient_t client);

// --- Entry point supervisor ---

/* Контролирует heartbeat всех критических задач. */
void app_start_task_watchdog(void *argument);

#endif /* INC_TASKS_TASK_WATCHDOG_H_ */
