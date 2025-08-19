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

#include <stddef.h>
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
bool canopen_is_running(void);

/**
 * @brief Weak hook for providing the CANopen Node-ID.
 *
 * This function is provided as a weak symbol and may be overridden by the
 * application to supply a board- or system-specific Node-ID. If not
 * overridden, the default implementation uses a fixed Node-ID from
 * @c CONFIG_CANOPENNODE_INIT_NODE_ID or other built-in mechanism.
 *
 * The Zephyr integration calls this hook when @ref canopen_start() is invoked
 * with @p node_id > 127 (meaning "use default resolution"). The hook must
 * return a valid CANopen Node-ID in the range 1..127. Returning 0 or a value
 * greater than 127 is treated as "unspecified" or "invalid", and the
 * integration will fall back to @c CONFIG_CANOPENNODE_INIT_NODE_ID.
 *
 * The function is invoked in the context of @ref canopen_start() before the
 * stack is started (i.e., not from an ISR). Implementations should keep the
 * logic fast and non-blocking. It is safe to read from non-volatile storage
 * or board configuration straps provided this does not block excessively.
 *
 * @param[in] ud
 *     Optional user data passed through from the integration. May be @c NULL.
 *
 * @retval 1..127  Valid Node-ID to use.
 * @retval 0       Invalid/unspecified, fall back to @c CONFIG_CANOPENNODE_INIT_NODE_ID.
 * @retval >127    Invalid/unspecified, fall back to @c CONFIG_CANOPENNODE_INIT_NODE_ID.
 *
 * @see canopen_start()
 */
__weak uint8_t canopen_get_node_id_hook(void *ud);

/** @} */ /* end of co_zephyr_integration */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_INTEGRATION_H */
