/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-to-CANopenNode configuration bridge.
 *
 * Aggregates Zephyr Kconfig (CONFIG_*) options into CANopenNode's
 * CO_CONFIG_* compile-time flags and related values, so the stack can
 * be configured from prj.conf and devicetree.
 *
 * @file        CO_zephyr_config.h
 * @author      BitConcepts, LLC <https://github.com/BitConcepts>
 * @copyright   2025 BitConcepts, LLC
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_CONFIG_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_CONFIG_H

#include <zephyr/autoconf.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup co_zephyr_config Zephyr ↔ CANopenNode configuration bridge
 * @brief Map Zephyr Kconfig to CANopenNode @c CO_CONFIG_* flags and constants.
 *
 * This header is a thin preprocessor bridge. It reads Zephyr @c CONFIG_* symbols
 * and produces the @c CO_CONFIG_* bitmasks and values that CANopenNode expects at
 * compile time. Include this header before including CANopenNode modules (or in a
 * central integration translation unit) so that the configuration takes effect.
 *
 * ### Helpers
 * - ::ZBIT(flag, cfgsym) — Expands to @p flag when @p cfgsym is enabled, otherwise 0.
 * - ::ZVAL(cfgsym) — Expands to the value of @p cfgsym (numeric or 0 if unset).
 *
 * ### Notes
 * - Requires Zephyr’s @c <zephyr/autoconf.h>.
 * - All @c CONFIG_CANOPENNODE_* options come from your project’s @c prj.conf/Kconfig.
 *
 * @{
 */

/** @name Helper macros
 *  @brief Small utilities for conditional bit composition.
 *  @{
 */
/** @brief Conditionally OR a flag when a Kconfig symbol is enabled. */
#define ZBIT(flag, cfgsym) (IS_ENABLED(cfgsym) ? (flag) : 0)
/** @brief Yield the value of a Kconfig symbol (or 0 if not defined). */
#define ZVAL(cfgsym)       (cfgsym)
/** @} */

/** @def CO_USE_GLOBALS
 *  @brief Selects storage model for CANopenNode objects.
 *
 *  If not provided by the build, this header auto-derives the value from
 *  Zephyr’s heap setting:
 *  - `1` — Use global/static objects; no dynamic allocation.
 *  - `0` — Use dynamic allocation; requires a non-zero
 *    `CONFIG_HEAP_MEM_POOL_SIZE`.
 *
 *  You may override by defining `CO_USE_GLOBALS` before including this header
 *  or via the build system (e.g., `-DCO_USE_GLOBALS=0`).
 *
 *  @see CONFIG_HEAP_MEM_POOL_SIZE
 */
#ifndef CO_USE_GLOBALS
/* Treat “heap present” (non-zero) as “use dynamic allocation”. */
#if defined(CONFIG_HEAP_MEM_POOL_SIZE) && (CONFIG_HEAP_MEM_POOL_SIZE > 0)
#define CO_USE_GLOBALS 0
#else
#define CO_USE_GLOBALS 1
#endif
#endif

/** @brief Build-time sanity check for dynamic mode.
 *
 *  Triggers a compile-time error if `CO_USE_GLOBALS==0` (dynamic allocation)
 *  but Zephyr’s heap is not enabled or has size 0.
 */
#if (CO_USE_GLOBALS == 0) &&                                                                       \
	!(defined(CONFIG_HEAP_MEM_POOL_SIZE) && (CONFIG_HEAP_MEM_POOL_SIZE > 0))
#error "CO_USE_GLOBALS=0 requires a nonzero CONFIG_HEAP_MEM_POOL_SIZE"
#endif

/** @name NMT / Heartbeat Producer
 *  @brief Configure CANopen NMT producer behavior and control bits.
 *  @{
 */
/** @brief Bitmask for CANopenNode NMT configuration derived from Kconfig. */
#define CO_CONFIG_NMT                                                                              \
	(ZBIT(CO_CONFIG_NMT_CALLBACK_CHANGE, CONFIG_CANOPENNODE_NMT_CALLBACK_CHANGE) |             \
	 ZBIT(CO_CONFIG_NMT_MASTER, CONFIG_CANOPENNODE_NMT_MASTER) |                               \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_NMT_CALLBACK) |                      \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_NMT_TIMERNEXT))

/** @brief Optional initial heartbeat delay in milliseconds. */
#define CO_NMT_FIRST_HB_TIME_MS CONFIG_CANOPENNODE_NMT_FIRST_HB_TIME_MS

/**
 * @brief Composite NMT startup/control mask you can pass to CANopenNode init.
 *
 * Combines the policy booleans above into the bitmask expected by CANopenNode.
 */
#define CO_CONFIG_NMT_CONTROL                                                                      \
	(ZBIT(CO_NMT_STARTUP_TO_OPERATIONAL, CONFIG_CANOPENNODE_NMT_STARTUP_TO_OPERATIONAL) |      \
	 ZBIT(CO_NMT_ERR_ON_BUSOFF_HB, CONFIG_CANOPENNODE_NMT_ERR_ON_BUSOFF_HB) |                  \
	 ZBIT(CO_NMT_ERR_ON_ERR_REG, CONFIG_CANOPENNODE_NMT_ERR_ON_ERR_REG) |                      \
	 ZBIT(CO_NMT_ERR_TO_STOPPED, CONFIG_CANOPENNODE_NMT_ERR_TO_STOPPED) |                      \
	 ZBIT(CO_NMT_ERR_FREE_TO_OPERATIONAL, CONFIG_CANOPENNODE_NMT_ERR_FREE_TO_OPERATIONAL))
/** @} */

/** @name Heartbeat Consumer
 *  @brief Configure HB consumer behavior, callbacks, and OD options.
 *  @{
 */
#define CO_CONFIG_HB_CONS                                                                          \
	(ZBIT(CO_CONFIG_HB_CONS_ENABLE, CONFIG_CANOPENNODE_HB_CONS_ENABLE) |                       \
	 ZBIT(CO_CONFIG_HB_CONS_CALLBACK_CHANGE, CONFIG_CANOPENNODE_HB_CONS_CALLBACK_CHANGE) |     \
	 ZBIT(CO_CONFIG_HB_CONS_CALLBACK_MULTI, CONFIG_CANOPENNODE_HB_CONS_CALLBACK_MULTI) |       \
	 ZBIT(CO_CONFIG_HB_CONS_QUERY_FUNCT, CONFIG_CANOPENNODE_HB_CONS_QUERY_FUNCT) |             \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_HB_CONS_CALLBACK) |                  \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_HB_CONS_TIMERNEXT) |                    \
	 ZBIT(CO_CONFIG_FLAG_OD_DYNAMIC, CONFIG_CANOPENNODE_HB_CONS_OD_DYNAMIC))
/** @} */

/** @name Node Guarding
 *  @brief Enable/Configure node guarding master/slave roles and timing.
 *  @{
 */
#define CO_CONFIG_NODE_GUARDING                                                                    \
	(ZBIT(CO_CONFIG_NODE_GUARDING_SLAVE_ENABLE,                                                \
	      CONFIG_CANOPENNODE_NODE_GUARDING_SLAVE_ENABLE) |                                     \
	 ZBIT(CO_CONFIG_NODE_GUARDING_MASTER_ENABLE,                                               \
	      CONFIG_CANOPENNODE_NODE_GUARDING_MASTER_ENABLE) |                                    \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_NODE_GUARDING_TIMERNEXT))

/** @brief Number of guarded nodes when acting as a master (if provided). */
#ifdef CONFIG_CANOPENNODE_NODE_GUARDING_MASTER_COUNT
#define CO_CONFIG_NODE_GUARDING_MASTER_COUNT CONFIG_CANOPENNODE_NODE_GUARDING_MASTER_COUNT
#endif
/** @} */

/** @name Emergency (EM)
 *  @brief Configure emergency producer/consumer and status bits.
 *  @{
 */
#define CO_CONFIG_EM                                                                               \
	(ZBIT(CO_CONFIG_EM_PRODUCER, CONFIG_CANOPENNODE_EM_PRODUCER) |                             \
	 ZBIT(CO_CONFIG_EM_PROD_CONFIGURABLE, CONFIG_CANOPENNODE_EM_PROD_CONFIGURABLE) |           \
	 ZBIT(CO_CONFIG_EM_PROD_INHIBIT, CONFIG_CANOPENNODE_EM_PROD_INHIBIT) |                     \
	 ZBIT(CO_CONFIG_EM_HISTORY, CONFIG_CANOPENNODE_EM_HISTORY) |                               \
	 ZBIT(CO_CONFIG_EM_CONSUMER, CONFIG_CANOPENNODE_EM_CONSUMER) |                             \
	 ZBIT(CO_CONFIG_EM_STATUS_BITS, CONFIG_CANOPENNODE_EM_STATUS_BITS) |                       \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_EM_CALLBACK) |                       \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_EM_TIMERNEXT))

/** @brief Size of error status bits array. */
#define CO_CONFIG_EM_ERR_STATUS_BITS_COUNT CONFIG_CANOPENNODE_EM_ERR_STATUS_BITS_COUNT

/** @brief Optional default error-condition enables (application maps them as needed). */
#define CO_CONFIG_ERR_CONDITION_CURRENT     IS_ENABLED(CONFIG_CANOPENNODE_ERR_CONDITION_CURRENT)
#define CO_CONFIG_ERR_CONDITION_VOLTAGE     IS_ENABLED(CONFIG_CANOPENNODE_ERR_CONDITION_VOLTAGE)
#define CO_CONFIG_ERR_CONDITION_TEMPERATURE IS_ENABLED(CONFIG_CANOPENNODE_ERR_CONDITION_TEMPERATURE)
#define CO_CONFIG_ERR_CONDITION_DEV_PROFILE IS_ENABLED(CONFIG_CANOPENNODE_ERR_CONDITION_DEV_PROFILE)
/** @} */

/** @name SDO Server
 *  @brief Configure SDO server features, callbacks, and timeouts.
 *  @{
 */
#define CO_CONFIG_SDO_SRV                                                                          \
	(ZBIT(CO_CONFIG_SDO_SRV_SEGMENTED, CONFIG_CANOPENNODE_SDO_SERVER_SEGMENTED) |              \
	 ZBIT(CO_CONFIG_SDO_SRV_BLOCK, CONFIG_CANOPENNODE_SDO_SERVER_BLOCK) |                      \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_SDO_SERVER_CALLBACK) |               \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_SDO_SERVER_TIMERNEXT) |                 \
	 ZBIT(CO_CONFIG_FLAG_OD_DYNAMIC, CONFIG_CANOPENNODE_SDO_SERVER_OD_DYNAMIC))

/** @brief Server buffer size in bytes. */
#define CO_CONFIG_SDO_SRV_BUFFER_SIZE CONFIG_CANOPENNODE_SDO_SERVER_BUFFER_SIZE
/** @brief SDO server timeout in ms. */
#define CO_CONFIG_SDO_SRV_TIMEOUT_MS  CONFIG_CANOPENNODE_SDO_SERVER_TIMEOUT_MS
/** @} */

/** @name SDO Client
 *  @brief Configure SDO client features, callbacks, and timeouts.
 *  @{
 */
#define CO_CONFIG_SDO_CLI                                                                          \
	(ZBIT(CO_CONFIG_SDO_CLI_ENABLE, CONFIG_CANOPENNODE_SDO_CLIENT_ENABLE) |                    \
	 ZBIT(CO_CONFIG_SDO_CLI_SEGMENTED, CONFIG_CANOPENNODE_SDO_CLIENT_SEGMENTED) |              \
	 ZBIT(CO_CONFIG_SDO_CLI_BLOCK, CONFIG_CANOPENNODE_SDO_CLIENT_BLOCK) |                      \
	 ZBIT(CO_CONFIG_SDO_CLI_LOCAL, CONFIG_CANOPENNODE_SDO_CLIENT_LOCAL) |                      \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_SDO_CLIENT_CALLBACK) |               \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_SDO_CLIENT_TIMERNEXT) |                 \
	 ZBIT(CO_CONFIG_FLAG_OD_DYNAMIC, CONFIG_CANOPENNODE_SDO_CLIENT_OD_DYNAMIC))

/** @brief Client buffer size in bytes. */
#define CO_CONFIG_SDO_CLI_BUFFER_SIZE CONFIG_CANOPENNODE_SDO_CLIENT_BUFFER_SIZE
/** @brief SDO client timeout in ms. */
#define CO_CONFIG_SDO_CLI_TIMEOUT_MS  CONFIG_CANOPENNODE_SDO_CLIENT_TIMEOUT_MS
/** @} */

/** @name TIME object
 *  @brief Configure TIME producer/consumer and callbacks.
 *  @{
 */
#define CO_CONFIG_TIME                                                                             \
	(ZBIT(CO_CONFIG_TIME_ENABLE, CONFIG_CANOPENNODE_TIME_ENABLE) |                             \
	 ZBIT(CO_CONFIG_TIME_PRODUCER, CONFIG_CANOPENNODE_TIME_PRODUCER) |                         \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_TIME_CALLBACK) |                     \
	 ZBIT(CO_CONFIG_FLAG_OD_DYNAMIC, CONFIG_CANOPENNODE_TIME_OD_DYNAMIC))
/** @} */

/** @name SYNC / PDO
 *  @brief Configure SYNC and PDO features, timers, and callbacks.
 *  @{
 */
#define CO_CONFIG_SYNC                                                                             \
	(ZBIT(CO_CONFIG_SYNC_ENABLE, CONFIG_CANOPENNODE_SYNC_ENABLE) |                             \
	 ZBIT(CO_CONFIG_SYNC_PRODUCER, CONFIG_CANOPENNODE_SYNC_PRODUCER) |                         \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_SYNC_CALLBACK) |                     \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_SYNC_TIMERNEXT) |                       \
	 ZBIT(CO_CONFIG_FLAG_OD_DYNAMIC, CONFIG_CANOPENNODE_SYNC_OD_DYNAMIC))

#define CO_CONFIG_PDO                                                                              \
	(ZBIT(CO_CONFIG_RPDO_ENABLE, CONFIG_CANOPENNODE_RPDO_ENABLE) |                             \
	 ZBIT(CO_CONFIG_TPDO_ENABLE, CONFIG_CANOPENNODE_TPDO_ENABLE) |                             \
	 ZBIT(CO_CONFIG_RPDO_TIMERS_ENABLE, CONFIG_CANOPENNODE_RPDO_TIMERS_ENABLE) |               \
	 ZBIT(CO_CONFIG_TPDO_TIMERS_ENABLE, CONFIG_CANOPENNODE_TPDO_TIMERS_ENABLE) |               \
	 ZBIT(CO_CONFIG_PDO_SYNC_ENABLE, CONFIG_CANOPENNODE_PDO_SYNC_ENABLE) |                     \
	 ZBIT(CO_CONFIG_PDO_OD_IO_ACCESS, CONFIG_CANOPENNODE_PDO_OD_IO_ACCESS) |                   \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_PDO_CALLBACK) |                      \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_PDO_TIMERNEXT) |                        \
	 ZBIT(CO_CONFIG_FLAG_OD_DYNAMIC, CONFIG_CANOPENNODE_PDO_OD_DYNAMIC))
/** @} */

/** @name Storage
 *  @brief Enable storage glue and select backends.
 *  @{
 */
/** @brief Master switch for CANopenNode storage module. */
#define CO_CONFIG_STORAGE (ZBIT(CO_CONFIG_STORAGE_ENABLE, CONFIG_CANOPENNODE_STORAGE_ENABLE))

/** @brief Backend selections for the integration layer. */
#define CO_STORAGE_BACKEND_SETTINGS IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_BACKEND_SETTINGS)
#define CO_STORAGE_BACKEND_RAM      IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_BACKEND_RAM)
#define CO_STORAGE_BACKEND_NONE     IS_ENABLED(CONFIG_CANOPENNODE_STORAGE_BACKEND_NONE)
/** @} */

/** @name Program Download (CiA 302-3)
 *  @brief Configuration and Program download.
 *  @{
 */
#define CO_CONFIG_PROG_DOWNLOAD                                                                    \
	(ZBIT(CO_CONFIG_PROG_DOWNLOAD_ENABLE, CONFIG_CANOPENNODE_PROG_DOWNLOAD) |                  \
	 ZBIT(CO_CONFIG_PROG_DOWNLOAD_PERMANENT, CONFIG_CANOPENNODE_PROG_DOWNLOAD_PERMANENT))

/** @brief Maximum EDS file size in bytes. */
#define CO_CONFIG_PROG_DOWNLOAD_EDS_MAX_SIZE CONFIG_CANOPENNODE_PROG_DOWNLOAD_EDS_MAX_SIZE
/** @} */

/** @name LEDs (CiA 303-3)
 *  @brief Enable LED state machine and optional callback.
 *  @{
 */
#define CO_CONFIG_LEDS                                                                             \
	(ZBIT(CO_CONFIG_LEDS_ENABLE, CONFIG_CANOPENNODE_LEDS_ENABLE) |                             \
	 ZBIT(CO_CONFIG_LEDS_CALLBACK, CONFIG_CANOPENNODE_LEDS_CALLBACK) |                         \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_LEDS_TIMERNEXT))
/** @} */

/** @name SRDO / GFC
 *  @brief Safety-related data objects and global fail-safe command.
 *  @{
 */
#define CO_CONFIG_GFC                                                                              \
	(ZBIT(CO_CONFIG_GFC_ENABLE, CONFIG_CANOPENNODE_GFC_ENABLE) |                               \
	 ZBIT(CO_CONFIG_GFC_CONSUMER, CONFIG_CANOPENNODE_GFC_CONSUMER) |                           \
	 ZBIT(CO_CONFIG_GFC_PRODUCER, CONFIG_CANOPENNODE_GFC_PRODUCER))

#define CO_CONFIG_SRDO                                                                             \
	(ZBIT(CO_CONFIG_SRDO_ENABLE, CONFIG_CANOPENNODE_SRDO_ENABLE) |                             \
	 ZBIT(CO_CONFIG_SRDO_CHECK_TX, CONFIG_CANOPENNODE_SRDO_CHECK_TX) |                         \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_SRDO_CALLBACK) |                     \
	 ZBIT(CO_CONFIG_FLAG_TIMERNEXT, CONFIG_CANOPENNODE_SRDO_TIMERNEXT))

/** @brief Minimum SRDO Tx delay in microseconds. */
#define CO_CONFIG_SRDO_MINIMUM_DELAY CONFIG_CANOPENNODE_SRDO_MINIMUM_DELAY
/** @} */

/** @name LSS (Layer Setting Services)
 *  @brief Configure LSS master/slave and callbacks.
 *  @{
 */
#define CO_CONFIG_LSS                                                                              \
	(ZBIT(CO_CONFIG_LSS_SLAVE, CONFIG_CANOPENNODE_LSS_SLAVE) |                                 \
	 ZBIT(CO_CONFIG_LSS_SLAVE_FASTSCAN_DIRECT_RESPOND,                                         \
	      CONFIG_CANOPENNODE_LSS_SLAVE_FASTSCAN_DIRECT_RESPOND) |                              \
	 ZBIT(CO_CONFIG_LSS_MASTER, CONFIG_CANOPENNODE_LSS_MASTER) |                               \
	 ZBIT(CO_CONFIG_FLAG_CALLBACK_PRE, CONFIG_CANOPENNODE_LSS_CALLBACK))
/** @} */

/** @name ASCII Gateway (CiA 309)
 *  @brief Configure ASCII gateway features and buffer sizes.
 *  @{
 */
#define CO_CONFIG_GTW                                                                              \
	(ZBIT(CO_CONFIG_GTW_MULTI_NET, CONFIG_CANOPENNODE_GTW_MULTI_NET) |                         \
	 ZBIT(CO_CONFIG_GTW_ASCII, CONFIG_CANOPENNODE_GTW_ASCII) |                                 \
	 ZBIT(CO_CONFIG_GTW_ASCII_SDO, CONFIG_CANOPENNODE_GTW_ASCII_SDO) |                         \
	 ZBIT(CO_CONFIG_GTW_ASCII_NMT, CONFIG_CANOPENNODE_GTW_ASCII_NMT) |                         \
	 ZBIT(CO_CONFIG_GTW_ASCII_LSS, CONFIG_CANOPENNODE_GTW_ASCII_LSS) |                         \
	 ZBIT(CO_CONFIG_GTW_ASCII_LOG, CONFIG_CANOPENNODE_GTW_ASCII_LOG) |                         \
	 ZBIT(CO_CONFIG_GTW_ASCII_ERROR_DESC, CONFIG_CANOPENNODE_GTW_ASCII_ERROR_DESC) |           \
	 ZBIT(CO_CONFIG_GTW_ASCII_PRINT_HELP, CONFIG_CANOPENNODE_GTW_ASCII_PRINT_HELP) |           \
	 ZBIT(CO_CONFIG_GTW_ASCII_PRINT_LEDS, CONFIG_CANOPENNODE_GTW_ASCII_PRINT_LEDS))

/** @brief Block download loop count for the gateway. */
#define CO_CONFIG_GTW_BLOCK_DL_LOOP  CONFIG_CANOPENNODE_GTW_BLOCK_DL_LOOP
/** @brief ASCII gateway communication buffer size in bytes. */
#define CO_CONFIG_GTWA_COMM_BUF_SIZE CONFIG_CANOPENNODE_GTWA_COMM_BUF_SIZE
/** @brief ASCII gateway log buffer size in bytes. */
#define CO_CONFIG_GTWA_LOG_BUF_SIZE  CONFIG_CANOPENNODE_GTWA_LOG_BUF_SIZE
/** @} */

/** @name CRC16
 *  @brief Enable/route CRC16 implementation used by various modules.
 *  @{
 */
#define CO_CONFIG_CRC16                                                                            \
	(ZBIT(CO_CONFIG_CRC16_ENABLE, CONFIG_CANOPENNODE_CRC16_ENABLE) |                           \
	 ZBIT(CO_CONFIG_CRC16_EXTERNAL, CONFIG_CANOPENNODE_CRC16_EXTERNAL))
/** @} */

/** @name FIFO
 *  @brief Configure FIFO utilities and optional ASCII helpers.
 *  @{
 */
#define CO_CONFIG_FIFO                                                                             \
	(ZBIT(CO_CONFIG_FIFO_ENABLE, CONFIG_CANOPENNODE_FIFO_ENABLE) |                             \
	 ZBIT(CO_CONFIG_FIFO_ALT_READ, CONFIG_CANOPENNODE_FIFO_ALT_READ) |                         \
	 ZBIT(CO_CONFIG_FIFO_CRC16_CCITT, CONFIG_CANOPENNODE_FIFO_CRC16_CCITT) |                   \
	 ZBIT(CO_CONFIG_FIFO_ASCII_COMMANDS, CONFIG_CANOPENNODE_FIFO_ASCII_COMMANDS) |             \
	 ZBIT(CO_CONFIG_FIFO_ASCII_DATATYPES, CONFIG_CANOPENNODE_FIFO_ASCII_DATATYPES))
/** @} */

/** @name Trace
 *  @brief Configure trace recorder and integer type selection.
 *  @{
 */
#define CO_CONFIG_TRACE                                                                            \
	(ZBIT(CO_CONFIG_TRACE_ENABLE, CONFIG_CANOPENNODE_TRACE_ENABLE) |                           \
	 ZBIT(CO_CONFIG_TRACE_OWN_INTTYPES, CONFIG_CANOPENNODE_TRACE_OWN_INTTYPES))
/** @} */

/** @name Debug
 *  @brief Enable debug features at module granularity.
 *  @{
 */
#define CO_CONFIG_DEBUG                                                                            \
	(ZBIT(CO_CONFIG_DEBUG_COMMON, CONFIG_CANOPENNODE_DEBUG_COMMON) |                           \
	 ZBIT(CO_CONFIG_DEBUG_SDO_CLIENT, CONFIG_CANOPENNODE_DEBUG_SDO_CLIENT) |                   \
	 ZBIT(CO_CONFIG_DEBUG_SDO_SERVER, CONFIG_CANOPENNODE_DEBUG_SDO_SERVER))

/** @} */

/** @name Zephyr integration (TX workqueue)
 *  @brief Configure TX worker thread resources used by the integration.
 *  @{
 */
/** @brief Stack size for the CAN TX workqueue thread. */
#define CO_TX_WQ_STACK_SIZE CONFIG_CANOPENNODE_TX_WORKQUEUE_STACK_SIZE
/** @brief Priority for the CAN TX workqueue thread. */
#define CO_TX_WQ_PRIORITY   CONFIG_CANOPENNODE_TX_WORKQUEUE_PRIORITY
/** @} */

/** @name EDS/DCF
 *  @brief Path to the EDS/DCF file used by the application (if any).
 *  @{
 */
/** @brief File system path to the node’s EDS file. */
#define CO_EDS_FILE_PATH CONFIG_CANOPENNODE_EDS_FILE_PATH
/** @} */

/** @} */ /* end of co_zephyr_config */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_CONFIG_H */
