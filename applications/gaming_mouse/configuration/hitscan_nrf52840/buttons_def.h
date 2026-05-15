/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <caf/gpio_pins.h>

/* This configuration file is included only once from button module and holds
 * information about pins forming keyboard matrix.
 */

/* This structure enforces the header file is included only once in the build.
 * Violating this requirement triggers a multiple definition error at link time.
 */
const struct {
} buttons_def_include_once;

static const struct gpio_pin col[] = {};

static const struct gpio_pin row[] = {
	{.port = 0, .pin = 30}, // left key
	{.port = 0, .pin = 29}, // right key
	{.port = 0, .pin = 28}, // middle key
	{.port = 0, .pin = 2},	// backward  key
	{.port = 0, .pin = 3},	// forward key
	{.port = 1, .pin = 15}, // cpi key
				// { .port = 1, .pin = 3 },
				// { .port = 1, .pin = 5 },
				// { .port = 0, .pin = 23 },
				// { .port = 0, .pin = 25 },
				// { .port = 1, .pin = 4 },
				// { .port = 1, .pin = 6 },
};
