/*
 * esb_state_pm.c
 * Keeps system awake during ESB activity (including Fast Path)
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include "esb_driver.h"

#define MODULE esb_state_pm
#include <caf/events/module_state_event.h>
#include <caf/events/keep_alive_event.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_INF);

/* How long to stay awake after last packet (ms) */
#define KEEP_ALIVE_TIMEOUT_MS 1000 * CONFIG_CAF_POWER_MANAGER_TIMEOUT

static struct k_work_delayable keep_alive_work;

/* * This worker checks if we sent data recently.
 * If yes, it sends a Keep Alive event to the system PM.
 */
static void keep_alive_handler(struct k_work *work)
{
	uint32_t last_active = esb_get_last_activity();
	uint32_t now = k_uptime_seconds();

	if ((now - last_active) < CONFIG_CAF_POWER_MANAGER_TIMEOUT) {
		/* We are active (gaming), tell CAF to stay awake */
		keep_alive();

		/* Check again soon */
		k_work_schedule(&keep_alive_work, K_MSEC(500));
	} else {
		/* Idle. Stop checking until someone wakes us. */
		/* Note: motion_sensor triggers wake-up via interrupts, so this is safe */
	}
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);
		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			k_work_init_delayable(&keep_alive_work, keep_alive_handler);
			/* Start monitoring */
			k_work_schedule(&keep_alive_work, K_MSEC(100));
		}
	}

	/* * Optimization: You could subscribe to motion_state_event here
	 * to start/stop the worker immediately
	 */

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);