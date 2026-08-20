/*
 * task_dispatcher.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_DISPATCHER_H_
#define INC_TASKS_TASK_DISPATCHER_H_

// --- Task entry point ---

/*
 * Owns command routing, ACK policy and service/domain dispatch.
 */
void app_start_task_dispatcher(void *argument);

#endif /* INC_TASKS_TASK_DISPATCHER_H_ */
