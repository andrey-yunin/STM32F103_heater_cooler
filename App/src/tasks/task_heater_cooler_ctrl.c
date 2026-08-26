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

/*
 * Период ожидания очереди.
 *
 * Тайм-аут нужен для периодического вызова
 * time-proportional PWM GPIO-нагревателей
 * и проверки RES входов.
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
static bool HeaterCooler_IsValidChannel(uint8_t channel)
{
	return channel < CAN_HEATER_COOLER_CHANNELS_DEFAULT;
}

/*
 * Проверяет, относится ли логический канал к Пельтье.
 *
 * Каналы 1 и 2 используют локальные PWM-каналы 0 и 1.
 */
static bool HeaterCooler_IsPeltierChannel(uint8_t channel)
{
	return (channel == HEATER_COOLER_CHANNEL_REAGENT_1) ||
  			(channel == HEATER_COOLER_CHANNEL_REAGENT_2);
}

/*
 * Преобразует логический канал 1..2
 * в локальный индекс драйвера Пельтье 0..1.
 */
static uint8_t HeaterCooler_ToPeltierChannel(uint8_t channel)
{
	return channel - HEATER_COOLER_CHANNEL_REAGENT_1;
}

/*
 * Проверяет, что команда не содержит параметров.
 *
 * ENABLE, DISABLE и SAFE_OFF используют только selector channel.
 * Все пять байт data должны быть нулевыми.
 */
static bool HeaterCooler_IsZeroParameter(
  		const HeaterCoolerCommand_t *command)
{
	if ((command == NULL) ||
  			(command->data_len != HEATER_COOLER_COMMAND_DATA_LEN))
  	{
  		return false;
  	}

  	for (uint8_t index = 0U;
  			index < HEATER_COOLER_COMMAND_DATA_LEN;
  			index++)
  	{
  		if (command->data[index] != 0U)
  		{
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
static bool HeaterCooler_ReadDuty(
  		const HeaterCoolerCommand_t *command,
  		uint8_t *out_duty)
{
	if ((command == NULL) ||
  			(out_duty == NULL) ||
  			(command->data_len != HEATER_COOLER_COMMAND_DATA_LEN))
  	{
  		return false;
  	}

  	if ((command->data[1] != 0U) ||
  			(command->data[2] != 0U) ||
  			(command->data[3] != 0U) ||
  			(command->data[4] != 0U))
  	{
  		return false;
  	}

  	/*
  	 * Duty хранится в процентах.
  	 * Значения 101..255 не передаются в драйверы.
  	 */
  	if (command->data[0] > HEATER_OUTPUT_DUTY_MAX_PERCENT)
  	{
  		return false;
  	}

  	*out_duty = command->data[0];

  	return true;
}

/*
 * Устанавливает duty в аппаратном драйвере выбранного канала.
 *
 * Эта функция не проверяет ENABLE-флаг.
 * Решение о том, можно ли применять duty,
 * принимается вызывающей функцией доменного уровня.
 */
static bool HeaterCooler_ApplyDuty(uint8_t channel,
  								   uint8_t duty_percent)
{
	switch (channel)
  	{
  	case HEATER_COOLER_CHANNEL_SAMPLE_DISK:
  		return HeaterOutputs_SetDuty(
  				HEATER_OUTPUT_SAMPLE_DISK,
  				duty_percent);

  	case HEATER_COOLER_CHANNEL_REAGENT_1:
  		return PeltierPwm_SetDuty(
  				0U,
  				duty_percent);

  	case HEATER_COOLER_CHANNEL_REAGENT_2:
  		return PeltierPwm_SetDuty(
  				1U,
  				duty_percent);

  	case HEATER_COOLER_CHANNEL_SCANNER_GLASS:
  		return HeaterOutputs_SetDuty(
  				HEATER_OUTPUT_SCANNER_GLASS,
  				duty_percent);

  	default:
  		return false;
  	}
}

/*
 * Немедленно выключает один логический канал.
 *
 * Для Пельтье duty=0 оставляет второй Peltier-канал работающим.
 * Поэтому здесь не вызывается общий AppSafety_AllOff().
 */
static bool HeaterCooler_ApplyChannelOff(uint8_t channel)
{
	switch (channel)
  	{
  	case HEATER_COOLER_CHANNEL_SAMPLE_DISK:
  		return HeaterOutputs_Disable(
  				HEATER_OUTPUT_SAMPLE_DISK);

  	case HEATER_COOLER_CHANNEL_REAGENT_1:
  		return PeltierPwm_SetDuty(0U, 0U);

  	case HEATER_COOLER_CHANNEL_REAGENT_2:
  		return PeltierPwm_SetDuty(1U, 0U);

  	case HEATER_COOLER_CHANNEL_SCANNER_GLASS:
  		return HeaterOutputs_Disable(
  				HEATER_OUTPUT_SCANNER_GLASS);

  	default:
  		return false;
  	}
}

/*
 * Включает выбранный канал с ранее принятой duty.
 *
 * Для GPIO-нагревателя сначала восстанавливается duty,
 * затем включается программное разрешение выхода.
 *
 * Для Пельтье при необходимости повторно запускаются
 * оба PWM-таймера.
 */
static bool HeaterCooler_EnableChannel(uint8_t channel)
{
	if (HeaterCooler_IsPeltierChannel(channel))
  	{
  		uint8_t peltier_channel;

  		/*
  		 * После board-wide SAFE_OFF таймеры остановлены.
  		 * Запускаем их перед первым ENABLE.
  		 */
  		if (!peltier_pwm_running)
  		{
  			if (!PeltierPwm_Start())
  			{
  				return false;
  			}

  			peltier_pwm_running = true;
  		}

  		peltier_channel =
  				HeaterCooler_ToPeltierChannel(channel);

  		return PeltierPwm_SetDuty(
  				peltier_channel,
  				heater_cooler_duty[channel]);
  		}

  	if (channel == HEATER_COOLER_CHANNEL_SAMPLE_DISK)
  	{
  		if (!HeaterOutputs_SetDuty(
  				HEATER_OUTPUT_SAMPLE_DISK,
  				heater_cooler_duty[channel]))
  		{
  			return false;
  		}

  		return HeaterOutputs_Enable(
  				HEATER_OUTPUT_SAMPLE_DISK);
  	}

  	if (channel == HEATER_COOLER_CHANNEL_SCANNER_GLASS)
  	{
  		if (!HeaterOutputs_SetDuty(
  				HEATER_OUTPUT_SCANNER_GLASS,
  				heater_cooler_duty[channel]))
  		{
  			return false;
  		}

  		return HeaterOutputs_Enable(
  				HEATER_OUTPUT_SCANNER_GLASS);
  	}

  	return false;
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
static void HeaterCooler_UpdateResFaults(void)
{
	for (uint8_t peltier_channel = 0U;
  			peltier_channel < PELTIER_RES_CHANNEL_COUNT;
  			peltier_channel++)
  	{
  		uint8_t domain_channel =
  				peltier_channel +
  				HEATER_COOLER_CHANNEL_REAGENT_1;

  		if (!PeltierRes_IsFault(peltier_channel))
  		{
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
  		(void)PeltierPwm_SetDuty(
  				peltier_channel,
  				0U);
  	}
}

/*
 * Обрабатывает board-wide SAFE_OFF.
 *
 * SAFE_OFF сбрасывает duty и состояние разрешения всех каналов.
 * Fault latch при этом не очищается.
 */
static void HeaterCooler_HandleSafeOff(
  		const HeaterCoolerCommand_t *command)
  {
  	if (!HeaterCooler_IsZeroParameter(command))
  	{
  		CAN_SendNack(
  				command->cmd_code,
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
  	for (uint8_t channel = 0U;
  			channel < CAN_HEATER_COOLER_CHANNELS_DEFAULT;
  			channel++)
  	{
  		heater_cooler_duty[channel] = 0U;
  		heater_cooler_enabled[channel] = false;
  	}

  	CAN_SendDone(
  			command->cmd_code,
  			0U);
}


/*
 * Обрабатывает одну доменную команду.
 */
static void HeaterCooler_HandleCommand(
  		const HeaterCoolerCommand_t *command)
  {
  	uint8_t channel;
  	uint8_t duty_percent;

  	if (command == NULL)
  	{
  		return;
  	}

  	/*
  	 * SAFE_OFF не адресует отдельный канал
  	 * и обрабатывается отдельно.
  	 */
  	if (command->cmd_code == CAN_CMD_HEATCTRL_SAFE_OFF)
  	{
  		HeaterCooler_HandleSafeOff(command);
  		return;
  	}

  	channel = command->channel;

  	/*
  	 * Dispatcher уже выполняет эту проверку,
  	 * но доменная задача повторяет её как защиту
  	 * перед обращением к аппаратным таблицам.
  	 */
  	if (!HeaterCooler_IsValidChannel(channel))
  	{
  		CAN_SendNack(
  				command->cmd_code,
  				CAN_ERR_HC_INVALID_CHANNEL);
  		return;
  	}

  	/*
  	 * Обновляем RES непосредственно перед обработкой команды,
  	 * чтобы не разрешить ENABLE уже неисправного канала.
  	 */
  	HeaterCooler_UpdateResFaults();

  	switch (command->cmd_code)
  	{
  	case CAN_CMD_HEATCTRL_SET_POWER:

  		/*
  		 * Проверяем формат и диапазон duty.
  		 */
  		if (!HeaterCooler_ReadDuty(
  				command,
  				&duty_percent))
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_INVALID_PARAM);
  			return;
  		}

  		/*
  		 * Защёлкнутый RES fault запрещает изменение мощности
  		 * неисправного канала.
  		 */
  		if (heater_cooler_fault_latched[channel])
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_DEVICE_BUSY);
  			return;
  		}

  		/*
  		 * Сохраняем duty независимо от ENABLE.
  		 *
  		 * Для выключенного GPIO-канала duty хранится драйвером,
  		 * а для выключенного Пельтье физически удерживается ноль.
  		 */
  		heater_cooler_duty[channel] = duty_percent;

  		if (heater_cooler_enabled[channel])
  		{
  			if (!HeaterCooler_ApplyDuty(
  					channel,
  					duty_percent))
  			{
  				CAN_SendNack(
  						command->cmd_code,
  						CAN_ERR_DEVICE_BUSY);
  				return;
  			}
  		}
  		else if (HeaterCooler_IsPeltierChannel(channel))
  		{
  			/*
  			 * SET_POWER не означает ENABLE.
  			 * Поэтому выключенный канал Пельтье
  			 * остаётся физически отключённым.
  			 */
  			(void)HeaterCooler_ApplyChannelOff(channel);
  		}
  		else if (!HeaterCooler_ApplyDuty(
  				channel,
  				duty_percent))
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_DEVICE_BUSY);
  			return;
  		}

  		CAN_SendDone(
  				command->cmd_code,
  				channel);
  		break;

  	case CAN_CMD_HEATCTRL_ENABLE:

  		if (!HeaterCooler_IsZeroParameter(command))
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_INVALID_PARAM);
  			return;
  		}

  		if (heater_cooler_fault_latched[channel])
  		{
  			/*
  			 * Отдельного RES-NACK пока нет в глобальном контракте.
  			 * Недоступный fault-канал временно возвращается
  			 * как DEVICE_BUSY, а подробный fault передаётся через status.
  			 */
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_DEVICE_BUSY);
  			return;
  		}

  		if (!HeaterCooler_EnableChannel(channel))
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_DEVICE_BUSY);
  			return;
  		}

  		heater_cooler_enabled[channel] = true;

  		CAN_SendDone(
  				command->cmd_code,
  				channel);
  		break;

  	case CAN_CMD_HEATCTRL_DISABLE:

  		if (!HeaterCooler_IsZeroParameter(command))
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_INVALID_PARAM);
  			return;
  		}

  		if (!HeaterCooler_ApplyChannelOff(channel))
  		{
  			CAN_SendNack(
  					command->cmd_code,
  					CAN_ERR_DEVICE_BUSY);
  			return;
  		}

  		heater_cooler_enabled[channel] = false;

  		CAN_SendDone(
  				command->cmd_code,
  				channel);
  		break;

  	case CAN_CMD_HEATCTRL_GET_STATUS:

  		/*
  		 * Реальный DATA-контракт status будет подключён
  		 * после завершения raw-to-mV для ADC feedback.
  		 *
  		 * Нельзя отправлять Дирижёру фиктивное feedback_mV.
  		 */
  		CAN_SendNack(
  				command->cmd_code,
  				CAN_ERR_DEVICE_BUSY);
  		break;

  	case CAN_CMD_HEATCTRL_CHANNEL_SELF_TEST:

  		/*
  		 * Self-test относится к следующему уровню реализации.
  		 */
  		CAN_SendNack(
  				command->cmd_code,
  				CAN_ERR_UNKNOWN_CMD);
  		break;

  	default:

  		/*
  		 * Дополнительная защита от неизвестной команды,
  		 * попавшей в domain queue.
  		 */
  		CAN_SendNack(
  				command->cmd_code,
  				CAN_ERR_UNKNOWN_CMD);
  		break;
  	}
}

void app_start_task_heater_cooler_ctrl(void *argument)
{
	HeaterCoolerCommand_t command;

  	(void)argument;

  	/*
  	 * Начальное состояние всех каналов — выключено.
  	 */
  	for (uint8_t channel = 0U;
  			channel < CAN_HEATER_COOLER_CHANNELS_DEFAULT;
  			channel++)
  	{
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

  	if (!peltier_pwm_running)
  	{
  		AppSafety_AllOff();
  	}

  	for (;;)
  	{
  		/*
  		 * RES контролируется независимо от наличия команд
  		 * в domain queue.
  		 */
  		HeaterCooler_UpdateResFaults();

  		/*
  		 * Обновляем time-proportional PWM GPIO-нагревателей.
  		 */
  		HeaterOutputs_Tick(HAL_GetTick());

  		/*
  		 * Тайм-аут очереди позволяет регулярно выполнять
  		 * safety-проверки и обновление GPIO PWM.
  		 */
  		if (osMessageQueueGet(
  				heater_cooler_queueHandle,
  				&command,
  				NULL,
  				HEATER_COOLER_TASK_PERIOD_MS) == osOK)
  		{
  			HeaterCooler_HandleCommand(&command);
  		}
  	}
}

