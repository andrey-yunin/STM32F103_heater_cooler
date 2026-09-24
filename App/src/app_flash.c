/*
 * app_flash.c
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#include "app_flash.h"

#include "cmsis_os.h"
#include "main.h"

#include <stddef.h>
#include <string.h>

/* --- Runtime-конфигурация узла в RAM --- */

static AppConfig_t g_app_config;
static osMutexId_t configMutex;

/* --- Защита общей конфигурации --- */

static const osMutexAttr_t configMutex_attr = { "configMutex", osMutexRecursive
		| osMutexPrioInherit,
NULL, 0U };

/* --- Расчет CRC16 для данных до поля checksum --- */

static uint16_t CalculateChecksum(const AppConfig_t *config) {
	uint16_t crc = 0xFFFFU;
	const uint8_t *bytes = (const uint8_t*) config;
	const size_t length = offsetof(AppConfig_t, checksum);

	for (size_t index = 0U; index < length; index++) {
		crc ^= bytes[index];

		for (uint8_t bit = 0U; bit < 8U; bit++) {
			if ((crc & 0x0001U) != 0U) {
				crc = (uint16_t) ((crc >> 1U) ^ 0xA001U);
			} else {
				crc >>= 1U;
			}
		}
	}
	return crc;
}

/* --- Формирование заводской конфигурации в RAM --- */

static void LoadDefaults(void) {
	memset(&g_app_config, 0, sizeof(g_app_config));

	g_app_config.magic = APP_CONFIG_MAGIC;
	g_app_config.performer_id = APP_CONFIG_DEFAULT_NODE_ID;
	g_app_config.checksum = CalculateChecksum(&g_app_config);
}

/* --- Чтение уникального идентификатора STM32 --- */

void AppConfig_GetMCU_UID(uint8_t *out_uid) {
	const uint8_t *uid_base = (const uint8_t*) 0x1FFFF7E8UL;
	if (out_uid == NULL) {
		return;
	}
	memcpy(out_uid, uid_base, 12U);
}

/* --- Загрузка конфигурации из Flash и создание Mutex --- */

void AppConfig_Init(void) {
	AppConfig_t *flash_config;

	configMutex = osMutexNew(&configMutex_attr);

	if (configMutex == NULL) {
		Error_Handler();
		return;
	}

	flash_config = (AppConfig_t*) APP_CONFIG_FLASH_ADDR;

	if ((flash_config->magic == APP_CONFIG_MAGIC)
			&& (flash_config->checksum == CalculateChecksum(flash_config))) {
		memcpy(&g_app_config, flash_config, sizeof(g_app_config));
	} else {
		LoadDefaults();
	}
}

/* --- Потокобезопасное чтение текущего NodeID --- */

uint32_t AppConfig_GetPerformerID(void) {
	uint32_t performer_id = APP_CONFIG_DEFAULT_NODE_ID;

	if (configMutex == NULL) {
		return performer_id;
	}

	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		performer_id = g_app_config.performer_id;
		(void) osMutexRelease(configMutex);
	}
	return performer_id;
}

/* --- Потокобезопасное изменение NodeID в RAM --- */

void AppConfig_SetPerformerID(uint32_t id) {
	if (configMutex == NULL) {
		return;
	}

	if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
		g_app_config.performer_id = (uint8_t) id;
		(void) osMutexRelease(configMutex);
	}
}

/* --- Стирание конфигурационной страницы и возврат к defaults --- */
bool AppConfig_FactoryReset(void) {
	FLASH_EraseInitTypeDef erase_config = { 0 };
	uint32_t page_error = 0U;
	HAL_StatusTypeDef status;

	if (configMutex == NULL) {
		return false;
	}

	if (osMutexAcquire(configMutex, osWaitForever) != osOK) {
		return false;
	}

	erase_config.TypeErase = FLASH_TYPEERASE_PAGES;
	erase_config.PageAddress = APP_CONFIG_FLASH_ADDR;
	erase_config.NbPages = 1U;

	/* Стирание допустимо только после успешной разблокировки. */
	status = HAL_FLASH_Unlock();

	if (status == HAL_OK) {
		status = HAL_FLASHEx_Erase(&erase_config, &page_error);
	}

	/* Закрываем доступ к записи независимо от результата стирания. */
	if (HAL_FLASH_Lock() != HAL_OK) {
		status = HAL_ERROR;
	}

	/* После получения mutex освобождаем его при любом результате. */
	if (osMutexRelease(configMutex) != osOK) {
		Error_Handler();
		return false;
	}

	return status == HAL_OK;
}

/* --- Атомарная запись runtime-конфигурации во Flash --- */

bool AppConfig_Commit(void) {
	FLASH_EraseInitTypeDef erase_config;
	HAL_StatusTypeDef status = HAL_ERROR;
	uint32_t page_error = 0U;
	uint32_t flash_address = APP_CONFIG_FLASH_ADDR;
	const uint32_t *config_words;

	if (configMutex == NULL) {
		return false;
	}

	if (osMutexAcquire(configMutex, osWaitForever) != osOK) {
		return false;
	}

	g_app_config.checksum = CalculateChecksum(&g_app_config);

	erase_config.TypeErase = FLASH_TYPEERASE_PAGES;
	erase_config.PageAddress = APP_CONFIG_FLASH_ADDR;
	erase_config.NbPages = 1U;

	HAL_FLASH_Unlock();

	status = HAL_FLASHEx_Erase(&erase_config, &page_error);

	if (status == HAL_OK) {
		config_words = (const uint32_t*) &g_app_config;

		for (uint32_t offset = 0U; offset < sizeof(g_app_config); offset +=
				sizeof(uint32_t)) {
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_address,
					*config_words);

			if (status != HAL_OK) {
				break;
			}

			flash_address += sizeof(uint32_t);
			config_words++;
		}
	}

	HAL_FLASH_Lock();

	(void) osMutexRelease(configMutex);

	return (status == HAL_OK);
}
