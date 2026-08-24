/*
 * task_dispatсher.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 *
 * Dispatcher получает уже проверенную и разобранную команду
 * ParsedCanCommand_t от CAN transport task.
 *
 * Ответственность Dispatcher:
 * 1. Отправить ACK после приёма команды.
 * 2. Обработать общие сервисные команды F001..F007.
 * 3. Передать Heater/Cooler-команды доменной задаче.
 * 4. Не смешивать CAN transport с Host-командами.
 */

#include "tasks/task_dispatcher.h"

#include "app_config.h"
#include "app_flash.h"
#include "app_queues.h"
#include "can_protocol.h"
#include "cmsis_os.h"
#include "main.h"
#include "tasks/task_can_handler.h"

#include <string.h>

/*
 * Проверка допустимого динамического NodeID.
 *
 * Broadcast-адрес и адрес Дирижёра не могут быть назначены
 * исполнительной плате.
 */
static bool Dispatcher_IsValidNodeId(uint8_t node_id) {
	if ((node_id < CAN_DYNAMIC_NODE_ID_MIN)
			|| (node_id > CAN_DYNAMIC_NODE_ID_MAX)
			|| (node_id == CAN_ADDR_CONDUCTOR)) {
		return false;
	}
	return true;
}

/*
 * Чтение uint16_t из payload в формате Little-Endian.
 *
 * Используется для Magic Key сервисных команд.
 */
static uint16_t Dispatcher_ReadLe16(const uint8_t *data) {
	return (uint16_t) data[0] | ((uint16_t) data[1] << 8U);
}

/*
 * Формирование одной диагностической метрики F007.
 *
 * DATA-пакет содержит:
 * data[0..1] — metric_id;
 * data[2..5] — значение uint32_t Little-Endian.
 *
 * Каждая метрика является самостоятельным DATA-элементом,
 * поэтому sequence_info всегда равен 0x80.
 */
static void Dispatcher_SendStatusMetric(uint16_t cmd_code, uint16_t metric_id,
		uint32_t value) {
	uint8_t data[CAN_DATA_PAYLOAD_MAX];

	data[0] = (uint8_t) (metric_id & 0xFFU);
	data[1] = (uint8_t) ((metric_id >> 8U) & 0xFFU);

	data[2] = (uint8_t) (value & 0xFFU);
	data[3] = (uint8_t) ((value >> 8U) & 0xFFU);
	data[4] = (uint8_t) ((value >> 16U) & 0xFFU);
	data[5] = (uint8_t) ((value >> 24U) & 0xFFU);

	CAN_SendData(cmd_code,
	CAN_DATA_SEQ_EOT_MASK, data, sizeof(data));
}

/*
 * F001 GET_DEVICE_INFO.
 *
 * Логический ответ содержит 16 байт:
 * device_type, fw_major, fw_minor, channel_count, UID[12].
 *
 * Разбиение:
 * DATA 0x00 — первые 6 байт;
 * DATA 0x01 — следующие 6 байт;
 * DATA 0x82 — последние 4 байта и два нулевых байта заполнения;
 * затем DONE.
 */
static void Dispatcher_HandleGetDeviceInfo(void) {
	uint8_t uid[12];
	uint8_t data[CAN_DATA_PAYLOAD_MAX];

	AppConfig_GetMCU_UID(uid);

	data[0] = CAN_DEVICE_TYPE_HEATER_COOLER;
	data[1] = FW_REV_MAJOR;
	data[2] = FW_REV_MINOR;
	data[3] = CAN_HEATER_COOLER_CHANNELS_DEFAULT;
	data[4] = uid[0];
	data[5] = uid[1];

	CAN_SendData(CAN_CMD_SRV_GET_DEVICE_INFO, 0x00U, data, sizeof(data));

	memcpy(&data[0], &uid[2], 6U);

	CAN_SendData(CAN_CMD_SRV_GET_DEVICE_INFO, 0x01U, data, sizeof(data));

	memset(data, 0, sizeof(data));
	memcpy(&data[0], &uid[8], 4U);

	CAN_SendData(CAN_CMD_SRV_GET_DEVICE_INFO,
			(uint8_t) (CAN_DATA_SEQ_EOT_MASK | 0x02U), data, sizeof(data));

	CAN_SendDone(CAN_CMD_SRV_GET_DEVICE_INFO, 0U);
}

/*
 * F004 GET_UID.
 *
 * Полный 96-битный UID передаётся двумя фрагментами:
 * DATA 0x00 — UID[0..5];
 * DATA 0x81 — UID[6..11] и EOT.
 */
static void Dispatcher_HandleGetUid(void) {
	uint8_t uid[12];

	AppConfig_GetMCU_UID(uid);

	CAN_SendData(CAN_CMD_SRV_GET_UID, 0x00U, &uid[0], 6U);

	CAN_SendData(CAN_CMD_SRV_GET_UID, (uint8_t) (CAN_DATA_SEQ_EOT_MASK | 0x01U),
			&uid[6], 6U);

	CAN_SendDone(CAN_CMD_SRV_GET_UID, 0U);
}

/*
 * F007 GET_STATUS.
 *
 * Snapshot считывается один раз, чтобы все метрики относились
 * к одному диагностическому состоянию транспорта.
 */
static void Dispatcher_HandleGetStatus(void) {
	CanDiagnostics_t diagnostics;

	CAN_Diagnostics_GetSnapshot(&diagnostics);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_RX_TOTAL, diagnostics.rx_total);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_TOTAL, diagnostics.tx_total);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_RX_QUEUE_OVERFLOW, diagnostics.rx_queue_overflow);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_QUEUE_OVERFLOW, diagnostics.tx_queue_overflow);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DISPATCHER_OVERFLOW, diagnostics.dispatcher_queue_overflow);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_NOT_EXT, diagnostics.dropped_not_ext);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_WRONG_DST, diagnostics.dropped_wrong_dst);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_WRONG_TYPE, diagnostics.dropped_wrong_type);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_WRONG_DLC, diagnostics.dropped_wrong_dlc);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_MAILBOX_TIMEOUT, diagnostics.tx_mailbox_timeout);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_HAL_ERROR, diagnostics.tx_hal_error);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_ERROR_CALLBACK, diagnostics.can_error_callback_count);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_ERROR_WARNING, diagnostics.error_warning_count);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_ERROR_PASSIVE, diagnostics.error_passive_count);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_BUS_OFF, diagnostics.bus_off_count);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_LAST_HAL_ERROR, diagnostics.last_hal_error);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_LAST_ESR, diagnostics.last_esr);

	Dispatcher_SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_APP_QUEUE_OVERFLOW, diagnostics.app_queue_overflow);

	CAN_SendDone(CAN_CMD_SRV_GET_STATUS, 0U);
}

/*
 * Обработка сервисных команд F001..F007.
 *
 * Magic Key находится в command->data[0..1], поскольку data[]
 * содержит payload bytes 3..7 исходного CAN COMMAND-кадра.
 */
static void Dispatcher_HandleServiceCommand(const ParsedCanCommand_t *command) {
	switch (command->cmd_code) {
	case CAN_CMD_SRV_GET_DEVICE_INFO:
		Dispatcher_HandleGetDeviceInfo();
		break;

	case CAN_CMD_SRV_REBOOT:
		if ((command->data_len < 2U)
				|| (Dispatcher_ReadLe16(command->data) != SRV_MAGIC_REBOOT)) {
			CAN_SendNack(command->cmd_code, CAN_ERR_INVALID_KEY);
			break;
		}

		CAN_SendDone(command->cmd_code, 0U);

		/*
		 * DONE уже поставлен в TX queue.
		 * Небольшая задержка позволяет CAN task отправить ответ.
		 */

		osDelay(100U);
		NVIC_SystemReset();
		break;

	case CAN_CMD_SRV_FLASH_COMMIT:
		if (AppConfig_Commit()) {
			CAN_SendDone(command->cmd_code, 0U);
		}

		else {
			CAN_SendNack(command->cmd_code, CAN_ERR_FLASH_WRITE);
		}
		break;

	case CAN_CMD_SRV_GET_UID:
		Dispatcher_HandleGetUid();
		break;

	case CAN_CMD_SRV_SET_NODE_ID:
		if (!Dispatcher_IsValidNodeId(command->device_id)) {
			CAN_SendNack(command->cmd_code,
			CAN_ERR_INVALID_DEVICE_ID);
			break;
		}

		/*
		 * Новый NodeID применяется в RAM.
		 * Сохранение во Flash выполняется отдельной командой F003.
		 */
		AppConfig_SetPerformerID(command->device_id);

		/*
		 * Direct-фильтр обновляется до отправки DONE,
		 * чтобы плата сразу принимала команды по новому адресу.
		 */
		CAN_UpdateDirectFilter(command->device_id);

		CAN_SendDone(command->cmd_code, command->device_id);
		break;

	case CAN_CMD_SRV_FACTORY_RESET:
		if ((command->data_len < 2U) || (Dispatcher_ReadLe16(command->data) !=
		SRV_MAGIC_FACTORY_RESET)) {
			CAN_SendNack(command->cmd_code, CAN_ERR_INVALID_KEY);
			break;
		}

		AppConfig_FactoryReset();

		CAN_SendDone(command->cmd_code, 0U);

		osDelay(100U);
		NVIC_SystemReset();
		break;

	case CAN_CMD_SRV_GET_STATUS:
		Dispatcher_HandleGetStatus();
		break;

	default:
		CAN_SendNack(command->cmd_code, CAN_ERR_UNKNOWN_CMD);
		break;
	}
}

/*
 * Проверка принадлежности команды к Heater/Cooler domain.
 */
static bool Dispatcher_IsHeaterCoolerCommand(uint16_t cmd_code) {
	switch (cmd_code) {
	case CAN_CMD_HEATCTRL_SET_POWER:
	case CAN_CMD_HEATCTRL_ENABLE:
	case CAN_CMD_HEATCTRL_DISABLE:
	case CAN_CMD_HEATCTRL_CHANNEL_SELF_TEST:
	case CAN_CMD_HEATCTRL_GET_STATUS:
	case CAN_CMD_HEATCTRL_SAFE_OFF:
		return true;

	default:
		return false;
	}
}

/*
 * Передача доменной команды в task_heater_cooler.
 *
 * Dispatcher не выполняет физическое действие сам.
 * Он только формирует HeaterCoolerCommand_t и помещает его
 * в доменную очередь.
 */
static void Dispatcher_HandleHeaterCoolerCommand(
		const ParsedCanCommand_t *command) {
	HeaterCoolerCommand_t heater_command;

	/*
	 * SAFE_OFF является board-wide командой.
	 * Для неё selector byte должен быть нулевым.
	 */
	if ((command->cmd_code == CAN_CMD_HEATCTRL_SAFE_OFF)
			&& (command->device_id != 0U)) {
		CAN_SendNack(command->cmd_code,
		CAN_ERR_HC_INVALID_CHANNEL);
		return;
	}

	/*
	 * Остальные Heater/Cooler-команды адресуют конкретный
	 * локальный канал 0..3.
	 */
	if ((command->cmd_code != CAN_CMD_HEATCTRL_SAFE_OFF)
			&& (command->device_id >= CAN_HEATER_COOLER_CHANNELS_DEFAULT)) {
		CAN_SendNack(command->cmd_code,
		CAN_ERR_HC_INVALID_CHANNEL);
		return;
	}

	memset(&heater_command, 0, sizeof(heater_command));

	heater_command.cmd_code = command->cmd_code;
	heater_command.channel =
			(command->cmd_code == CAN_CMD_HEATCTRL_SAFE_OFF) ?
					HEATER_COOLER_INVALID_CHANNEL : command->device_id;

	heater_command.data_len = command->data_len;

	memcpy(heater_command.data, command->data, sizeof(heater_command.data));

	if (osMessageQueuePut(heater_cooler_queueHandle, &heater_command, 0U, 0U)
			!= osOK) {
		CAN_Diagnostics_RecordAppQueueOverflow();

		CAN_SendNack(command->cmd_code,
		CAN_ERR_DEVICE_BUSY);

		return;
	}

	/*
	 * DONE/NACK здесь не отправляется.
	 * Финальный результат отправит task_heater_cooler
	 * после фактического выполнения команды.
	 */
}

/*
 * Основной цикл Dispatcher.
 *
 * ACK означает, что команда принята приложением для обработки.
 * DONE или NACK сообщает окончательный результат.
 */
void app_start_task_dispatcher(void *argument) {
	ParsedCanCommand_t command;

	(void) argument;

	for (;;) {
		if (osMessageQueueGet(dispatcher_queueHandle, &command,
		NULL,
		osWaitForever) != osOK) {
			continue;
		}

		/*
		 * ACK отправляется до прикладной обработки команды.
		 */
		CAN_SendAck(command.cmd_code);

		/*
		 * Сервисный диапазон F001..F007.
		 */
		if ((command.cmd_code >= CAN_CMD_SRV_GET_DEVICE_INFO)
				&& (command.cmd_code <= CAN_CMD_SRV_GET_STATUS)) {
			Dispatcher_HandleServiceCommand(&command);
		}
		/*
		 * Низкоуровневые Heater/Cooler-команды 0x08xx.
		 */
		else if (Dispatcher_IsHeaterCoolerCommand(command.cmd_code)) {
			Dispatcher_HandleHeaterCoolerCommand(&command);
		}
		/*
		 * Команда не принадлежит ни сервисному, ни доменному контракту.
		 */
		else {
			CAN_SendNack(command.cmd_code,
			CAN_ERR_UNKNOWN_CMD);
		}
	}
}
