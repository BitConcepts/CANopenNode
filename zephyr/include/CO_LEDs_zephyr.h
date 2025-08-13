#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include "303/CO_LEDs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize GPIOs for CANopen LEDs using DT aliases:
 *   - alias: co_led_run  -> RUN/green LED
 *   - alias: co_led_err  -> ERR/red  LED
 *
 * Pins are configured OUTPUT_INACTIVE. Active level/inversion
 * is taken from devicetree flags (GPIO_ACTIVE_LOW/HIGH).
 *
 * @return 0 on success, -ENODEV if aliases missing or devices not ready,
 *         or a negative errno from gpio_pin_configure_dt().
 */
int co_leds_zephyr_init_dt_aliases(void);

/**
 * Register the CANopen LED callback so LED states are mirrored to hardware
 * after each CO_LEDs_process() update. Call once after CO_LEDs_init().
 */
void co_leds_zephyr_connect_callback(CO_LEDs_t *leds);

/**
 * Optional: immediately mirror the current CO LEDs once (e.g., right after init).
 * Safe to call anytime.
 */
void co_leds_zephyr_sync_once(CO_LEDs_t *leds);

#ifdef __cplusplus
}
#endif
