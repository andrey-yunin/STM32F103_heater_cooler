/*
 * task_can_handler.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_CAN_HANDLER_H_
#define INC_TASKS_TASK_CAN_HANDLER_H_

#include <stdint.h>


typedef struct
{
	uint32_t rx_total;
    uint32_t tx_total;

    uint32_t rx_queue_overflow;
    uint32_t tx_queue_overflow;
    uint32_t dispatcher_queue_overflow;
    uint32_t app_queue_overflow;

    uint32_t dropped_not_ext;
    uint32_t dropped_wrong_dst;
    uint32_t dropped_wrong_type;
    uint32_t dropped_wrong_dlc;

    uint32_t tx_mailbox_timeout;
    uint32_t tx_hal_error;

    uint32_t can_error_callback_count;
    uint32_t error_warning_count;
    uint32_t error_passive_count;
    uint32_t bus_off_count;

    uint32_t last_hal_error;
    uint32_t last_esr;
} CanDiagnostics_t;


// --- Task entry point ---

/*
 * Owns bxCAN start, RX frame validation and physical TX mailbox access.
 */
void app_start_task_can_handler(void *argument);

void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out);
void CAN_Diagnostics_RecordRxQueueOverflow(void);
void CAN_Diagnostics_RecordAppQueueOverflow(void);
void CAN_Diagnostics_RecordCanError(uint32_t hal_error,
									uint32_t esr);

#endif /* INC_TASKS_TASK_CAN_HANDLER_H_ */
