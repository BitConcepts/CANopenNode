/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-to-CANopenNode LED bridge.
 *
 * Mirrors the computed CANopen RUN/ERR LED states (from CO_LEDs) to Zephyr GPIOs
 * using devicetree aliases. Respects ACTIVE_LOW/HIGH via DT flags and provides
 * a callback-based hookup and one-shot sync utility.
 *
 * @file        CO_zephyr_leds.c
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

#include "CO_zephyr_leds.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define RUN_NODE DT_ALIAS(co_led_run)
#define ERR_NODE DT_ALIAS(co_led_err)

/* Ensure the expected DT aliases exist and are enabled. */
BUILD_ASSERT(DT_NODE_HAS_STATUS(RUN_NODE, okay), "DT alias 'co-led-run' missing or disabled");
BUILD_ASSERT(DT_NODE_HAS_STATUS(ERR_NODE, okay), "DT alias 'co-led-err' missing or disabled");

/* File-local GPIO descriptors resolved from devicetree. */
static const struct gpio_dt_spec LED_RUN = GPIO_DT_SPEC_GET(RUN_NODE, gpios);
static const struct gpio_dt_spec LED_ERR = GPIO_DT_SPEC_GET(ERR_NODE, gpios);

/* Set true after successful pin configuration; guards callbacks before init. */
static bool hw_ready;

/*
 * CANopen LED callback.
 * Maps CO_LEDs_t state bits to the two GPIOs. Safe to call anytime after init.
 */
static void co_zephyr_leds_cb(CO_LEDs_t *leds, void *user_arg)
{
	ARG_UNUSED(user_arg);
	if (!hw_ready || leds == NULL) {
		return;
	}

	const bool run_on = (leds->LEDgreen & CO_LED_CANopen) != 0;
	const bool err_on = (leds->LEDred & CO_LED_CANopen) != 0;

	(void)gpio_pin_set_dt(&LED_RUN, run_on);
	(void)gpio_pin_set_dt(&LED_ERR, err_on);
}

int co_zephyr_leds_init_dt_aliases(void)
{
	if (!device_is_ready(LED_RUN.port) || !device_is_ready(LED_ERR.port)) {
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&LED_RUN, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&LED_ERR, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

	hw_ready = true;
	return 0;
}

void co_zephyr_leds_connect_callback(CO_LEDs_t *leds)
{
	if (!leds) {
		return;
	}
	CO_LEDs_registerCallback(leds, co_zephyr_leds_cb, NULL);
	co_zephyr_leds_sync_once(leds);
}

void co_zephyr_leds_sync_once(CO_LEDs_t *leds)
{
	if (!hw_ready || !leds) {
		return;
	}

	const bool run_on = (leds->LEDgreen & CO_LED_CANopen) != 0;
	const bool err_on = (leds->LEDred & CO_LED_CANopen) != 0;

	(void)gpio_pin_set_dt(&LED_RUN, run_on);
	(void)gpio_pin_set_dt(&LED_ERR, err_on);
}
