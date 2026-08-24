/*
 * task_can_handler.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#include "tasks/task_can_handler.h"

#include "app_config.h"
#include "app_flash.h"
#include "app_queues.h"
#include "can_protocol.h"
#include "cmsis_os.h"
#include "main.h"

#include <string.h>

extern CAN_HandleTypeDef hcan;

#define CAN_TX_MAILBOX_TIMEOUT_MS 10U

static volatile CanDiagnostics_t g_can_diag;

// --- Hardware CAN filter configuration ---

/*
 * bxCAN filter-register mask:
 * bits 19..26 - destination address;
 * bits 27..28 - message type;
 * bit 2       - Extended ID indicator.
 *
 * Priority, source address and RTR are intentionally not masked here.
 * They are handled by the software transport validation layer.
 */
#define CAN_FILTER_MSGTYPE_DST_MASK (0x03FFUL << 19)
#define CAN_FILTER_IDE              (1UL << 2)

static void CAN_ConfigureFilterBank(uint8_t bank, uint8_t destination)
{
    CAN_FilterTypeDef filter_config;
    uint32_t filter_id;
    uint32_t filter_mask;
    uint32_t filter_reg;

    memset(&filter_config, 0, sizeof(filter_config));

    /*
     * The filter accepts only COMMAND frames addressed to the selected
     * destination. The source address remains unfiltered at hardware level.
     */
    filter_id = CAN_BUILD_ID(0U, CAN_MSG_TYPE_COMMAND, destination, 0U);

    /*
     * Both identifier and mask use bxCAN filter-register coordinates.
     * The Extended CAN ID is shifted by three bits before being written.
     */
    filter_reg = (filter_id << 3) | CAN_FILTER_IDE;
    filter_mask = CAN_FILTER_MSGTYPE_DST_MASK | CAN_FILTER_IDE;

    filter_config.FilterBank = bank;
    filter_config.FilterMode = CAN_FILTERMODE_IDMASK;
    filter_config.FilterScale = CAN_FILTERSCALE_32BIT;
    filter_config.FilterIdHigh = (uint16_t)(filter_reg >> 16);
    filter_config.FilterIdLow = (uint16_t)(filter_reg & 0xFFFFU);
    filter_config.FilterMaskIdHigh = (uint16_t)(filter_mask >> 16);
    filter_config.FilterMaskIdLow = (uint16_t)(filter_mask & 0xFFFFU);
    filter_config.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter_config.FilterActivation = ENABLE;
    filter_config.SlaveStartFilterBank = 14U;

    if (HAL_CAN_ConfigFilter(&hcan, &filter_config) != HAL_OK) {
        Error_Handler();
    }
}

void CAN_UpdateDirectFilter(uint8_t destination)
{
    CAN_ConfigureFilterBank(1U, destination);
}

// --- Internal transport helpers ---

static void CAN_QueueTxFrame(const CanTxFrame_t *tx_frame) {
    if (tx_frame == NULL) {
        return;
    }

    if (osMessageQueuePut(can_tx_queueHandle, tx_frame, 0U, 0U) == osOK) {
        (void)osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
    }

    else {
        /*
         * Ответ не помещен в software TX queue.
         * Физическая передача из ISR или другой задачи не выполняется.
         *
         */
        g_can_diag.tx_queue_overflow++;
    }
}

void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out) {
    uint32_t primask;

    if (out == NULL) {
        return;
    }

    /*
     * Snapshot должен сохранить исходное состояние PRIMASK.
     * Нельзя безусловно включать IRQ после копирования.
     */
    primask = __get_PRIMASK();

    __disable_irq();
    memcpy(out, (const void *)&g_can_diag, sizeof(CanDiagnostics_t));
    __set_PRIMASK(primask);
}

void CAN_Diagnostics_RecordRxQueueOverflow(void) { g_can_diag.rx_queue_overflow++; }

void CAN_Diagnostics_RecordAppQueueOverflow(void) { g_can_diag.app_queue_overflow++; }

void CAN_Diagnostics_RecordCanError(uint32_t hal_error, uint32_t esr) {
    g_can_diag.can_error_callback_count++;
    g_can_diag.last_hal_error = hal_error;
    g_can_diag.last_esr = esr;

    if ((esr & CAN_ESR_EWGF) != 0U) {
        g_can_diag.error_warning_count++;
    }

    if ((esr & CAN_ESR_EPVF) != 0U) {
        g_can_diag.error_passive_count++;
    }

    if ((esr & CAN_ESR_BOFF) != 0U) {
        g_can_diag.bus_off_count++;
    }
}

static bool CAN_IsAcceptedCommand(const CanRxFrame_t *rx_frame)
{
	uint8_t destination;
	uint8_t source;
	uint8_t node_id;

	if (rx_frame == NULL) {
		return false;
	}

	if (rx_frame->header.IDE != CAN_ID_EXT) {
		g_can_diag.dropped_not_ext++;
		return false;
	}

	if (rx_frame->header.RTR != CAN_RTR_DATA) {
		return false;
	}

	if (rx_frame->header.DLC != CAN_FRAME_DLC) {
		g_can_diag.dropped_wrong_dlc++;
		return false;
	}

	if (CAN_GET_MSG_TYPE(rx_frame->header.ExtId) != CAN_MSG_TYPE_COMMAND) {
		g_can_diag.dropped_wrong_type++;
		return false;
	}

	destination = CAN_GET_DST_ADDR(rx_frame->header.ExtId);
	source = CAN_GET_SRC_ADDR(rx_frame->header.ExtId);
	node_id = (uint8_t)AppConfig_GetPerformerID();

	if (source != CAN_ADDR_CONDUCTOR) {
		return false;
	}

	if ((destination != node_id) &&
		(destination != CAN_ADDR_BROADCAST)) {
		g_can_diag.dropped_wrong_dst++;
		return false;
	}

	return true;
}

static void CAN_ParseCommand(const CanRxFrame_t *rx_frame, ParsedCanCommand_t *parsed_command) {
    memset(parsed_command, 0, sizeof(*parsed_command));
    parsed_command->cmd_code = (uint16_t)rx_frame->data[0] | ((uint16_t)rx_frame->data[1] << 8);

    parsed_command->device_id = rx_frame->data[2];

    memcpy(parsed_command->data, &rx_frame->data[3], sizeof(parsed_command->data));
    parsed_command->data_len = sizeof(parsed_command->data);
}

// --- CAN response builders ---

void CAN_SendAck(uint16_t cmd_code) {
    CanTxFrame_t tx_frame;

    memset(&tx_frame, 0, sizeof(tx_frame));

    tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_ACK, CAN_ADDR_CONDUCTOR,
                                         (uint8_t)AppConfig_GetPerformerID());

    tx_frame.header.IDE = CAN_ID_EXT;
    tx_frame.header.RTR = CAN_RTR_DATA;
    tx_frame.header.DLC = CAN_FRAME_DLC;

    tx_frame.data[0] = (uint8_t)(cmd_code & 0xFFU);
    tx_frame.data[1] = (uint8_t)((cmd_code >> 8) & 0xFFU);

    CAN_QueueTxFrame(&tx_frame);
}

void CAN_SendNack(uint16_t cmd_code, uint16_t error_code)

{
    CanTxFrame_t tx_frame;

    memset(&tx_frame, 0, sizeof(tx_frame));

    tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_NACK, CAN_ADDR_CONDUCTOR,
                                         (uint8_t)AppConfig_GetPerformerID());

    tx_frame.header.IDE = CAN_ID_EXT;
    tx_frame.header.RTR = CAN_RTR_DATA;
    tx_frame.header.DLC = CAN_FRAME_DLC;

    tx_frame.data[0] = (uint8_t)(cmd_code & 0xFFU);
    tx_frame.data[1] = (uint8_t)((cmd_code >> 8) & 0xFFU);
    tx_frame.data[2] = (uint8_t)(error_code & 0xFFU);
    tx_frame.data[3] = (uint8_t)((error_code >> 8) & 0xFFU);

    CAN_QueueTxFrame(&tx_frame);
}

void CAN_SendData(uint16_t cmd_code, uint8_t sequence_info, const uint8_t *data, uint8_t len) {
    CanTxFrame_t tx_frame;
    uint8_t copy_len;

    /*
     * cmd_code is retained for transaction context.
     * It is not encoded into DATA payload according to DDS-240.
     */
    (void)cmd_code;

    memset(&tx_frame, 0, sizeof(tx_frame));

    copy_len = len;

    if (copy_len > CAN_DATA_PAYLOAD_MAX) {
        copy_len = CAN_DATA_PAYLOAD_MAX;
    }

    tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_DATA_DONE_LOG,
                                         CAN_ADDR_CONDUCTOR, AppConfig_GetPerformerID());

    tx_frame.header.IDE = CAN_ID_EXT;
    tx_frame.header.RTR = CAN_RTR_DATA;
    tx_frame.header.DLC = CAN_FRAME_DLC;

    tx_frame.data[0] = CAN_SUB_TYPE_DATA;
    tx_frame.data[1] = sequence_info;

    if ((data != NULL) && (copy_len > 0U)) {
        memcpy(&tx_frame.data[2], data, copy_len);
    }

    CAN_QueueTxFrame(&tx_frame);
}

void CAN_SendDone(uint16_t cmd_code, uint8_t target_id) {
    CanTxFrame_t tx_frame;

    memset(&tx_frame, 0, sizeof(tx_frame));

    tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_DATA_DONE_LOG,
                                         CAN_ADDR_CONDUCTOR, (uint8_t)AppConfig_GetPerformerID());

    tx_frame.header.IDE = CAN_ID_EXT;
    tx_frame.header.RTR = CAN_RTR_DATA;
    tx_frame.header.DLC = CAN_FRAME_DLC;

    tx_frame.data[0] = CAN_SUB_TYPE_DONE;
    tx_frame.data[1] = (uint8_t)(cmd_code & 0xFFU);
    tx_frame.data[2] = (uint8_t)((cmd_code >> 8) & 0xFFU);
    tx_frame.data[3] = target_id;

    CAN_QueueTxFrame(&tx_frame);
}

// --- CAN task ---

void app_start_task_can_handler(void *argument) {
    CanRxFrame_t rx_frame;
    CanTxFrame_t tx_frame;
    ParsedCanCommand_t parsed_command;
    uint32_t tx_mailbox;

    (void)argument;

    // --- Configure broadcast and direct command filters ---

    CAN_ConfigureFilterBank(0U, CAN_ADDR_BROADCAST);
    CAN_ConfigureFilterBank(1U, (uint8_t)AppConfig_GetPerformerID());

    if (HAL_CAN_Start(&hcan) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_CAN_ActivateNotification(&hcan,
              CAN_IT_RX_FIFO0_MSG_PENDING |
              CAN_IT_RX_FIFO0_FULL |
              CAN_IT_RX_FIFO0_OVERRUN |
              CAN_IT_ERROR_WARNING |
              CAN_IT_ERROR_PASSIVE |
              CAN_IT_BUSOFF |
              CAN_IT_LAST_ERROR_CODE |
              CAN_IT_ERROR) != HAL_OK) {
        Error_Handler();
    }

    for (;;) {
        uint32_t flags;

        flags = osThreadFlagsWait(FLAG_CAN_RX | FLAG_CAN_TX, osFlagsWaitAny, osWaitForever);

        if ((flags & osFlagsError) != 0U) {
            continue;
        }

        // --- RX: raw frame to dispatcher queue ---

        if ((flags & FLAG_CAN_RX) != 0U) {
            while (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, 0U) == osOK) {
                if (!CAN_IsAcceptedCommand(&rx_frame)) {
                    continue;
                }

                CAN_ParseCommand(&rx_frame, &parsed_command);

                if (osMessageQueuePut(dispatcher_queueHandle,
                                      &parsed_command,
                                      0U,
                                      0U) == osOK) {
                    g_can_diag.rx_total++;
                } else {
                    g_can_diag.dispatcher_queue_overflow++;
                }
            }
        }

        // --- TX: queue frame to bxCAN mailbox ---
        if ((flags & FLAG_CAN_TX) != 0U) {
            while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0U) == osOK) {

                uint32_t start_tick = osKernelGetTickCount();

                while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U) {
                    if ((osKernelGetTickCount() - start_tick) >= CAN_TX_MAILBOX_TIMEOUT_MS) {
                        break;
                    }
                    osDelay(1U);
                }

                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U) {
                    g_can_diag.tx_mailbox_timeout++;
                    continue;
                }

                if (HAL_CAN_AddTxMessage(&hcan,
                                         &tx_frame.header,
                                         tx_frame.data,
                                         &tx_mailbox) == HAL_OK) {
                    g_can_diag.tx_total++;
                } else {
                    g_can_diag.tx_hal_error++;
                    g_can_diag.last_hal_error = HAL_CAN_GetError(&hcan);
                    g_can_diag.last_esr = hcan.Instance->ESR;
                }
            }
        }
    }
}
