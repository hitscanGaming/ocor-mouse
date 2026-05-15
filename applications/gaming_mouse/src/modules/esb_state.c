/*
 * esb_state.c
 *
 * Implements: REQ-RADIO-001, REQ-RADIO-002
 * (2.4 GHz ESB pairing state machine + auto-reconnect on power-up)
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <errno.h>

#include "hid_event.h"
#include "config_event.h"
#include "usb_event.h"
#include "esb_driver.h"
#include "config_channel_transport.h"
#include "esb_pairing.h"
#include "esb_pairing_def.h"
#include "hid_report_desc.h"
#include "hid_report_user_config.h"
#include "hwid.h"
#include "esb_event.h"
#include <caf/events/click_event.h>
#include CONFIG_CAF_CLICK_DETECTOR_DEF_PATH

#define MODULE esb_state
#include <caf/events/module_state_event.h>
#include <caf/events/power_event.h>

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_INF);

static struct config_channel_transport cfg_chan_transport;
static bool esb_active = false;
static bool usb_active = false;

static struct esb_hid_device_t esb_hid_device;

/* Buffer to hold RX data for deferred processing */

static struct k_work config_set_report_work;
static struct k_work config_get_report_work;

static struct k_work_delayable wakeup_work;
static struct k_work_delayable suspend_work;

struct esb_config_channel_packet {
	uint8_t size;
	uint8_t data[REPORT_SIZE_USER_CONFIG + 1];
};

static struct esb_config_channel_packet esb_config_packet;

/* Pairing deferred work */
static struct k_work pairing_rsp_work;
static struct esb_pairing_rsp pending_rsp;
static struct k_work_delayable pairing_req_work;
static struct k_work_delayable pairing_timeout_work;

#define PAIRING_REQ_INTERVAL_MS 100

static int set_report(uint8_t report_id, const uint8_t *buf, size_t size)
{
	int err = -ENOTSUP;
	LOG_HEXDUMP_INF(buf, size, "set_report:");

	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE) &&
	    (report_id == REPORT_ID_USER_CONFIG) && (size == REPORT_SIZE_USER_CONFIG)) {
		err = config_channel_transport_set(&cfg_chan_transport, buf, size);
		if (err) {
			LOG_WRN("Failed to set config transport: %d", err);
		}
	}

	return err;
}

/* Step 2: Process configuration channel (Thread Context) */
static void config_set_report_handler(struct k_work *work)
{
	uint8_t report_id = esb_config_packet.data[0];
	uint8_t size = esb_config_packet.size - 1;
	uint8_t *buf = &esb_config_packet.data[1];

	int err = set_report(report_id, buf, size);
	if (err) {
		LOG_WRN("Failed to set config transport: %d", err);
	}
}

/* Step 3: ESB TX sends configuration response data back (Thread Context) */
static void config_get_report_handler(struct k_work *work)
{
	if (!esb_active) {
		return;
	}

	struct esb_config_channel_packet packet;

	packet.data[0] = REPORT_ID_USER_CONFIG;

	int err = config_channel_transport_get(&cfg_chan_transport, &packet.data[1],
					       REPORT_SIZE_USER_CONFIG);

	if (!err) {
		packet.size = REPORT_SIZE_USER_CONFIG + 1;

		/* 1. Traffic Light: RED (Stop Motion) */
		esb_set_config_tx_pending(true);

		/* 2. Wait for motion stream to drain (2 intervals @ 8k = 250us) */
		k_busy_wait(300);

		/* 3. Send Config Data with RELIABILITY (ACK required) */
		/* Retry loop to ensure config passes even in interference */
		int retries = 5;
		while (retries--) {
			int tx_err = esb_write_data(packet.data, packet.size, true);
			if (tx_err == 0) {
				break;
			}
			k_busy_wait(100);
		}

		/* 4. Traffic Light: GREEN (Resume Motion) */
		esb_set_config_tx_pending(false);
	}
}

static void pairing_rsp_handler(struct k_work *work)
{
	LOG_INF("Processing pairing response");

	int err = pairing_handle_response((uint8_t *)&pending_rsp, sizeof(pending_rsp));
	if (err) {
		LOG_ERR("Failed to handle pairing response: %d", err);
		return;
	}

	pairing_config_t *config = pairing_get_config();

	/* Apply new address and channel */
	uint8_t addr[5];
	memcpy(addr, config->base_addr, 4);
	addr[4] = config->prefix;
	esb_driver_update_pairing(addr, config->channel);

	struct esb_state_event *event = new_esb_state_event();
	event->state = ESB_STATE_ACTIVE;
	APP_EVENT_SUBMIT(event);

	k_work_cancel_delayable(&pairing_req_work);
	k_work_cancel_delayable(&pairing_timeout_work);
}

static void pairing_timeout_handler(struct k_work *work)
{
	LOG_WRN("Pairing timeout on mouse - reverting address");

	/* Revert address by stopping manager and updating driver */
	pairing_manager_t *manager = pairing_manager_get_instance();
	k_spinlock_key_t key = k_spin_lock(&manager->lock);
	manager->state = PAIRING_TIMEOUT;
	manager->is_pairing_mode = false;
	k_spin_unlock(&manager->lock, key);

	pairing_config_t *config = pairing_get_config();
	if (config->valid) {
		uint8_t addr[5];
		memcpy(addr, config->base_addr, 4);
		addr[4] = config->prefix;
		esb_driver_update_pairing(addr, config->channel);
	} else {
		esb_driver_suspend();
		esb_driver_set_address_default();
		esb_driver_resume();
	}

	k_work_cancel_delayable(&pairing_req_work);

	struct esb_state_event *event = new_esb_state_event();
	event->state = ESB_STATE_DISCONNECTED;
	APP_EVENT_SUBMIT(event);
}

/* The utility handles timeout internally via timeout_work. */

static void pairing_req_handler(struct k_work *work)
{
	pairing_state_t state = pairing_get_state();
	if (state != PAIRING_WAITING_RESPONSE) {
		if (state == PAIRING_TIMEOUT || state == PAIRING_FAILED) {
			/* Revert address */
			pairing_config_t *config = pairing_get_config();
			if (config->valid) {
				uint8_t addr[5];
				memcpy(addr, config->base_addr, 4);
				addr[4] = config->prefix;
				esb_driver_update_pairing(addr, config->channel);
			} else {
				esb_driver_set_address_default();
			}
		}
		return;
	}

	struct esb_pairing_req req;
	req.type = ESB_PKT_PAIR_REQ;

	/* Populate HWID */
	hwid_get(req.hwid, sizeof(req.hwid));
	req.vid = 0x1915; // Placeholder
	req.pid = 0xEEEE; // Placeholder

	/* Send Pairing Request */
	LOG_INF("Sending Pairing Request...");
	esb_write_data((uint8_t *)&req, sizeof(req), true);

	/* Reschedule request */
	k_work_schedule(&pairing_req_work, K_MSEC(PAIRING_REQ_INTERVAL_MS));
}

static void enter_pairing_mode_tx(void)
{
	LOG_INF("Entering Pairing Mode...");

	struct esb_state_event *event = new_esb_state_event();
	event->state = ESB_STATE_PAIRING;
	APP_EVENT_SUBMIT(event);

	/* Switch to Discovery Channel & Broadcast Address */
	uint8_t broadcast_addr[5] = BROADCAST_ADDR_BASE;
	broadcast_addr[4] = BROADCAST_PREFIX;
	esb_driver_update_pairing(broadcast_addr, DISCOVERY_CHANNEL);

	/* Start Pairing Manager */
	hwid_get(pairing_manager_get_instance()->current_hwid, 8); // Ensure it's there
	pairing_start(pairing_manager_get_instance()->current_hwid);

	/* Start Sending Requests */
	k_work_schedule(&pairing_req_work, K_NO_WAIT);
	k_work_reschedule(&pairing_timeout_work, K_MSEC(PAIRING_TIMEOUT_MS));
}

/* Modified RX Data Handler to intercept Pairing Response */
static void on_esb_rx_data(uint8_t *data, size_t size)
{
	/* Filter for Pairing Response */
	if (pairing_get_state() == PAIRING_WAITING_RESPONSE && data[0] == ESB_PKT_PAIR_RSP &&
	    size == sizeof(struct esb_pairing_rsp)) {

		struct esb_pairing_rsp *rsp = (struct esb_pairing_rsp *)data;
		LOG_INF("Pairing RSP Recvd: Ch %d", rsp->new_channel);

		/* Defer processing to work queue to avoid ISR issues */
		memcpy(&pending_rsp, rsp, sizeof(struct esb_pairing_rsp));
		k_work_submit(&pairing_rsp_work);

		return;
	}

	/* Filter for Config Reports */
	if (data[0] == REPORT_ID_USER_CONFIG && size == 1 + REPORT_SIZE_USER_CONFIG) {
		/* Copy to static buffer and schedule work to avoid processing in ISR */
		memcpy(&esb_config_packet.data, data, size);
		esb_config_packet.size = size;
		k_work_submit(&config_set_report_work);
	}
}

static int init(void)
{
	int err = 0;
	err = esb_driver_init();
	if (err) {
		LOG_ERR("ESB driver init failed, err %d", err);
		return err;
	}
	esb_driver_register_rx_cb(on_esb_rx_data);

	/* Initialize Pairing */
	err = pairing_manager_init();
	if (err) {
		LOG_WRN("Pairing init failed: %d", err);
	}

	pairing_config_t *config = pairing_get_config();
	if (config->valid) {
		LOG_INF("Loaded pairing info (Ch %d)", config->channel);
		uint8_t addr[5];
		memcpy(addr, config->base_addr, 4);
		addr[4] = config->prefix;
		esb_driver_update_pairing(addr, config->channel);
	}

	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE)) {
		config_channel_transport_init(&cfg_chan_transport);
	}

	k_work_init(&config_get_report_work, config_get_report_handler);
	k_work_init(&config_set_report_work, config_set_report_handler);
	k_work_init(&pairing_rsp_work, pairing_rsp_handler);
	k_work_init_delayable(&pairing_req_work, pairing_req_handler);
	k_work_init_delayable(&pairing_timeout_work, pairing_timeout_handler);

	k_work_init_delayable(&wakeup_work, esb_driver_resume);
	k_work_init_delayable(&suspend_work, esb_driver_suspend);

	err = esb_init_hids_init(&esb_hid_device);
	if (err) {
		LOG_ERR("ESB hid init failed, err %d", err);
		return err;
	}
	k_work_reschedule(&wakeup_work, K_NO_WAIT);

	if (!usb_active) {
		update_esb_hid(&esb_hid_device, true);
	}

	return 0;
}

static bool click_event_handler(const struct click_event *event)
{
	if (event->key_id == COMBO_KEY_ID && event->click == CLICK_LONG) {
		enter_pairing_mode_tx();
	} else if (event->key_id == 0x00 && event->click == CLICK_LONG) {
		// templorily use key_id 0 for test
		enter_pairing_mode_tx();
	}

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_click_event(aeh)) {
		return click_event_handler(cast_click_event(aeh));
	}
	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE) && is_config_event(aeh)) {
		struct config_event *evt = cast_config_event(aeh);
		/* If the Transport layer has a response ready (e.g. after processing the Set
		 * Report), schedule transmission */
		if (config_channel_transport_rsp_receive(&cfg_chan_transport, evt)) {
			k_work_submit(&config_get_report_work);
		}
		return false;
	}

	if (is_usb_state_event(aeh)) {
		struct usb_state_event *evt = cast_usb_state_event(aeh);
		bool new_usb_active =
			(evt->state == USB_STATE_POWERED) || (evt->state == USB_STATE_ACTIVE);
		// Check if the state has genuinely changed to avoid redundant work
		if (usb_active == new_usb_active) {
			return false;
		}

		if (usb_active && esb_active) {
			LOG_INF("USB Active: Suspending ESB");
			esb_active = false;
			if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE)) {
				config_channel_transport_disconnect(&cfg_chan_transport);
			}
			// 1. Disable ESB reporting subscriptions (stops reports coming from
			// motion_sensor to ESB)
			update_esb_hid(&esb_hid_device, false);

			// 2. Suspend ESB hardware/thread
			k_work_cancel_delayable(&wakeup_work);
			k_work_reschedule(&suspend_work, K_NO_WAIT);
		} else if (!usb_active && !esb_active) {
			LOG_INF("USB Inactive: Resuming ESB");
			esb_active = true;
			esb_driver_resume();
			k_work_cancel_delayable(&suspend_work);
			k_work_reschedule(&wakeup_work, K_NO_WAIT);

			// 2. Enable ESB reporting subscriptions (must happen AFTER wakeup)
			update_esb_hid(&esb_hid_device, true);
		}
		return false;
	}

	if (is_power_down_event(aeh)) {
		if (esb_active) {
			esb_driver_suspend();
			esb_active = false;
		}
		return false;
	}

	if (is_module_state_event(aeh)) {
		struct module_state_event *evt = cast_module_state_event(aeh);
		if (check_state(evt, MODULE_ID(main), MODULE_STATE_READY)) {
			int err = init();

			if (!err) {
				esb_active = true;
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
		}
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, config_event);
APP_EVENT_SUBSCRIBE(MODULE, usb_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, click_event);