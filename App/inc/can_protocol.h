/*
 * can_protocol.h
 *
 *  Created on: Aug 19, 2026
 *      Author: andrey
 */

#ifndef INC_CAN_PROTOCOL_H_
#define INC_CAN_PROTOCOL_H_

#include <stdbool.h>
#include <stdint.h>

/* DDS-240 CAN transport profile. */
#define CAN_FRAME_DLC                 8U
#define CAN_DATA_PAYLOAD_MAX          6U

/* Priority field: Extended CAN ID bits 28..26. */
#define CAN_PRIORITY_HIGH             0U
#define CAN_PRIORITY_NORMAL           1U

/* Message type field: Extended CAN ID bits 25..24. */
#define CAN_MSG_TYPE_COMMAND          0U
#define CAN_MSG_TYPE_ACK              1U
#define CAN_MSG_TYPE_NACK             2U
#define CAN_MSG_TYPE_DATA_DONE_LOG    3U

/* DATA/DONE/LOG subtype in payload byte 0. */
#define CAN_SUB_TYPE_DONE             0x01U
#define CAN_SUB_TYPE_DATA             0x02U
#define CAN_SUB_TYPE_LOG              0x03U

/* DATA sequence information in payload byte 1. */
#define CAN_DATA_SEQ_EOT_MASK         0x80U
#define CAN_DATA_SEQ_INDEX_MASK       0x7FU

/* DDS-240 node addresses. */
#define CAN_ADDR_BROADCAST            0x00U
#define CAN_ADDR_CONDUCTOR            0x10U
#define CAN_ADDR_HEATER_COOLER_BOARD  0x80U

#define CAN_DYNAMIC_NODE_ID_MIN       0x02U
#define CAN_DYNAMIC_NODE_ID_MAX       0x7FU


/* Device type identifiers returned by service command F001. */

#define CAN_DEVICE_TYPE_HEATER_COOLER 0x07U

/* Common service commands. */
#define CAN_CMD_SRV_GET_DEVICE_INFO   0xF001U
#define CAN_CMD_SRV_REBOOT            0xF002U
#define CAN_CMD_SRV_FLASH_COMMIT      0xF003U
#define CAN_CMD_SRV_GET_UID           0xF004U
#define CAN_CMD_SRV_SET_NODE_ID       0xF005U
#define CAN_CMD_SRV_FACTORY_RESET     0xF006U
#define CAN_CMD_SRV_GET_STATUS        0xF007U

/* Magic keys for protected service commands. */
#define SRV_MAGIC_REBOOT              0x55AAU
#define SRV_MAGIC_FACTORY_RESET       0xDEADU

/*
 * Heater/Cooler low-level commands.
 * Thermal regulation and zone-to-channel policy remain on the Conductor.
 */
#define CAN_CMD_HEATCTRL_SET_POWER             0x0801U
#define CAN_CMD_HEATCTRL_ENABLE                0x0802U
#define CAN_CMD_HEATCTRL_DISABLE               0x0803U
#define CAN_CMD_HEATCTRL_CHANNEL_SELF_TEST     0x0804U
#define CAN_CMD_HEATCTRL_GET_STATUS            0x0810U
#define CAN_CMD_HEATCTRL_SAFE_OFF              0x08FFU

/*
 * The active Conductor implementation sends zero-based channel ids
 * in payload byte 2: 0..3.
 */
#define CAN_HEATER_COOLER_CHANNELS_DEFAULT     4U

/* Common executor NACK namespace. */
#define CAN_ERR_NONE                    0x0000U
#define CAN_ERR_UNKNOWN_CMD             0x0001U
#define CAN_ERR_INVALID_DEVICE_ID       0x0002U
#define CAN_ERR_DEVICE_BUSY             0x0003U
#define CAN_ERR_INVALID_KEY             0x0004U
#define CAN_ERR_FLASH_WRITE             0x0005U
#define CAN_ERR_INVALID_PARAM           0x0006U

/* Heater/Cooler domain NACK namespace: 0xE800..0xE8FF. */
#define CAN_NACK_DOMAIN_HEATER_COOLER_BASE    0xE800U
#define CAN_ERR_HC_INVALID_CHANNEL            0xE800U
#define CAN_ERR_HC_FEEDBACK_OUT_OF_RANGE      0xE801U
#define CAN_ERR_HC_OVERTEMP                   0xE802U

/* Common GET_STATUS metric identifiers. */
#define CAN_STATUS_RX_TOTAL             0x0001U
#define CAN_STATUS_TX_TOTAL             0x0002U
#define CAN_STATUS_RX_QUEUE_OVERFLOW    0x0003U
#define CAN_STATUS_TX_QUEUE_OVERFLOW    0x0004U
#define CAN_STATUS_DISPATCHER_OVERFLOW  0x0005U
#define CAN_STATUS_DROP_NOT_EXT         0x0006U
#define CAN_STATUS_DROP_WRONG_DST       0x0007U
#define CAN_STATUS_DROP_WRONG_TYPE      0x0008U
#define CAN_STATUS_DROP_WRONG_DLC       0x0009U
#define CAN_STATUS_TX_MAILBOX_TIMEOUT   0x000AU
#define CAN_STATUS_TX_HAL_ERROR         0x000BU
#define CAN_STATUS_ERROR_CALLBACK       0x000CU
#define CAN_STATUS_ERROR_WARNING        0x000DU
#define CAN_STATUS_ERROR_PASSIVE        0x000EU
#define CAN_STATUS_BUS_OFF              0x000FU
#define CAN_STATUS_LAST_HAL_ERROR       0x0010U
#define CAN_STATUS_LAST_ESR             0x0011U
#define CAN_STATUS_APP_QUEUE_OVERFLOW   0x0012U

/*
 * Build and extract the 29-bit Extended CAN identifier.
 * The lowest CAN ID byte remains reserved and is always zero.
 */
#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
			((uint32_t)((((uint32_t)(priority) & 0x07UL) << 26) | \
			(((uint32_t)(msg_type) & 0x03UL) << 24) | \
            (((uint32_t)(dst_addr)  & 0xFFUL) << 16) | \
            (((uint32_t)(src_addr)  & 0xFFUL) << 8)))

#define CAN_GET_PRIORITY(id) \
			((uint8_t)(((id) >> 26) & 0x07U))

#define CAN_GET_MSG_TYPE(id) \
			((uint8_t)(((id) >> 24) & 0x03U))

#define CAN_GET_DST_ADDR(id) \
			((uint8_t)(((id) >> 16) & 0xFFU))

#define CAN_GET_SRC_ADDR(id) \
			((uint8_t)(((id) >> 8) & 0xFFU))

#endif /* INC_CAN_PROTOCOL_H_ */
