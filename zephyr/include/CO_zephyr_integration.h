/**
 * @file
 * @brief CANopenNode Zephyr integration: RT thread & mainline processing
 *
 * This header defines the public interface for the Zephyr-native CANopenNode
 * integration, providing lifecycle management and scheduler integration.
 *
 * - **RT thread**: Runs SYNC, RPDO, TPDO processing on PRE-callback events.
 * - **Mainline (CO_process)**: Executes periodically to handle internal state machine.
 * - **Auto-start**: Supports auto-initialization via SYS_INIT.
 *
 * @note Requires generated `OD.c/h` and a configured CAN device (via devicetree or Kconfig).
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
 * @brief Start CANopenNode with explicit device, node ID, and bitrate
 *
 * Initializes the full CANopen stack and RT thread using the given CAN device,
 * node ID (1–127), and bitrate in kbps. If bitrate is 0, Kconfig default is used.
 *
 * @param can_dev      CAN device pointer (NULL = use default from the device tree)
 * @param node_id      CANopen Node ID (1–127)
 * @param bitrate_kbps CAN bitrate in kbps (0 = use default from Kconfig)
 *
 * @retval 0       Success
 * @retval -ENODEV Invalid or unready device
 * @retval -EINVAL Invalid arguments or CANopen error
 * @retval -EALREADY Already running
 * @retval -ENOMEM Allocation or storage error
 */
int co_canopen_start(const struct device *can_dev, uint8_t node_id, uint16_t bitrate_kbps);

/**
 * @brief Stop the CANopen stack and worker thread
 *
 * Tears down all internal structures and disables the CAN controller.
 * Safe to call multiple times.
 */
void co_canopen_stop(void);

/**
 * @brief Check if the CANopen stack is running
 *
 * @retval true  The stack is active and running
 * @retval false The stack is stopped or not yet initialized
 */
bool co_canopen_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_INTEGRATION_H */
