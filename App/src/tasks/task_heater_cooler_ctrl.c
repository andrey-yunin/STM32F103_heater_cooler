/*
 * task_heater_cooler_ctrl.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 *
 *
 *
 * Доменная задача Heater/Cooler.
 *
 * Ответственность задачи:
 *   - принимать HeaterCoolerCommand_t из domain queue;
 *   - применять SET_POWER, ENABLE и DISABLE;
 *   - выполнять board-wide SAFE_OFF;
 *   - контролировать RES каналов Пельтье;
 *   - фиксировать fault неисправного канала;
 *   - отправлять финальный DONE или NACK.
 *
 * Температурное регулирование здесь не выполняется.
 * Duty рассчитывается Дирижёром и приходит через SET_POWER.
 */

#include "tasks/task_heater_cooler_ctrl.h"

#include "app_config.h"
#include "app_queues.h"
#include "app_safety.h"
#include "can_protocol.h"
#include "cmsis_os.h"
#include "main.h"

#include "drivers/heater_outputs.h"
#include "drivers/peltier_pwm.h"
#include "drivers/peltier_res.h"

// --- Период обслуживания защиты ---

/*
 * Ограничивает ожидание очереди, чтобы задача периодически
 * проверяла RES даже при отсутствии команд.
 * Программный PWM для GPIO-нагревателей не используется.
 */

#define HEATER_COOLER_TASK_PERIOD_MS 10U

/*
 * В parsed command всегда копируются payload bytes 3..7:
 * четыре байта параметра и один резервный байт.
 */
#define HEATER_COOLER_COMMAND_DATA_LEN 5U

/*
 * Состояние четырёх логических каналов.
 *
 * channel 0 — GPIO heater sample disk;
 * channel 1 — Peltier local channel 0;
 * channel 2 — Peltier local channel 1;
 * channel 3 — GPIO heater scanner glass.
 */
static uint8_t heater_cooler_duty[CAN_HEATER_COOLER_CHANNELS_DEFAULT];

static bool heater_cooler_enabled[CAN_HEATER_COOLER_CHANNELS_DEFAULT];

static bool heater_cooler_fault_latched[CAN_HEATER_COOLER_CHANNELS_DEFAULT];

/*
 * Состояние аппаратного запуска PWM.
 *
 * После SAFE_OFF таймеры Пельтье остановлены.
 * Перед последующим ENABLE они должны быть запущены снова.
 */
static bool peltier_pwm_running;

/* Проверяет общий логический номер канала 0..3. */
static bool HeaterCooler_IsValidChannel(uint8_t channel) {
	return channel < CAN_HEATER_COOLER_CHANNELS_DEFAULT;
}

/*
 * Проверяет, относится ли логический канал к Пельтье.
 *
 * Каналы 1 и 2 используют локальные PWM-каналы 0 и 1.
 */
static bool HeaterCooler_IsPeltierChannel(uint8_t channel) {
	return (channel == HEATER_COOLER_CHANNEL_REAGENT_1)
			|| (channel == HEATER_COOLER_CHANNEL_REAGENT_2);
}

/*
 * Преобразует логический канал 1..2
 * в локальный индекс драйвера Пельтье 0..1.
 */
static uint8_t HeaterCooler_ToPeltierChannel(uint8_t channel) {
	return channel - HEATER_COOLER_CHANNEL_REAGENT_1;
}

/*
 * Проверяет, что команда не содержит параметров.
 *
 * ENABLE, DISABLE и SAFE_OFF используют только selector channel.
 * Все пять байт data должны быть нулевыми.
 */
static bool HeaterCooler_IsZeroParameter(const HeaterCoolerCommand_t *command) {
	if ((command == NULL)
			|| (command->data_len != HEATER_COOLER_COMMAND_DATA_LEN)) {
		return false;
	}

	for (uint8_t index = 0U; index < HEATER_COOLER_COMMAND_DATA_LEN; index++) {
		if (command->data[index] != 0U) {
			return false;
		}
	}

	return true;
}

/*
 * Читает duty из параметра SET_POWER.
 *
 * Формат параметра на CAN:
 *
 *   data[0] — duty 0..100%;
 *   data[1] — старший байт uint32 параметра;
 *   data[2] — старший байт uint32 параметра;
 *   data[3] — старший байт uint32 параметра;
 *   data[4] — резерв.
 *
 * Все байты, кроме data[0], должны быть нулевыми.
 */
static bool HeaterCooler_ReadDuty(const HeaterCoolerCommand_t *command,
		uint8_t *out_duty) {
	if ((command == NULL) || (out_duty == NULL)
			|| (command->data_len != HEATER_COOLER_COMMAND_DATA_LEN)) {
		return false;
	}

	if ((command->data[1] != 0U) || (command->data[2] != 0U)
			|| (command->data[3] != 0U) || (command->data[4] != 0U)) {
		return false;
	}

	if (command->data[0] > PELTIER_PWM_DUTY_MAX_PERCENT) {
		return false;
	}

	*out_duty = command->data[0];

	return true;
}

// --- Применение duty Пельтье ---

/*
 * Управляет только PWM-каналами Пельтье.
 * Разрешение канала проверяет вызывающий доменный обработчик.
 * GPIO-нагреватели не принимают duty.
 */
static bool HeaterCooler_ApplyDuty(uint8_t channel, uint8_t duty_percent) {
	if (!HeaterCooler_IsPeltierChannel(channel)) {
		return false;
	}

	return PeltierPwm_SetDuty(HeaterCooler_ToPeltierChannel(channel),
			duty_percent);
}

/*
 * Немедленно выключает один логический канал.
 *
 * Для Пельтье duty=0 оставляет второй Peltier-канал работающим.
 * Поэтому здесь не вызывается общий AppSafety_AllOff().
 */
static bool HeaterCooler_ApplyChannelOff(uint8_t channel) {
	switch (channel) {
	case HEATER_COOLER_CHANNEL_SAMPLE_DISK:
		return HeaterOutputs_Disable(HEATER_OUTPUT_SAMPLE_DISK);

	case HEATER_COOLER_CHANNEL_REAGENT_1:
		return PeltierPwm_SetDuty(0U, 0U);

	case HEATER_COOLER_CHANNEL_REAGENT_2:
		return PeltierPwm_SetDuty(1U, 0U);

	case HEATER_COOLER_CHANNEL_SCANNER_GLASS:
		return HeaterOutputs_Disable(HEATER_OUTPUT_SCANNER_GLASS);

	default:
		return false;
	}
}

// --- Включение логического канала ---

/*
 * Нагреватель включается постоянным уровнем GPIO.
 * Пельтье включается с сохранённым duty; после SAFE_OFF
 * предварительно восстанавливается работа PWM-таймеров.
 * Проверка fault выполняется до вызова этой функции.
 */
static bool HeaterCooler_EnableChannel(uint8_t channel) {
	if (HeaterCooler_IsPeltierChannel(channel)) {
		// --- Восстановление PWM после общего отключения ---

		/*
		 * PeltierPwm_Start() запускает оба таймера с нулевым duty.
		 * После успешного запуска применяем duty выбранного канала.
		 */
		if (!peltier_pwm_running) {
			if (!PeltierPwm_Start()) {
				return false;
			}

			peltier_pwm_running = true;
		}

		return PeltierPwm_SetDuty(HeaterCooler_ToPeltierChannel(channel),
				heater_cooler_duty[channel]);
	}

	// --- Прямое включение GPIO-нагревателя ---

	/* Явно преобразуем канал CAN в локальный enum драйвера. */
	switch (channel) {
	case HEATER_COOLER_CHANNEL_SAMPLE_DISK:
		return HeaterOutputs_Enable(HEATER_OUTPUT_SAMPLE_DISK);

	case HEATER_COOLER_CHANNEL_SCANNER_GLASS:
		return HeaterOutputs_Enable(HEATER_OUTPUT_SCANNER_GLASS);

	default:
		return false;
	}
}

/*
 * Проверяет RES-входы обоих каналов Пельтье.
 *
 * При active-low fault:
 *   - соответствующий PWM переводится в duty=0;
 *   - канал выключается;
 *   - fault фиксируется latch-ом;
 *   - повторный ENABLE блокируется.
 *
 * Остальные каналы не отключаются автоматически.
 * STB CAN-трансивера не изменяется.
 */
static void HeaterCooler_UpdateResFaults(void) {
	for (uint8_t peltier_channel = 0U;
			peltier_channel < PELTIER_RES_CHANNEL_COUNT; peltier_channel++) {
		uint8_t domain_channel = peltier_channel +
		HEATER_COOLER_CHANNEL_REAGENT_1;

		if (!PeltierRes_IsFault(peltier_channel)) {
			continue;
		}

		/*
		 * Сохраняем fault до разрешённого recovery.
		 * Автоматическое снятие защёлки не выполняется.
		 */
		heater_cooler_fault_latched[domain_channel] = true;
		heater_cooler_enabled[domain_channel] = false;

		/*
		 * Отключаем только неисправный канал Пельтье.
		 */
		(void) PeltierPwm_SetDuty(peltier_channel, 0U);
	}
}

/*
 * Обрабатывает board-wide SAFE_OFF.
 *
 * SAFE_OFF сбрасывает duty и состояние разрешения всех каналов.
 * Fault latch при этом не очищается.
 */
static void HeaterCooler_HandleSafeOff(const HeaterCoolerCommand_t *command) {
	if (!HeaterCooler_IsZeroParameter(command)) {
		CAN_SendNack(command->cmd_code,
		CAN_ERR_INVALID_PARAM);
		return;
	}

	/*
	 * Выключаем все тепловые выходы.
	 * CAN-трансивер остаётся активным.
	 */
	AppSafety_AllOff();
	peltier_pwm_running = false;

	/*
	 * После SAFE_OFF все каналы выключены,
	 * а следующий SET_POWER только сохраняет duty.
	 */
	for (uint8_t channel = 0U; channel < CAN_HEATER_COOLER_CHANNELS_DEFAULT;
			channel++) {
		heater_cooler_duty[channel] = 0U;
		heater_cooler_enabled[channel] = false;
	}

	CAN_SendDone(command->cmd_code, 0U);
}

/*
 * Обрабатывает одну доменную команду.
 */
static void HeaterCooler_HandleCommand(const HeaterCoolerCommand_t *command) {
	uint8_t channel;
	uint8_t duty_percent;

	if (command == NULL) {
		return;
	}

	/*
	 * SAFE_OFF не адресует отдельный канал
	 * и обрабатывается отдельно.
	 */
	if (command->cmd_code == CAN_CMD_HEATCTRL_SAFE_OFF) {
		HeaterCooler_HandleSafeOff(command);
		return;
	}

	channel = command->channel;

	/*
	 * Dispatcher уже выполняет эту проверку,
	 * но доменная задача повторяет её как защиту
	 * перед обращением к аппаратным таблицам.
	 */
	if (!HeaterCooler_IsValidChannel(channel)) {
		CAN_SendNack(command->cmd_code,
		CAN_ERR_HC_INVALID_CHANNEL);
		return;
	}

	/*
	 * Обновляем RES непосредственно перед обработкой команды,
	 * чтобы не разрешить ENABLE уже неисправного канала.
	 */
	HeaterCooler_UpdateResFaults();

	switch (command->cmd_code) {
	case CAN_CMD_HEATCTRL_SET_POWER:

		/*
		 * Номер канала уже проверен перед switch.
		 * Для GPIO-нагревателей SET_POWER недопустим:
		 * возвращаем NACK без изменения их выхода.
		 */
		if (!HeaterCooler_IsPeltierChannel(channel)) {
			CAN_SendNack(command->cmd_code, CAN_ERR_INVALID_PARAM);
			return;
		}

		// --- Проверка параметров и защиты ---

		/* Проверяем полный параметр и диапазон duty 0..100%. */
		if (!HeaterCooler_ReadDuty(command, &duty_percent)) {
			CAN_SendNack(command->cmd_code, CAN_ERR_INVALID_PARAM);
			return;
		}

		/* Защёлкнутый RES fault блокирует управление каналом. */
		if (heater_cooler_fault_latched[channel]) {
			CAN_SendNack(command->cmd_code, CAN_ERR_DEVICE_BUSY);
			return;
		}

		// --- Применение с учётом разрешения канала ---

		/*
		 * SET_POWER не включает запрещённый канал.
		 * Разрешённому каналу применяем duty сразу;
		 * выключенный физически удерживаем в нуле.
		 */
		if (heater_cooler_enabled[channel]) {
			if (!HeaterCooler_ApplyDuty(channel, duty_percent)) {
				CAN_SendNack(command->cmd_code, CAN_ERR_DEVICE_BUSY);
				return;
			}
		} else if (!HeaterCooler_ApplyChannelOff(channel)) {
			CAN_SendNack(command->cmd_code, CAN_ERR_DEVICE_BUSY);
			return;
		}

		// --- Заданный duty Пельтье ---

		/*
		 * Индексация по общему номеру канала.
		 * Рабочие элементы — 1 и 2; для нагревателей 0 и 3
		 * duty не применяется.
		 */

		heater_cooler_duty[channel] = duty_percent;

		CAN_SendDone(command->cmd_code, channel);
		break;

	case CAN_CMD_HEATCTRL_ENABLE:

		if (!HeaterCooler_IsZeroParameter(command)) {
			CAN_SendNack(command->cmd_code,
			CAN_ERR_INVALID_PARAM);
			return;
		}

		if (heater_cooler_fault_latched[channel]) {
			/*
			 * Отдельного RES-NACK пока нет в глобальном контракте.
			 * Недоступный fault-канал временно возвращается
			 * как DEVICE_BUSY, а подробный fault передаётся через status.
			 */
			CAN_SendNack(command->cmd_code,
			CAN_ERR_DEVICE_BUSY);
			return;
		}

		if (!HeaterCooler_EnableChannel(channel)) {
			CAN_SendNack(command->cmd_code,
			CAN_ERR_DEVICE_BUSY);
			return;
		}

		heater_cooler_enabled[channel] = true;

		CAN_SendDone(command->cmd_code, channel);
		break;

	case CAN_CMD_HEATCTRL_DISABLE:

		if (!HeaterCooler_IsZeroParameter(command)) {
			CAN_SendNack(command->cmd_code,
			CAN_ERR_INVALID_PARAM);
			return;
		}

		if (!HeaterCooler_ApplyChannelOff(channel)) {
			CAN_SendNack(command->cmd_code,
			CAN_ERR_DEVICE_BUSY);
			return;
		}

		heater_cooler_enabled[channel] = false;

		CAN_SendDone(command->cmd_code, channel);
		break;

	case CAN_CMD_HEATCTRL_GET_STATUS:

		/*
		 * Реальный DATA-контракт status будет подключён
		 * после завершения raw-to-mV для ADC feedback.
		 *
		 * Нельзя отправлять Дирижёру фиктивное feedback_mV.
		 */
		CAN_SendNack(command->cmd_code,
		CAN_ERR_DEVICE_BUSY);
		break;

	case CAN_CMD_HEATCTRL_CHANNEL_SELF_TEST:

		/*
		 * Self-test относится к следующему уровню реализации.
		 */
		CAN_SendNack(command->cmd_code,
		CAN_ERR_UNKNOWN_CMD);
		break;

	default:

		/*
		 * Дополнительная защита от неизвестной команды,
		 * попавшей в domain queue.
		 */
		CAN_SendNack(command->cmd_code,
		CAN_ERR_UNKNOWN_CMD);
		break;
	}
}

void app_start_task_heater_cooler_ctrl(void *argument) {
	HeaterCoolerCommand_t command;

	(void) argument;

	/*
	 * Начальное состояние всех каналов — выключено.
	 */
	for (uint8_t channel = 0U; channel < CAN_HEATER_COOLER_CHANNELS_DEFAULT;
			channel++) {
		heater_cooler_duty[channel] = 0U;
		heater_cooler_enabled[channel] = false;
		heater_cooler_fault_latched[channel] = false;
	}

	/*
	 * GPIO-нагреватели переводятся в safe-off
	 * до обработки первой команды.
	 */
	HeaterOutputs_AllOff();

	/*
	 * Запускаем PWM Пельтье с duty=0.
	 *
	 * Даже при ошибке запуска задача остаётся активной,
	 * чтобы исполнитель мог отвечать по CAN.
	 */
	peltier_pwm_running = PeltierPwm_Start();

	if (!peltier_pwm_running) {
		AppSafety_AllOff();
	}

	for (;;) {
		/*
		 * RES контролируется независимо от наличия команд
		 * в domain queue.
		 */
		HeaterCooler_UpdateResFaults();

		/*
		 * Тайм-аут ожидания очереди позволяет периодически
		 * проверять RES даже при отсутствии команд.
		 */

		if (osMessageQueueGet(heater_cooler_queueHandle, &command,
		NULL,
		HEATER_COOLER_TASK_PERIOD_MS) == osOK) {
			HeaterCooler_HandleCommand(&command);
		}
	}
}

