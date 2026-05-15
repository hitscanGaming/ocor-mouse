/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "polling_rate.h"

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(polling_rate, LOG_LEVEL_INF);

/* Counter device from devicetree */
#define POLLING_RATE_TIMER_NODE DT_NODELABEL(timer3)

#if DT_NODE_HAS_STATUS(POLLING_RATE_TIMER_NODE, okay)
static const struct device *const counter_dev = DEVICE_DT_GET(POLLING_RATE_TIMER_NODE);
#else
#error "Timer node timer3 not enabled in devicetree"
#endif

/* Current polling rate in Hz */
static uint32_t current_rate_hz = CONFIG_POLLING_RATE_DEFAULT_HZ;

/* User callback */
static polling_rate_callback_t user_callback;

/* Running state */
static bool is_running;

/**
 * @brief Counter ISR handler
 */
static void counter_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (user_callback) {
		user_callback();
	}
}

int polling_rate_init(void)
{
	if (!device_is_ready(counter_dev)) {
		LOG_ERR("Counter device not ready");
		return -ENODEV;
	}

	uint32_t freq = counter_get_frequency(counter_dev);
	LOG_INF("Polling rate module initialized. Counter freq: %u Hz", freq);

	return 0;
}

void polling_rate_set_callback(polling_rate_callback_t cb)
{
	user_callback = cb;
}

void polling_rate_set_hz(uint32_t rate_hz)
{
	/* Clamp to configured bounds */
	if (rate_hz < CONFIG_POLLING_RATE_MIN_HZ) {
		rate_hz = CONFIG_POLLING_RATE_MIN_HZ;
	}
	if (rate_hz > CONFIG_POLLING_RATE_MAX_HZ) {
		rate_hz = CONFIG_POLLING_RATE_MAX_HZ;
	}

	current_rate_hz = rate_hz;

	/* If already running, restart with new rate */
	if (is_running) {
		polling_rate_start();
	}
}

uint32_t polling_rate_get_interval_us(void)
{
	if (current_rate_hz == 0) {
		return 1000000 / CONFIG_POLLING_RATE_DEFAULT_HZ;
	}
	return 1000000 / current_rate_hz;
}

uint32_t polling_rate_get_hz(void)
{
	return current_rate_hz;
}

int polling_rate_start(void)
{
	if (!device_is_ready(counter_dev)) {
		return -ENODEV;
	}

	uint32_t interval_us = polling_rate_get_interval_us();

	/* Ensure clean state */
	counter_stop(counter_dev);

	struct counter_top_cfg top_cfg = {
		.ticks = counter_us_to_ticks(counter_dev, interval_us),
		.callback = counter_handler,
		.user_data = NULL,
		.flags = 0,
	};

	int err = counter_set_top_value(counter_dev, &top_cfg);
	if (err) {
		LOG_ERR("Failed to set counter top value: %d", err);
		return err;
	}

	err = counter_start(counter_dev);
	if (err) {
		LOG_ERR("Failed to start counter: %d", err);
		return err;
	}

	is_running = true;
	LOG_DBG("Polling started at %u Hz (%u us)", current_rate_hz, interval_us);

	return 0;
}

int polling_rate_stop(void)
{
	if (!device_is_ready(counter_dev)) {
		return -ENODEV;
	}

	int err = counter_stop(counter_dev);
	is_running = false;

	LOG_DBG("Polling stopped");

	return err;
}

bool polling_rate_is_running(void)
{
	return is_running;
}
