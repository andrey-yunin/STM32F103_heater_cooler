/*
 * peltier_adc.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 *
 * Контракт чтения двух каналов обратной связи Пельтье.
 *
 * ADC1 работает в режиме:
 *   rank 1 — ADC_CHANNEL_0 / PA0;
 *   rank 2 — ADC_CHANNEL_1 / PA1.
 *
 * Результаты полной последовательности передаются DMA1 Channel1
 * в память в том же порядке: raw[0], затем raw[1].
 *
 */

#ifndef INC_DRIVERS_PELTIER_ADC_H_
#define INC_DRIVERS_PELTIER_ADC_H_

#include <stdbool.h>
#include <stdint.h>

/* Количество каналов в регулярной последовательности ADC. */
#define PELTIER_ADC_CHANNEL_COUNT 2U

/* Максимальное значение 12-битного ADC. */
#define PELTIER_ADC_MAX_RAW 4095U

/*
 * Ограничение ожидания полного DMA-кадра.
 * Тайм-аут относится ко всей последовательности из двух rank.
 */
#define PELTIER_ADC_DMA_TIMEOUT_MS 10U

/*
 * Количество повторных попыток после ошибки.
 * При значении 2 выполняется максимум три запуска:
 * первая попытка плюс две повторные.
 */
#define PELTIER_ADC_DMA_RETRY_COUNT 2U

/*
 * Полный кадр измерений ADC.
 *
 * Индекс raw[] соответствует rank ADC:
 *   raw[0] — PA0 / ADC_CHANNEL_0;
 *   raw[1] — PA1 / ADC_CHANNEL_1.
 */
typedef struct
{
	uint16_t raw[PELTIER_ADC_CHANNEL_COUNT];
} PeltierAdcFrame_t;

/*
 * Читает полный кадр из двух ADC-каналов через DMA.
 *
 * Функция возвращает true только после получения полного кадра.
 * При тайм-ауте, ошибке DMA или некорректном значении
 * возвращается false, а кадр не считается достоверным.
 *
 * При false доменный уровень обязан сохранить безопасное состояние
 * силовых выходов и зафиксировать неисправность.
 */
bool PeltierAdc_ReadFrame(PeltierAdcFrame_t *out_frame);

/*
 * Совместимый интерфейс чтения одного логического канала.
 *
 * Внутри всё равно считывается полный DMA-кадр из двух каналов.
 * Для одновременного получения обоих значений предпочтительно
 * использовать PeltierAdc_ReadFrame().
 */
bool PeltierAdc_ReadRaw(uint8_t channel,
						uint16_t *out_raw);

/*
 * Читает выбранный канал ADC и возвращает в милливолтах
 * напряжение на соответствующем токовом шунте.
 *
 * Значение не является напряжением непосредственно на элементе Пельтье:
 * PA0 измеряет напряжение на R15, PA1 — напряжение на R16.
 * Функция возвращает false при ошибке или неполном DMA-кадре.
 */
bool PeltierAdc_ReadFeedback_mV(uint8_t channel,
 								uint16_t *out_mV);

/*
 * Опорное напряжение питания ADC, используемое
 * для пересчёта кода ADC в милливолты.
 *
 * Сигналы PA0/PA1 формируются измерительными цепями
 * токовых шунтов R15/R16 и фильтруются перед входами ADC.
 */
#define PELTIER_ADC_VDDA_MV 3300U


#endif /* INC_DRIVERS_PELTIER_ADC_H_ */
