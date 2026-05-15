/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief ESB event header file.
 */

#ifndef _ESB_EVENT_H_
#define _ESB_EVENT_H_

#include <zephyr/toolchain.h>

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESB Event
 * @defgroup nrf_desktop_esb_event ESB Event
 *
 * File defines a set of events used to transmit the information about ESB
 * state between the application modules.
 *
 * @{
 */

/** @brief Peer states. */
enum esb_state {
	/** Cable is not attached. */
	ESB_STATE_DISCONNECTED,
	ESB_STATE_PAIRING,
	/** Cable attached but no communication. */
	ESB_STATE_POWERED,
	/** Cable attached and data is exchanged. */
	ESB_STATE_ACTIVE,
	/** Cable attached but port is suspended. */
	ESB_STATE_SUSPENDED,

	/** Number of states. */
	ESB_STATE_COUNT
};

/** @brief ESB state event. */
struct esb_state_event {
	/** Event header. */
	struct app_event_header header;

	/** State of the ESB module. */
	enum esb_state state;
};
APP_EVENT_TYPE_DECLARE(esb_state_event);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* _ESB_EVENT_H_ */
