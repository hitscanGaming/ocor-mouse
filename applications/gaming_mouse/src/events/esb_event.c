/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <assert.h>
#include <zephyr/sys/util.h>
#include <stdio.h>

#include "esb_event.h"

static const char *const state_name[] = {[ESB_STATE_DISCONNECTED] = "DISCONNECTED",
					 [ESB_STATE_POWERED] = "POWERED",
					 [ESB_STATE_ACTIVE] = "ACTIVE",
					 [ESB_STATE_SUSPENDED] = "SUSPENDED"};

static void log_esb_state_event(const struct app_event_header *aeh)
{
	const struct esb_state_event *event = cast_esb_state_event(aeh);

	BUILD_ASSERT(ARRAY_SIZE(state_name) == ESB_STATE_COUNT, "Invalid number of elements");

	__ASSERT_NO_MSG(event->state < ESB_STATE_COUNT);

	APP_EVENT_MANAGER_LOG(aeh, "state:%s", state_name[event->state]);
}

APP_EVENT_TYPE_DEFINE(esb_state_event, log_esb_state_event, NULL,
		      APP_EVENT_FLAGS_CREATE(IF_ENABLED(CONFIG_DESKTOP_INIT_LOG_ESB_STATE_EVENT,
							(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE))));
