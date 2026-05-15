/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <caf/click_detector.h>
#include <caf/key_id.h>
/* This configuration file is included only once from click_detector module
 * and holds information about click detector configuration.
 */

/* This structure enforces the header file is included only once in the build.
 * Violating this requirement triggers a multiple definition error at link time.
 */
static const struct {
} click_detector_def_include_once;

static const struct click_detector_config click_detector_config[] = {
#if CONFIG_DESKTOP_BLE_PEER_CONTROL
	{
		.key_id = CONFIG_DESKTOP_BLE_PEER_CONTROL_BUTTON,
		.consume_button_event = true,
	},
#endif /* CONFIG_DESKTOP_BLE_PEER_CONTROL */
	{
		.key_id = 5,
		.consume_button_event = true,
	},
};

/* -------------------------------------------------------------------------- */
/* Custom Combo Configuration                                                 */
/* -------------------------------------------------------------------------- */
#define COMBO_HOLD_TIME_MS 3000
#define COMBO_KEY_ID	   0xFFFF // Virtual ID for the generated event
/* Based on your buttons_def.h: Row 0=Left, Row 1=Right, Row 2=Middle */
#define ID_LEFT		   KEY_ID(0, 0)
#define ID_RIGHT	   KEY_ID(0, 1)
#define ID_MIDDLE	   KEY_ID(0, 2)
