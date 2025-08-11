/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 BitConcepts, LLC
 *
 * CANopenNode — Zephyr configuration mapping
 * This header maps Zephyr Kconfig symbols (CONFIG_CANOPENNODE_*) to
 * the CO_CONFIG_* macros used by the portable CANopenNode stack.
 *
 * The CO_CONFIG_* macros are documented in CO_config.h of CANopenNode.
 * This mapper ensures that undefined CONFIG_ symbols are treated as 0,
 * so the macros are safe for use outside Zephyr.
 */

#ifndef CO_CONFIG_ZEPHYR_H
#define CO_CONFIG_ZEPHYR_H

/* Helper: ensure undefined CONFIG_* are treated as 0 */
#define ZCFG(sym) (defined(CONFIG_##sym) && (CONFIG_##sym) ? 1 : 0)

/* ---------------- Global configuration flags ---------------- */
#define CO_CONFIG_GLOBAL(CALLBACK_PRE, CALLBACK_PRE_RTR, TIMERNEXT, OD_DYNAMIC)                    \
	(((CALLBACK_PRE) ? CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE : 0) |                               \
	 ((CALLBACK_PRE_RTR) ? CO_CONFIG_GLOBAL_RT_FLAG_CALLBACK_PRE : 0) |                        \
	 ((TIMERNEXT) ? CO_CONFIG_GLOBAL_FLAG_TIMERNEXT : 0) |                                     \
	 ((OD_DYNAMIC) ? CO_CONFIG_GLOBAL_FLAG_OD_DYNAMIC : 0))

#define CO_CONFIG_GLOBAL_FLAGS                                                                     \
	CO_CONFIG_GLOBAL(ZCFG(CANOPENNODE_GLOBAL_FLAG_CALLBACK_PRE),                               \
			 ZCFG(CANOPENNODE_GLOBAL_RT_FLAG_CALLBACK_PRE),                            \
			 ZCFG(CANOPENNODE_GLOBAL_FLAG_TIMERNEXT),                                  \
			 ZCFG(CANOPENNODE_GLOBAL_FLAG_OD_DYNAMIC))

/* ---------------- NMT and Heartbeat ---------------- */
#define CO_CONFIG_NMT_FLAGS                                                                        \
	((ZCFG(CANOPENNODE_NMT_CALLBACK_CHANGE) ? CO_CONFIG_NMT_CALLBACK_CHANGE : 0) |             \
	 (ZCFG(CANOPENNODE_NMT_MASTER) ? CO_CONFIG_NMT_MASTER : 0))

#define CO_CONFIG_HB_CONS_FLAGS                                                                    \
	((ZCFG(CANOPENNODE_HB_CONS_ENABLE) ? CO_CONFIG_HB_CONS_ENABLE : 0) |                       \
	 (ZCFG(CANOPENNODE_HB_CONS_CALLBACK_CHANGE) ? CO_CONFIG_HB_CONS_CALLBACK_CHANGE : 0) |     \
	 (ZCFG(CANOPENNODE_HB_CONS_CALLBACK_MULTI) ? CO_CONFIG_HB_CONS_CALLBACK_MULTI : 0) |       \
	 (ZCFG(CANOPENNODE_HB_CONS_QUERY_FUNCT) ? CO_CONFIG_HB_CONS_QUERY_FUNCT : 0))

/* ---------------- Emergency ---------------- */
#define CO_CONFIG_EM_FLAGS                                                                         \
	((ZCFG(CANOPENNODE_EM_PRODUCER) ? CO_CONFIG_EM_PRODUCER : 0) |                             \
	 (ZCFG(CANOPENNODE_EM_PROD_CONFIGURABLE) ? CO_CONFIG_EM_PROD_CONFIGURABLE : 0) |           \
	 (ZCFG(CANOPENNODE_EM_PROD_INHIBIT) ? CO_CONFIG_EM_PROD_INHIBIT : 0) |                     \
	 (ZCFG(CANOPENNODE_EM_HISTORY) ? CO_CONFIG_EM_HISTORY : 0) |                               \
	 (ZCFG(CANOPENNODE_EM_STATUS_BITS) ? CO_CONFIG_EM_STATUS_BITS : 0) |                       \
	 (ZCFG(CANOPENNODE_EM_CONSUMER) ? CO_CONFIG_EM_CONSUMER : 0))

#define CO_CONFIG_ERR_CONDITION_FLAGS                                                              \
	((ZCFG(CANOPENNODE_ERR_CONDITION_GENERIC) ? CO_CONFIG_ERR_CONDITION_GENERIC : 0) |         \
	 (ZCFG(CANOPENNODE_ERR_CONDITION_CURRENT) ? CO_CONFIG_ERR_CONDITION_CURRENT : 0) |         \
	 (ZCFG(CANOPENNODE_ERR_CONDITION_VOLTAGE) ? CO_CONFIG_ERR_CONDITION_VOLTAGE : 0) |         \
	 (ZCFG(CANOPENNODE_ERR_CONDITION_TEMPERATURE) ? CO_CONFIG_ERR_CONDITION_TEMPERATURE : 0) | \
	 (ZCFG(CANOPENNODE_ERR_CONDITION_COMMUNICATION) ? CO_CONFIG_ERR_CONDITION_COMMUNICATION    \
							: 0) |                                     \
	 (ZCFG(CANOPENNODE_ERR_CONDITION_DEV_PROFILE) ? CO_CONFIG_ERR_CONDITION_DEV_PROFILE : 0) | \
	 (ZCFG(CANOPENNODE_ERR_CONDITION_MANUFACTURER) ? CO_CONFIG_ERR_CONDITION_MANUFACTURER      \
						       : 0))

/* ---------------- SDO ---------------- */
#define CO_CONFIG_SDO_SRV_FLAGS                                                                    \
	((ZCFG(CANOPENNODE_SDO_SRV_SEGMENTED) ? CO_CONFIG_SDO_SRV_SEGMENTED : 0) |                 \
	 (ZCFG(CANOPENNODE_SDO_SRV_BLOCK) ? CO_CONFIG_SDO_SRV_BLOCK : 0))

#define CO_CONFIG_SDO_CLI_FLAGS                                                                    \
	((ZCFG(CANOPENNODE_SDO_CLI_ENABLE) ? CO_CONFIG_SDO_CLI_ENABLE : 0) |                       \
	 (ZCFG(CANOPENNODE_SDO_CLI_SEGMENTED) ? CO_CONFIG_SDO_CLI_SEGMENTED : 0) |                 \
	 (ZCFG(CANOPENNODE_SDO_CLI_BLOCK) ? CO_CONFIG_SDO_CLI_BLOCK : 0) |                         \
	 (ZCFG(CANOPENNODE_SDO_CLI_LOCAL) ? CO_CONFIG_SDO_CLI_LOCAL : 0))

/* ---------------- TIME / SYNC / PDO ---------------- */
#define CO_CONFIG_SYNC_FLAGS                                                                       \
	((ZCFG(CANOPENNODE_SYNC_ENABLE) ? CO_CONFIG_SYNC_ENABLE : 0) |                             \
	 (ZCFG(CANOPENNODE_SYNC_PRODUCER) ? CO_CONFIG_SYNC_PRODUCER : 0))

#define CO_CONFIG_PDO_FLAGS                                                                        \
	((ZCFG(CANOPENNODE_RPDO_ENABLE) ? CO_CONFIG_RPDO_ENABLE : 0) |                             \
	 (ZCFG(CANOPENNODE_TPDO_ENABLE) ? CO_CONFIG_TPDO_ENABLE : 0) |                             \
	 (ZCFG(CANOPENNODE_RPDO_TIMERS_ENABLE) ? CO_CONFIG_RPDO_TIMERS_ENABLE : 0) |               \
	 (ZCFG(CANOPENNODE_TPDO_TIMERS_ENABLE) ? CO_CONFIG_TPDO_TIMERS_ENABLE : 0) |               \
	 (ZCFG(CANOPENNODE_PDO_SYNC_ENABLE) ? CO_CONFIG_PDO_SYNC_ENABLE : 0) |                     \
	 (ZCFG(CANOPENNODE_PDO_OD_IO_ACCESS) ? CO_CONFIG_PDO_OD_IO_ACCESS : 0))

/* ---------------- Storage / LEDs ---------------- */
#define CO_CONFIG_STORAGE_FLAGS (ZCFG(CANOPENNODE_STORAGE_ENABLE) ? CO_CONFIG_STORAGE_ENABLE : 0)

#define CO_CONFIG_LEDS_FLAGS (ZCFG(CANOPENNODE_LEDS_ENABLE) ? CO_CONFIG_LEDS_ENABLE : 0)

/* ---------------- SRDO / GFC ---------------- */
#define CO_CONFIG_GFC_FLAGS                                                                        \
	((ZCFG(CANOPENNODE_GFC_ENABLE) ? CO_CONFIG_GFC_ENABLE : 0) |                               \
	 (ZCFG(CANOPENNODE_GFC_CONSUMER) ? CO_CONFIG_GFC_CONSUMER : 0) |                           \
	 (ZCFG(CANOPENNODE_GFC_PRODUCER) ? CO_CONFIG_GFC_PRODUCER : 0))

#define CO_CONFIG_SRDO_FLAGS                                                                       \
	((ZCFG(CANOPENNODE_SRDO_ENABLE) ? CO_CONFIG_SRDO_ENABLE : 0) |                             \
	 (ZCFG(CANOPENNODE_SRDO_CHECK_TX) ? CO_CONFIG_SRDO_CHECK_TX : 0))

/* ---------------- LSS ---------------- */
#define CO_CONFIG_LSS_FLAGS                                                                        \
	((ZCFG(CANOPENNODE_LSS_SLAVE) ? CO_CONFIG_LSS_SLAVE : 0) |                                 \
	 (ZCFG(CANOPENNODE_LSS_MASTER) ? CO_CONFIG_LSS_MASTER : 0))

/* ---------------- Gateway ---------------- */
#define CO_CONFIG_GTWA_FLAGS                                                                       \
	((ZCFG(CANOPENNODE_GTW_MULTI_NET) ? CO_CONFIG_GTWA_MULTI_NET : 0) |                        \
	 (ZCFG(CANOPENNODE_GTW_ASCII) ? CO_CONFIG_GTWA_ASCII : 0) |                                \
	 (ZCFG(CANOPENNODE_GTW_ASCII_SDO) ? CO_CONFIG_GTWA_ASCII_SDO : 0) |                        \
	 (ZCFG(CANOPENNODE_GTW_ASCII_NMT) ? CO_CONFIG_GTWA_ASCII_NMT : 0) |                        \
	 (ZCFG(CANOPENNODE_GTW_ASCII_LSS) ? CO_CONFIG_GTWA_ASCII_LSS : 0) |                        \
	 (ZCFG(CANOPENNODE_GTW_ASCII_LOG) ? CO_CONFIG_GTWA_ASCII_LOG : 0) |                        \
	 (ZCFG(CANOPENNODE_GTW_ASCII_ERROR_DESC) ? CO_CONFIG_GTWA_ASCII_ERROR_DESC : 0) |          \
	 (ZCFG(CANOPENNODE_GTW_ASCII_PRINT_HELP) ? CO_CONFIG_GTWA_ASCII_PRINT_HELP : 0) |          \
	 (ZCFG(CANOPENNODE_GTW_ASCII_PRINT_LEDS) ? CO_CONFIG_GTWA_ASCII_PRINT_LEDS : 0))

/* ---------------- FIFO / CRC / Trace / Debug ---------------- */
#define CO_CONFIG_FIFO_FLAGS                                                                       \
	((ZCFG(CANOPENNODE_FIFO_ENABLE) ? CO_CONFIG_FIFO_ENABLE : 0) |                             \
	 (ZCFG(CANOPENNODE_FIFO_ALT_READ) ? CO_CONFIG_FIFO_ALT_READ : 0) |                         \
	 (ZCFG(CANOPENNODE_FIFO_CRC16_CCITT) ? CO_CONFIG_FIFO_CRC16_CCITT : 0) |                   \
	 (ZCFG(CANOPENNODE_FIFO_ASCII_COMMANDS) ? CO_CONFIG_FIFO_ASCII_COMMANDS : 0) |             \
	 (ZCFG(CANOPENNODE_FIFO_ASCII_DATATYPES) ? CO_CONFIG_FIFO_ASCII_DATATYPES : 0))

#define CO_CONFIG_CRC16_FLAGS                                                                      \
	((ZCFG(CANOPENNODE_CRC16_ENABLE) ? CO_CONFIG_CRC16_ENABLE : 0) |                           \
	 (ZCFG(CANOPENNODE_CRC16_EXTERNAL) ? CO_CONFIG_CRC16_EXTERNAL : 0))

#define CO_CONFIG_TRACE_FLAGS                                                                      \
	((ZCFG(CANOPENNODE_TRACE_ENABLE) ? CO_CONFIG_TRACE_ENABLE : 0) |                           \
	 (ZCFG(CANOPENNODE_TRACE_OWN_INTTYPES) ? CO_CONFIG_TRACE_OWN_INTTYPES : 0))

#define CO_CONFIG_DEBUG_FLAGS                                                                      \
	((ZCFG(CANOPENNODE_DEBUG_COMMON) ? CO_CONFIG_DEBUG_COMMON : 0) |                           \
	 (ZCFG(CANOPENNODE_DEBUG_SDO_CLIENT) ? CO_CONFIG_DEBUG_SDO_CLIENT : 0) |                   \
	 (ZCFG(CANOPENNODE_DEBUG_SDO_SERVER) ? CO_CONFIG_DEBUG_SDO_SERVER : 0))

#endif /* CO_CONFIG_ZEPHYR_H */
