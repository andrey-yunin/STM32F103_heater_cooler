/*
 * app_flash.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#ifndef INC_APP_FLASH_H_
#define INC_APP_FLASH_H_

#include "app_config.h"

#include <stdbool.h>
#include <stdint.h>

#define APP_CONFIG_FLASH_ADDR 0x0800FC00UL
#define APP_CONFIG_MAGIC 0x55AAEEFFUL
#define APP_CONFIG_DEFAULT_NODE_ID CAN_NODE_ID

typedef struct {
	uint32_t magic;
	uint8_t performer_id;
	uint8_t reserved_bytes[5];
	uint16_t checksum;
} AppConfig_t;

void AppConfig_GetMCU_UID(uint8_t *out_uid);
void AppConfig_Init(void);
uint32_t AppConfig_GetPerformerID(void);
void AppConfig_SetPerformerID(uint32_t id);
bool AppConfig_Commit(void);
/* true — очистка завершилась успешно; false — ошибка. */
bool AppConfig_FactoryReset(void);

#endif /* INC_APP_FLASH_H_ */
