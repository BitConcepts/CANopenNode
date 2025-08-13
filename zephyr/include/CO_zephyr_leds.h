/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-to-CANopenNode LED bridge.
 *
 * Mirrors the computed CANopen RUN/ERR LED states (from CO_LEDs) to Zephyr GPIOs
 * using devicetree aliases. Respects ACTIVE_LOW/HIGH via DT flags and provides
 * a callback-based hookup and one-shot sync utility.
 *
 * @file        CO_zephyr_leds.h
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

#ifndef ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_LEDS_H
#define ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_LEDS_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include "303/CO_LEDs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup co_zephyr_leds CANopenNode ↔ Zephyr LED helpers
 * @brief Mirror CANopen RUN/ERR LED states to Zephyr GPIOs.
 *
 * This helper binds the CANopen LED state machine from @ref CO_LEDs_t (CiA 303-3)
 * to two Zephyr-controlled GPIOs selected via **devicetree aliases**:
 *
 * - `co_led_run` → RUN (green) LED
 * - `co_led_err` → ERR (red) LED
 *
 * The active level and inversion are taken from the GPIO flags on the aliased
 * pins (e.g., `GPIO_ACTIVE_LOW`). Pins are configured as `GPIO_OUTPUT_INACTIVE`
 * during initialization. After you connect the callback, each call to
 * `CO_LEDs_process()` updates the physical pins to match the synthesized
 * CANopen RUN/ERR indicators (bit @ref CO_LED_CANopen in `LEDgreen`/`LEDred`).
 *
 * ### Devicetree requirements
 * Provide two DT aliases that point to GPIOs, for example:
 * @code{.dts}
 * / {
 *   aliases {
 *     co_led_run = &led0;
 *     co_led_err = &led1;
 *   };
 * };
 *
 * &led0 { gpios = <&gpio0 13 GPIO_ACTIVE_LOW>; status = "okay"; };
 * &led1 { gpios = <&gpio0 14 GPIO_ACTIVE_LOW>; status = "okay"; };
 * @endcode
 *
 * ### Typical usage
 * @code{.c}
 * // After CO_LEDs_init(&leds, ...):
 * int rc = co_zephyr_leds_init_dt_aliases();
 * if (rc == 0) {
 *     co_zephyr_leds_connect_callback(&leds);
 *     // Optionally mirror immediately:
 *     co_zephyr_leds_sync_once(&leds);
 * }
 * @endcode
 *
 * @{
 */

/**
 * @brief Initialize GPIOs for CANopen LEDs using devicetree aliases.
 *
 * Resolves the `co_led_run` and `co_led_err` DT aliases to GPIO pins,
 * validates their controller readiness, and configures both pins as
 * `GPIO_OUTPUT_INACTIVE`. Active level and inversion are honored from
 * each pin's devicetree flags (e.g., `GPIO_ACTIVE_LOW`).
 *
 * @note This function does **not** register any callbacks; call
 *       co_zephyr_leds_connect_callback() after @ref CO_LEDs_init().
 *
 * @retval 0        Success.
 * @retval -ENODEV  A referenced GPIO controller is not ready, or required DT
 *                  aliases are missing/disabled.
 * @retval <0       A negative errno returned by @ref gpio_pin_configure_dt.
 */
int co_zephyr_leds_init_dt_aliases(void);

/**
 * @brief Connect the CANopen LED callback to mirror states to hardware.
 *
 * Registers an internal callback via @ref CO_LEDs_registerCallback so that
 * every @ref CO_LEDs_process update pushes the synthesized RUN/ERR states
 * to the configured GPIOs. Safe to call once after @ref CO_LEDs_init.
 *
 * @param[in] leds  Pointer to a valid @ref CO_LEDs_t instance.
 *
 * @note This function does not reconfigure GPIOs; call
 *       co_zephyr_leds_init_dt_aliases() first.
 * @note If @p leds is `NULL`, the function returns immediately.
 */
void co_zephyr_leds_connect_callback(CO_LEDs_t *leds);

/**
 * @brief One-shot mirror of the current CANopen LED state to GPIOs.
 *
 * Evaluates the current `LEDgreen`/`LEDred` fields and writes the corresponding
 * RUN/ERR outputs once. Useful immediately after initialization to ensure the
 * physical LEDs reflect the current state without waiting for the next
 * @ref CO_LEDs_process call.
 *
 * @param[in] leds  Pointer to a valid @ref CO_LEDs_t instance.
 *
 * @note If the GPIOs were not initialized (hardware not ready) or @p leds is
 *       `NULL`, the call is ignored.
 */
void co_zephyr_leds_sync_once(CO_LEDs_t *leds);

/** @} */ /* end of co_zephyr_leds */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_CANOPENNODE_CO_ZEPHYR_LEDS_H */
