/*
 * task_heater_cooler_ctrl.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Entry point доменной задачи Heater/Cooler.
 *
 */

#ifndef INC_TASKS_TASK_HEATER_COOLER_CTRL_H_
#define INC_TASKS_TASK_HEATER_COOLER_CTRL_H_

// --- Task entry point ---

/*
 * Владеет HeaterCoolerCommand_t, состоянием каналов,
 * fault latch и финальными DONE/NACK.
 */
void app_start_task_heater_cooler_ctrl(void *argument);

#endif /* INC_TASKS_TASK_HEATER_COOLER_CTRL_H_ */
