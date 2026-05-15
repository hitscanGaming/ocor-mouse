/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Implements: REQ-USB-002, REQ-BUTTON-001
 * (vendor-defined HID channel for the web configurator; button mapping persistence)
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>

#define MODULE userpin
#include <caf/events/module_state_event.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <caf/events/power_event.h>

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

/* HARDWARE CONFIGURATION */
static const struct gpio_dt_spec ir_en_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(iren), gpios);

/* NOTE: Indicator/LED power control moved to leds.c */

static int init(void)
{
	int err;

	LOG_DBG("Initializing user pins");

	if (!gpio_is_ready_dt(&ir_en_pin)) {
		LOG_ERR("Device %s is not ready", ir_en_pin.port->name);
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&ir_en_pin, GPIO_OUTPUT_ACTIVE);
	if (err) {
		LOG_ERR("Failed to configure %s (err: %d)", ir_en_pin.port->name, err);
		return err;
	}

	LOG_INF("User pins initialized");
	return 0;
}

static void turn_pin_on(void)
{
	int ret = gpio_pin_set_dt(&ir_en_pin, 1);
	if (ret) {
		LOG_ERR("Failed to enable IR_EN (err: %d)", ret);
	}

	module_set_state(MODULE_STATE_READY);
}

static void turn_pin_off(void)
{
	int ret = gpio_pin_set_dt(&ir_en_pin, 0);
	if (ret) {
		LOG_ERR("Failed to disable IR_EN (err: %d)", ret);
	}

	module_set_state(MODULE_STATE_STANDBY);
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	static bool initialized = false;

	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			if (!initialized) {
				int err = init();
				if (!err) {
					initialized = true;
				} else {
					module_set_state(MODULE_STATE_ERROR);
					return false;
				}
			}
			turn_pin_on();
		}
		return false;
	}

	if (is_wake_up_event(aeh)) {
		if (initialized) {
			turn_pin_on();
		}
		return false;
	}

	if (is_power_down_event(aeh)) {
		if (initialized) {
			turn_pin_off();
		}
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);