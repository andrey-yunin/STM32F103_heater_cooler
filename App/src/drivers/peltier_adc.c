/*
 * peltier_adc.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Драйвер чтения ADC feedback двух каналов Пельтье.
 *
 * Используется стандартная схема STM32:
 *
 *   ADC1 regular scan
 *        |
 *        | rank 1: PA0
 *        | rank 2: PA1
 *        v
 *   ADC1->DR
 *        |
 *        | DMA request после каждого преобразования
 *        v
 *   peltier_adc_dma_buffer[0..1]
 *
 * DMA не определяет номер канала самостоятельно.
 * Привязка к каналу обеспечивается фиксированным порядком
 * rank в конфигурации ADC и порядком записи DMA в память.
 *
 *
 */

#include "drivers/peltier_adc.h"

#include "main.h"

#include <stddef.h>

/* ADC1 объявлен CubeMX в main.c. */
extern ADC_HandleTypeDef hadc1;

/*
 * Значение-заполнитель DMA-буфера.
 *
 * До начала преобразования оба элемента получают 0xFFFF.
 * После полного DMA-кадра каждый элемент обязан находиться
 * в диапазоне 0..4095.
 */
#define PELTIER_ADC_DMA_INVALID_RAW 0xFFFFU


/*
 * Буфер DMA должен быть статическим:
 * DMA продолжает обращаться к нему после выхода
 * из функции HAL_ADC_Start_DMA().
 *
 * volatile указывает компилятору, что содержимое буфера
 * может изменяться аппаратно без выполнения инструкций CPU.
 */
static volatile uint16_t peltier_adc_dma_buffer[PELTIER_ADC_CHANNEL_COUNT];

/*
 * Эти флаги изменяются из DMA callback и читаются
 * из основного кода драйвера.
 */
static volatile bool peltier_adc_dma_complete;
static volatile bool peltier_adc_dma_error;
static volatile bool peltier_adc_transfer_active;

/* Проверяет локальный индекс ADC-канала. */
static bool PeltierAdc_IsValid(uint8_t channel)
{
	return channel < PELTIER_ADC_CHANNEL_COUNT;
}

/*
 * Подготавливает состояние перед запуском новой DMA-последовательности.
 *
 * Старый буфер нельзя принимать за новое измерение,
 * поэтому перед каждым запуском он заполняется недействительным
 * значением 0xFFFF.
 */
static void PeltierAdc_PrepareTransfer(void)
{
	for (uint8_t index = 0U;
			index < PELTIER_ADC_CHANNEL_COUNT;
			index++)
		{
		peltier_adc_dma_buffer[index] =
  				PELTIER_ADC_DMA_INVALID_RAW;
  	}

  	peltier_adc_dma_complete = false;
  	peltier_adc_dma_error = false;
  	peltier_adc_transfer_active = true;
}

/*
 * Выполняет одну попытку получения полного DMA-кадра.
 *
 * HAL_ADC_Start_DMA() сам выполняет правильный порядок запуска:
 * сначала настраивает DMA на передачу ADC1->DR,
 * затем запускает ADC-преобразование.
 */
static bool PeltierAdc_ReadSequenceOnce(
		uint16_t samples[PELTIER_ADC_CHANNEL_COUNT])
{
	uint32_t start_time_ms;
  	bool frame_complete;
  	bool transfer_error;
  	HAL_StatusTypeDef stop_status;

  	/*
  	 * Одновременный запуск двух последовательностей недопустим:
  	 * второй запуск мог бы изменить общий DMA-буфер.
  	 */
  	if (peltier_adc_transfer_active)
  	{
  		return false;
  	}

  	/* Очищаем старое состояние и старые результаты. */
  	PeltierAdc_PrepareTransfer();

  	/*
  	 * Передаём DMA адрес ADC1->DR, адрес буфера и длину
  	 * полной последовательности из двух half-word значений.
  	 */
  	if (HAL_ADC_Start_DMA(&hadc1,
  			(uint32_t *)(void *)peltier_adc_dma_buffer,
  			PELTIER_ADC_CHANNEL_COUNT) != HAL_OK)
  	{
  		peltier_adc_transfer_active = false;
  		return false;
  	}

  	start_time_ms = HAL_GetTick();

  	/*
  	 * Ожидаем именно callback полного DMA-кадра.
  	 *
  	 * Здесь нет HAL_ADC_PollForConversion() и нет чтения DR
  	 * программой после каждого rank. Значения переносит DMA.
  	 */
  	while (!peltier_adc_dma_complete &&
  			!peltier_adc_dma_error)
  	{
  		/*
  		 * Проверка времени выполняется по unsigned-разности,
  		 * поэтому корректно работает и при переполнении HAL tick.
  		 */
  		if ((HAL_GetTick() - start_time_ms) >=
  				PELTIER_ADC_DMA_TIMEOUT_MS)
  		{
  			break;
  		}
  	}

  	/*
  	 * Сохраняем состояние до остановки ADC/DMA.
  	 * После тайм-аута остановка может прервать незавершённый кадр.
  	 */
  	frame_complete = peltier_adc_dma_complete;
  	transfer_error = peltier_adc_dma_error;

  	/*
  	 * В Normal mode DMA после полного кадра уже остановлен.
  	 * После ошибки или тайм-аута HAL_ADC_Stop_DMA()
  	 * дополнительно прекращает возможное незавершённое преобразование.
  	 */
  	stop_status = HAL_ADC_Stop_DMA(&hadc1);

  	/* После остановки текущая передача больше не считается активной. */
  	peltier_adc_transfer_active = false;

  	/*
  	 * Принимаем только полный кадр без ошибки и только после
  	 * успешной остановки ADC/DMA.
  	 */
  	if ((stop_status != HAL_OK) ||
  			!frame_complete ||
  			transfer_error)
  	{
  		return false;
  	}

  	/*
  	 * Проверяем каждый результат.
  	 *
  	 * Если DMA записал только часть кадра, остальной элемент
  	 * останется равен 0xFFFF и будет отвергнут этим условием.
  	 */
  	for (uint8_t index = 0U;
  			index < PELTIER_ADC_CHANNEL_COUNT;
  			index++)
  	{
  		if (peltier_adc_dma_buffer[index] >
  				PELTIER_ADC_MAX_RAW)
  		{
  			return false;
  		}

  		samples[index] = peltier_adc_dma_buffer[index];
  	}

  	return true;
  }

  /*
   * Callback вызывается HAL после полного DMA-переноса.
   *
   * К этому моменту DMA уже записал оба значения:
   * raw[0] соответствует rank 1,
   * raw[1] соответствует rank 2.
   */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
	if ((hadc == &hadc1) &&
  			peltier_adc_transfer_active)
  	{
  		/*
  		 * Барьер памяти гарантирует, что записи DMA в буфер
  		 * наблюдаются CPU до обработки флага завершения.
  		 */
  		__DMB();

  		peltier_adc_dma_complete = true;
  	}
}

/*
 * Callback ошибки ADC или DMA.
 *
 * Ошибочный или неполный кадр никогда не передаётся
 * вызывающему коду как достоверное измерение.
 */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
	if ((hadc == &hadc1) &&
  			peltier_adc_transfer_active)
  	{
  		peltier_adc_dma_error = true;
  	}
}

bool PeltierAdc_ReadFrame(PeltierAdcFrame_t *out_frame)
{
	uint16_t samples[PELTIER_ADC_CHANNEL_COUNT];

  	/* Проверяем адрес структуры результата. */
  	if (out_frame == NULL)
  	{
  		return false;
  	}

  	/*
  	 * Очищаем выходную структуру до начала работы.
  	 * Значения считаются действительными только при возврате true.
  	 */
  	for (uint8_t index = 0U;
  			index < PELTIER_ADC_CHANNEL_COUNT;
  			index++)
  	{
  		out_frame->raw[index] = 0U;
  	}

  	/*
  	 * Повторяем полный кадр целиком.
  	 *
  	 * Нельзя повторять только один rank:
  	 * это нарушило бы соответствие порядка измерений
  	 * и положения в DMA-буфере.
  	 */
  	for (uint8_t attempt = 0U;
  			attempt <= PELTIER_ADC_DMA_RETRY_COUNT;
  			attempt++)
  	{
  		if (PeltierAdc_ReadSequenceOnce(samples))
  		{
  			/*
  			 * Копируем кадр только после полной проверки
  			 * обоих DMA-результатов.
  			 */
  			for (uint8_t index = 0U;
  					index < PELTIER_ADC_CHANNEL_COUNT;
  					index++)
  			{
  				out_frame->raw[index] = samples[index];
  			}

  			return true;
  		}
  	}

  	/*
  	 * Все попытки исчерпаны.
  	 *
  	 * Драйвер не принимает решение о температуре и не выключает
  	 * силовые выходы самостоятельно. Доменный уровень обязан
  	 * перевести систему в safe-off и зафиксировать fault.
  	 */
  	return false;
  }

bool PeltierAdc_ReadRaw(uint8_t channel,
						uint16_t *out_raw)
{
	PeltierAdcFrame_t frame;

  	/*
  	 * Проверяем канал и указатель результата до запуска ADC.
  	 */
  	if (!PeltierAdc_IsValid(channel) ||
  			(out_raw == NULL))
  	{
  		return false;
  	}

  	/*
  	 * Даже при запросе одного канала считывается вся
  	 * двухканальная последовательность ADC.
  	 */
  	if (!PeltierAdc_ReadFrame(&frame))
  	{
  		return false;
  	}

  	/*
  	 * Номер логического канала совпадает с индексом rank:
  	 * channel 0 — rank 1,
  	 * channel 1 — rank 2.
  	 */
  	*out_raw = frame.raw[channel];

  	return true;
}

bool PeltierAdc_ReadFeedback_mV(uint8_t channel,
  								uint16_t *out_mV)
{
	uint16_t raw;
  	uint32_t shunt_voltage_mV;

  	/*
  	 * Проверяем логический канал и адрес результата
  	 * до запуска новой ADC-последовательности.
  	 */
  	if (!PeltierAdc_IsValid(channel) ||
  			(out_mV == NULL))
  	{
  		return false;
  	}

  	/*
  	 * Получаем полный DMA-кадр.
  	 *
  	 * raw[0] — напряжение шунта R15 канала 0;
  	 * raw[1] — напряжение шунта R16 канала 1.
  	 */
  	if (!PeltierAdc_ReadRaw(channel, &raw))
  	{
  		return false;
  	}

  	/*
  	 * Пересчитываем 12-битный результат ADC
  	 * в напряжение на входе PA0/PA1.
  	 *
  	 * Это напряжение шунта, а не напряжение на самом Пельтье.
  	 */
  	shunt_voltage_mV =
  			((uint32_t)raw * PELTIER_ADC_VDDA_MV
  					+ (PELTIER_ADC_MAX_RAW / 2U))
  			/ PELTIER_ADC_MAX_RAW;

  	/* Передаём проверенное значение вызывающему коду. */
  	*out_mV = (uint16_t)shunt_voltage_mV;

  	return true;
}

