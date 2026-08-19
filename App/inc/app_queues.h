/*
 * app_queues.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#ifndef INC_APP_QUEUES_H_
#define INC_APP_QUEUES_H_

#include "cmsis_os.h"

extern osMessageQueueId_t can_rx_queueHandle;
extern osMessageQueueId_t can_tx_queueHandle;
extern osMessageQueueId_t dispatcher_queueHandle;
extern osMessageQueueId_t heater_cooler_queueHandle;

extern osThreadId_t task_can_handleHandle;
extern osThreadId_t task_dispatcherHandle;

#endif /* INC_APP_QUEUES_H_ */
