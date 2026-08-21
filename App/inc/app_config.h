/*
 * app_config.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#ifndef INC_APP_CONFIG_H_
#define INC_APP_CONFIG_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* Firmware version reported by service command F001. */
#define FW_REV_MAJOR 0x01U
#define FW_REV_MINOR 0x00U
#define FW_REV_PATCH 0x00U

/* Local CAN configuration of Heater/Cooler executor. */
#define CAN_NODE_ID 0x80U
#define CAN_DATA_MAX_LEN 8U

/* RTOS queue lengths used by the executor tasks. */
#define CAN_RX_QUEUE_LEN 16U
#define CAN_TX_QUEUE_LEN 16U
#define DISPATCHER_QUEUE_LEN 8U
#define HEATER_COOLER_QUEUE_LEN 8U

/* Notification flags used by the CAN handler task. */
#define FLAG_CAN_RX 0x01U
#define FLAG_CAN_TX 0x02U

/*
 * Executor-local channel map.
 * The channel byte in HEATCTRL commands is zero-based: 0..3.
 * Conductor converts thermo zone number to channel with zone - 1.
 */
#define HEATER_COOLER_CHANNEL_SAMPLE_DISK 0U
#define HEATER_COOLER_CHANNEL_REAGENT_1 1U
#define HEATER_COOLER_CHANNEL_REAGENT_2 2U
#define HEATER_COOLER_CHANNEL_SCANNER_GLASS 3U

#define HEATER_COOLER_INVALID_CHANNEL 0xFFU

/*
 * Raw CAN frames are kept in the application layer so that the transport
 * task can pass a complete HAL frame to the dispatcher without reinterpretation.
 */
typedef struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
} CanRxFrame_t;

typedef struct {
    CAN_TxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
} CanTxFrame_t;

/*
 * Parsed command from CAN handler to dispatcher.
 * data contains payload bytes 3..7.
 */
typedef struct {
    uint16_t cmd_code;
    uint8_t device_id;
    uint8_t data[5];
    uint8_t data_len;
} ParsedCanCommand_t;

/*
 * Domain command from dispatcher to Heater/Cooler controller.
 * channel is zero-based: 0..3.
 */
typedef struct {
    uint8_t channel;
    uint16_t cmd_code;
    uint8_t data[5];
    uint8_t data_len;
} HeaterCoolerCommand_t;

#endif /* INC_APP_CONFIG_H_ */
