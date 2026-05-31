/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-to-CANopenNode integration API.
 *
 * Provides a runtime bridge to start/stop the CANopen stack with a selected
 * Zephyr CAN device, Node-ID, and bitrate, enabling control from code in
 * addition to prj.conf and devicetree.
 *
 * @file        CO_zephyr_integration.h
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

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_INTEGRATION_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_INTEGRATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>

#include "CANopen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup co_zephyr_integration CANopenNode ↔ Zephyr runtime integration
 * @brief Start/stop helpers that wire CANopenNode to a Zephyr CAN device.
 *
 * This API brings up the full CANopenNode stack on Zephyr with an explicitly
 * chosen CAN device, Node-ID, and bitrate. It complements Kconfig/devicetree
 * configuration by allowing programmatic control at runtime.
 *
 * ### Behavior
 * - Initializes CANopenNode (CAN, NMT, SDO, PDO, etc.) and enters normal mode.
 * - Optionally creates/uses an RT processing thread (SYNC/RPDO/TPDO) if enabled
 *   by Kconfig in the integration module.
 * - Supports optional persistent parameter storage when configured.
 *
 * ### Devicetree / Kconfig
 * - If @p can_dev is `NULL`, the default CAN device is resolved from
 *   `DT_CHOSEN(zephyr_canbus)`.
 * - When @p bitrate_kbps is 0, the bitrate falls back to the Kconfig default
 *   (e.g., `CONFIG_CANOPENNODE_BITRATE_KBPS`).
 *
 * ### Typical usage
 * @code{.c}
 * #include "CO_zephyr_integration.h"
 *
 * void app_start_canopen(void)
 * {
 *     // Use default CAN dev from DT, Node-ID from Kconfig, default bitrate
 *     int rc = canopen_start(NULL, CONFIG_CANOPENNODE_INIT_NODE_ID, 0);
 *     if (rc != 0) {
 *         printk("CANopen start failed: %d\n", rc);
 *         return;
 *     }
 *
 *     if (canopen_is_running()) {
 *         printk("CANopen is up\n");
 *     }
 * }
 *
 * void app_stop_canopen(void)
 * {
 *     canopen_stop();
 * }
 * @endcode
 *
 * @{
 */

extern atomic_t g_running;
extern CO_t *CO;

/**
 * @brief Start CANopenNode with explicit device, node ID, and bitrate.
 *
 * Initializes the full CANopen stack and (optionally) the real-time processing
 * thread using the given CAN device, CANopen Node-ID (0–127), and bitrate in
 * kbps. When @p can_dev is `NULL`, the CAN device is taken from devicetree
 * (the `zephyr,canbus` chosen node). When @p bitrate_kbps is 0, the Kconfig
 * default is used.
 *
 * @param[in] can_dev       Zephyr CAN device pointer
 *                          (`NULL` = use DT chosen `zephyr,canbus`).
 * @param[in] node_id       CANopen Node-ID in the range 1–127.
 * @param[in] bitrate_kbps  CAN bitrate in kbps
 *                          (0 = use Kconfig default, e.g., `CONFIG_CANOPENNODE_BITRATE_KBPS`).
 *
 * @retval 0         Success.
 * @retval -ENODEV   Invalid, missing, or not-ready CAN device.
 * @retval -EINVAL   Invalid arguments (e.g., Node-ID out of range) or CANopen init error.
 * @retval -EALREADY Integration is already running.
 * @retval -ENOMEM   Allocation or storage initialization error.
 *
 * @note If persistent storage is enabled via Kconfig, initialization may
 *       perform a one-time load and can surface backend errors via logs.
 * @pre  Zephyr kernel is initialized; @p can_dev (if non-NULL) is ready.
 * @warning Do not call from ISR context.
 */
int canopen_start(const struct device *can_dev, uint8_t node_id, uint16_t bitrate_kbps);

/**
 * @brief Stop the CANopen stack and worker thread.
 *
 * Tears down all internal structures, disables the CAN controller, and
 * stops the integration’s RT processing thread if present. Safe to call
 * multiple times; subsequent calls become no-ops.
 */
void canopen_stop(void);

/**
 * @brief Query whether the CANopen stack is running.
 *
 * @retval true   The stack is active and running.
 * @retval false  The stack is stopped or not yet initialized.
 */
static inline bool canopen_is_running(void)
{
	return (bool)atomic_get(&g_running);
}

/**
 * @brief Check whether a specific application error is currently active.
 *
 * Thin wrapper around CO_isError(). This tests an error/status bit managed by
 * the CANopenNode Emergency/Error Manager.
 *
 * @param errorBit  Error/status selector (one of the CO_EM_* values such as
 *                  CO_EM_GENERIC_ERROR).  Note: kept as @c uint8_t to match
 *                  the current signature; consider using @c CO_EM_t for clarity.
 *
 * @retval true   The selected error is active.
 * @retval false  The selected error is not active, or the stack is not initialized.
 */
static inline bool canopen_is_error(uint8_t errorBit)
{
	return (CO && CO->em) ? CO_isError(CO->em, errorBit) : false;
}

/**
 * @brief Read the CiA 301 Error Register (object 0x1001).
 *
 * Returns the current 8-bit Error Register maintained by the stack (per CiA 301),
 * or 0 if the stack has not been initialized yet.
 *
 * @return Current value of object 0x1001 (Error Register), or 0 on uninitialized stack.
 */
static inline uint8_t canopen_get_error_register(void)
{
	return (CO && CO->em && CO->em->errorRegister) ? *(CO->em->errorRegister) : (uint8_t)0;
}

/**
 * @brief Report a generic application error via the Emergency (EMCY) object.
 *
 * Sets the CO_EM_GENERIC_ERROR condition and requests the stack to emit an EMCY
 * with the given 16-bit EMCY error code (CiA 301) and a manufacturer-specific
 * 32-bit info field.
 *
 * Typical usage: call when detecting an application fault (e.g., invalid config,
 * overtemperature) to make the error visible to the network and to log it in
 * 0x1003 (Pre-defined Error Field) according to the stack’s configuration.
 *
 * @param errorBit   Error/status selector (one of the CO_EM_* values such as
 *                   CO_EM_GENERIC_ERROR).  Note: kept as @c uint8_t to match the
 *                   current signature; consider using @c CO_EM_t for clarity.
 * @param errorCode  16-bit EMCY error code (CiA 301 compliant).
 * @param infoCode   32-bit manufacturer-specific info (placed in EMCY data as
 *                   defined by the stack).
 *
 * @note No-op if the stack (CO/CO->em) is not initialized.
 */
void canopen_error_report(uint8_t errorBit, uint16_t errorCode, uint32_t infoCode);

/**
 * @brief Clear the previously reported generic application error.
 *
 * Clears the CO_EM_GENERIC_ERROR condition using CO_errorReset(). Depending on
 * stack configuration, this may also cause an “error reset/cleared” EMCY
 * indication to be sent to the network.
 *
 * @param errorBit  Error/status selector (one of the CO_EM_* values such as
 *                  CO_EM_GENERIC_ERROR).  Note: kept as @c uint8_t to match the
 *                  current signature; consider using @c CO_EM_t for clarity.
 * @param infoCode  32-bit manufacturer-specific info (placed in EMCY data as
 *                  defined by the stack).
 * @note No-op if the stack (CO/CO->em) is not initialized.
 */
void canopen_error_reset(uint8_t errorBit, uint32_t infoCode);

/**
 * @brief Weak symbol for supplying the CANopen Node-ID.
 *
 * Override this function in your application to return a board- or system-
 * specific Node-ID (e.g., read from a DIP switch, NVS, or hardware straps).
 * If not overridden, the default implementation returns
 * @c CONFIG_CANOPENNODE_INIT_NODE_ID.
 *
 * The integration calls this function during auto-start (@ref SYS_INIT) and
 * during a comm-reset (@c CO_RESET_COMM) to resolve the node ID without
 * explicit user parameters.
 *
 * @retval 1..127  Valid CANopen Node-ID.
 * @retval 0       Treated as invalid; auto-start will use CONFIG_CANOPENNODE_INIT_NODE_ID.
 *
 * @note Do not block in this function; it is called from a thread context but
 *       must return quickly. It is **not** called from an ISR.
 * @see canopen_start()
 */
__weak uint8_t canopen_get_node_id(void);

/** @} */ /* end of co_zephyr_integration */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_INTEGRATION_H */
