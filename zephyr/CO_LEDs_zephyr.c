#include "co_leds_zephyr.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

/* --- DT aliases (Approach #1) --- */
#define RUN_NODE DT_ALIAS(co_led_run)
#define ERR_NODE DT_ALIAS(co_led_err)

/* Build-time checks: make sure aliases exist and are enabled */
BUILD_ASSERT(DT_NODE_HAS_STATUS(RUN_NODE, okay), "DT alias 'co-led-run' missing or disabled");
BUILD_ASSERT(DT_NODE_HAS_STATUS(ERR_NODE, okay), "DT alias 'co-led-err' missing or disabled");

/* Resolve GPIO specs (handles ACTIVE_LOW/HIGH) */
static const struct gpio_dt_spec LED_RUN = GPIO_DT_SPEC_GET(RUN_NODE, gpios);
static const struct gpio_dt_spec LED_ERR = GPIO_DT_SPEC_GET(ERR_NODE, gpios);

static bool hw_ready;

/* Mirror the synthesized CANopen RUN/ERR bits to hardware */
static void co_leds_cb(CO_LEDs_t *leds, void *user_arg)
{
	ARG_UNUSED(user_arg);
	if (!hw_ready || leds == NULL) {
		return;
	}

	/* Only the “computed on/off” bit (CO_LED_CANopen) matters for hardware */
	const bool run_on = (leds->LEDgreen & CO_LED_CANopen) != 0;
	const bool err_on = (leds->LEDred & CO_LED_CANopen) != 0;

	/* gpio_pin_set_dt() respects ACTIVE_LOW via DT flags */
	(void)gpio_pin_set_dt(&LED_RUN, run_on);
	(void)gpio_pin_set_dt(&LED_ERR, err_on);
}

int co_leds_zephyr_init_dt_aliases(void)
{
	/* Validate ports */
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

void co_leds_zephyr_connect_callback(CO_LEDs_t *leds)
{
	if (!leds) {
		return;
	}
	/* Requires your CO_CONFIG_LEDS_CALLBACK integration */
	CO_LEDs_registerCallback(leds, co_leds_cb, NULL);

	/* Push current state to pins immediately (optional nicety) */
	co_leds_zephyr_sync_once(leds);
}

void co_leds_zephyr_sync_once(CO_LEDs_t *leds)
{
	if (!hw_ready || !leds) {
		return;
	}

	const bool run_on = (leds->LEDgreen & CO_LED_CANopen) != 0;
	const bool err_on = (leds->LEDred & CO_LED_CANopen) != 0;

	(void)gpio_pin_set_dt(&LED_RUN, run_on);
	(void)gpio_pin_set_dt(&LED_ERR, err_on);
}
