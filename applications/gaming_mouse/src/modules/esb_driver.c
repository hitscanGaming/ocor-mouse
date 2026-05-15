/*
 * esb_driver.c
 * Implementation of ESB driver.
 */

#include <zephyr/kernel.h>
#include <esb.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include "esb_driver.h"
#include "hid_report_desc.h"
#include "hid_event.h"
#include "esb_pairing_def.h"

LOG_MODULE_REGISTER(esb_driver, LOG_LEVEL_INF);

K_MUTEX_DEFINE(esb_lock);

static struct esb_payload tx_payload = ESB_CREATE_PAYLOAD(0, 0);
static struct esb_payload rx_payload;
static bool is_esb_paired = false;

/* Callback for RX Data (Config Channel) */
static esb_driver_rx_cb_t rx_callback = NULL;

/* PM Hook: timestamp of last activity */
atomic_t last_activity_timestamp;

/* Traffic Light Flag Definition */
atomic_t g_config_tx_pending = ATOMIC_INIT(0);

/****************************** ESB hid *******************************/
#define REPORT_ID_SIZE		1
/** ESB HID Class Boot protocol code */
#define HID_PROTOCOL_BOOT	0
/** ESB HID Class Report protocol code */
#define HID_PROTOCOL_REPORT	1
#define ESB_SUBSCRIBER_PRIORITY 100

#define CONFIG_ESB_HID_DEVICE_COUNT 1

static uint32_t get_report_bm(size_t hid_id)
{
	// 	BUILD_ASSERT(REPORT_ID_COUNT <=
	// 		     sizeof(esb_hid_device[0].report_bm) * CHAR_BIT);
	// #if CONFIG_DESKTOP_USB_SELECTIVE_REPORT_SUBSCRIPTION
	// 	BUILD_ASSERT(ARRAY_SIZE(esb_hid_report_bm) ==
	// 		     ARRAY_SIZE(esb_hid_device));
	// 	return esb_hid_report_bm[hid_id];
	// #else
	return UINT32_MAX;
	// #endif
}

static int esb_init_hid_device_init(struct esb_hid_device_t *esb_hid_dev, const struct device *dev,
				    uint32_t report_bm)
{
	esb_hid_dev->dev = dev;
	esb_hid_dev->hid_protocol = HID_PROTOCOL_REPORT;
	esb_hid_dev->report_bm = report_bm;
	return 0;
}

static void broadcast_subscriber_change(struct esb_hid_device_t *esb_hid)
{
	struct hid_report_subscriber_event *event = new_hid_report_subscriber_event();

	event->subscriber = esb_hid;
	event->params.pipeline_size = ESB_SUBSCRIBER_PIPELINE_SIZE;
	event->params.priority = ESB_SUBSCRIBER_PRIORITY;
	event->params.report_max = ESB_SUBSCRIBER_REPORT_MAX;
	event->connected = esb_hid->enabled;

	APP_EVENT_SUBMIT(event);
}

static void broadcast_subscription_change(struct esb_hid_device_t *esb_hid)
{
	bool new_rep_enabled = (esb_hid->enabled) && (esb_hid->hid_protocol == HID_PROTOCOL_REPORT);
	bool new_boot_enabled = (esb_hid->enabled) && (esb_hid->hid_protocol == HID_PROTOCOL_BOOT);

	LOG_INF("new_rep_enabled: %u, esb_hid->enabled: %u", new_rep_enabled, esb_hid->enabled);
	LOG_INF("esb_hid->report_enabled[REPORT_ID_MOUSE]: %u",
		esb_hid->report_enabled[REPORT_ID_MOUSE]);

	if (IS_ENABLED(CONFIG_DESKTOP_HID_REPORT_MOUSE_SUPPORT) &&
	    (new_rep_enabled != esb_hid->report_enabled[REPORT_ID_MOUSE]) &&
	    (esb_hid->report_bm & BIT(REPORT_ID_MOUSE))) {
		struct hid_report_subscription_event *event = new_hid_report_subscription_event();

		event->report_id = REPORT_ID_MOUSE;
		event->enabled = new_rep_enabled;
		event->subscriber = esb_hid;

		APP_EVENT_SUBMIT(event);

		esb_hid->report_enabled[REPORT_ID_MOUSE] = new_rep_enabled;
		LOG_INF("generate hid report subscription, eanble: %u", new_rep_enabled);
	}
	if (IS_ENABLED(CONFIG_DESKTOP_HID_REPORT_KEYBOARD_SUPPORT) &&
	    (new_rep_enabled != esb_hid->report_enabled[REPORT_ID_KEYBOARD_KEYS]) &&
	    (esb_hid->report_bm & BIT(REPORT_ID_KEYBOARD_KEYS))) {
		struct hid_report_subscription_event *event = new_hid_report_subscription_event();

		event->report_id = REPORT_ID_KEYBOARD_KEYS;
		event->enabled = new_rep_enabled;
		event->subscriber = esb_hid;

		APP_EVENT_SUBMIT(event);

		esb_hid->report_enabled[REPORT_ID_KEYBOARD_KEYS] = new_rep_enabled;
	}
	if (IS_ENABLED(CONFIG_DESKTOP_HID_REPORT_SYSTEM_CTRL_SUPPORT) &&
	    (new_rep_enabled != esb_hid->report_enabled[REPORT_ID_SYSTEM_CTRL]) &&
	    (esb_hid->report_bm & BIT(REPORT_ID_SYSTEM_CTRL))) {
		struct hid_report_subscription_event *event = new_hid_report_subscription_event();

		event->report_id = REPORT_ID_SYSTEM_CTRL;
		event->enabled = new_rep_enabled;
		event->subscriber = esb_hid;

		APP_EVENT_SUBMIT(event);
		esb_hid->report_enabled[REPORT_ID_SYSTEM_CTRL] = new_rep_enabled;
	}
	if (IS_ENABLED(CONFIG_DESKTOP_HID_REPORT_CONSUMER_CTRL_SUPPORT) &&
	    (new_rep_enabled != esb_hid->report_enabled[REPORT_ID_CONSUMER_CTRL]) &&
	    (esb_hid->report_bm & BIT(REPORT_ID_CONSUMER_CTRL))) {
		struct hid_report_subscription_event *event = new_hid_report_subscription_event();

		event->report_id = REPORT_ID_CONSUMER_CTRL;
		event->enabled = new_rep_enabled;
		event->subscriber = esb_hid;

		APP_EVENT_SUBMIT(event);
		esb_hid->report_enabled[REPORT_ID_CONSUMER_CTRL] = new_rep_enabled;
	}
	if (IS_ENABLED(CONFIG_DESKTOP_HID_BOOT_INTERFACE_MOUSE) &&
	    (new_boot_enabled != esb_hid->report_enabled[REPORT_ID_BOOT_MOUSE]) &&
	    (esb_hid->report_bm & BIT(REPORT_ID_BOOT_MOUSE))) {
		struct hid_report_subscription_event *event = new_hid_report_subscription_event();

		event->report_id = REPORT_ID_BOOT_MOUSE;
		event->enabled = new_boot_enabled;
		event->subscriber = esb_hid;

		APP_EVENT_SUBMIT(event);
		esb_hid->report_enabled[REPORT_ID_BOOT_MOUSE] = new_boot_enabled;
	}
	if (IS_ENABLED(CONFIG_DESKTOP_HID_BOOT_INTERFACE_KEYBOARD) &&
	    (new_boot_enabled != esb_hid->report_enabled[REPORT_ID_BOOT_KEYBOARD]) &&
	    (esb_hid->report_bm & BIT(REPORT_ID_BOOT_KEYBOARD))) {
		struct hid_report_subscription_event *event = new_hid_report_subscription_event();

		event->report_id = REPORT_ID_BOOT_KEYBOARD;
		event->enabled = new_boot_enabled;
		event->subscriber = esb_hid;

		APP_EVENT_SUBMIT(event);
		esb_hid->report_enabled[REPORT_ID_BOOT_KEYBOARD] = new_boot_enabled;
	}

	LOG_INF("ESB HID %p %sabled", (void *)esb_hid, (esb_hid->enabled) ? ("en") : ("dis"));
	if (esb_hid->enabled) {
		LOG_INF("%s_PROTOCOL active", esb_hid->hid_protocol ? "REPORT" : "BOOT");
	}
}

void update_esb_hid(struct esb_hid_device_t *esb_hid, bool enabled)
{
	/* Ensure that the function is executed in a cooperative thread context and no extra
	 * synchronization is required.
	 */
	__ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(!k_is_preempt_thread());

	if (esb_hid->enabled == enabled) {
		/* Already updated. */
		return;
	}
	// Apply mutual exclusion logic to ESB's local enabled state
	// if (enabled && is_usb_active) {
	// 	LOG_WRN("Attempted to enable ESB when USB is active. Ignoring.");
	// 	return;
	// }

	esb_hid->enabled = enabled;

	if (esb_hid->enabled) {
		esb_hid->hid_protocol = HID_PROTOCOL_REPORT;
		broadcast_subscriber_change(esb_hid);
	}

	broadcast_subscription_change(esb_hid);

	if (!esb_hid->enabled) {
		broadcast_subscriber_change(esb_hid);
	}
}

int esb_init_hids_init(struct esb_hid_device_t *esb_hid_device)
{
	int err = 0;

	err = esb_init_hid_device_init(esb_hid_device, NULL, get_report_bm(0));
	if (err) {
		LOG_ERR("esb_init_hid_device_init failed for %zu (err: %d)", 0, err);
		return err;
	}

	return err;
}

/*********************************** ESB hid END ***********************************/

/* --- Internal: ISR Handler --- */
void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		/* OPTIONAL: Clear flag here for fastest resume,
		   but safer to do it in logic thread to avoid races */
		break;
	case ESB_EVENT_TX_FAILED:
		/* CRITICAL: Flush FIFO on failure to prevent blocking 4K stream */
		esb_flush_tx();
		/* CRITICAL: Release the "Red Light" if logic thread is stuck waiting */
		atomic_set(&g_config_tx_pending, 0);
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			/* If we have a callback (registered by esb_state), pass the data */
			if (rx_callback) {
				rx_callback(rx_payload.data, rx_payload.length);
			}
		}
		break;
	}
}

/* Interface Implementation */
void esb_set_config_tx_pending(bool pending)
{
	atomic_set(&g_config_tx_pending, pending ? 1 : 0);
}

bool esb_is_config_tx_pending(void)
{
	return (atomic_get(&g_config_tx_pending) == 1);
}

/* --- Internal: Clock Management --- */
static int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);
	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			return res;
		}
	} while (err);

	return 0;
}

int esb_driver_suspend(void)
{
	int err;
	do {
		err = esb_suspend();
		if (err == -EBUSY) {
			k_msleep(100);
		}
	} while (err == -EBUSY);
	if (err) {
		LOG_ERR("esb suspend failed (err:%d)", err);
	}
	return err;
}

int esb_driver_resume(void)
{
	LOG_INF("esb_driver_resume");
	int err = 0;
	do {
		err = esb_start_tx();
		if (err == -EBUSY) {
			k_msleep(100);
		}
	} while (err == -EBUSY);

	if (err == -ENODATA) {
		err = 0;
	}
	if (err) {
		LOG_ERR("ESB wake up failed (err:%d)", err);
	}
	return err;
}

/* --- Public API --- */

static void esb_driver_create_config(struct esb_config *config)
{
	*config = (struct esb_config)ESB_DEFAULT_CONFIG;
	config->protocol = ESB_PROTOCOL_ESB_DPL;
	config->retransmit_delay = 600;
	config->bitrate = ESB_BITRATE_2MBPS;
	config->event_handler = event_handler;
	config->mode = ESB_MODE_PTX;
	config->selective_auto_ack = true;
	config->use_fast_ramp_up = true;
}

int esb_driver_set_address_default(void)
{
	is_esb_paired = false;
	int err;
	uint8_t base_addr_0[4] = BROADCAST_ADDR_BASE;
	uint8_t base_addr_1[4] = DEFAULT_ADDR_BASE;
	uint8_t addr_prefix[8] = {
		BROADCAST_PREFIX, DEFAULT_PREFIX, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}
	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}
	err = esb_set_prefixes(addr_prefix, 8);
	if (err) {
		return err;
	}

	err = esb_set_rf_channel(DEFAULT_CHANNEL);
	if (err) {
		return err;
	}

	return 0;
}

int esb_driver_init(void)
{
	int err;

	err = clocks_start();
	if (err) {
		return err;
	}

	struct esb_config config;
	esb_driver_create_config(&config);

	err = esb_init(&config);
	if (err) {
		return err;
	}

	err = esb_driver_set_address_default();
	if (err) {
		return err;
	}

	return 0;
}

void esb_driver_register_rx_cb(esb_driver_rx_cb_t cb)
{
	rx_callback = cb;
}

int esb_write_data(uint8_t *data, size_t size, bool ack_required)
{
	k_mutex_lock(&esb_lock, K_FOREVER);

	atomic_set(&last_activity_timestamp, k_uptime_seconds());

	tx_payload.length = size;
	tx_payload.noack = !ack_required;
	tx_payload.pipe = is_esb_paired ? 1 : 0;
	memcpy(tx_payload.data, data, size);
	// LOG_HEXDUMP_INF(data, size, "esb write data");
	int err = esb_write_payload(&tx_payload);

	if (err == -ENOMEM) {
		esb_flush_tx();
		err = esb_write_payload(&tx_payload);
	}

	k_mutex_unlock(&esb_lock);
	return err;
}

uint32_t esb_get_last_activity(void)
{
	return atomic_get(&last_activity_timestamp);
}

int esb_driver_update_pairing(const uint8_t *addr, uint8_t channel)
{
	int err;

	esb_driver_suspend();

	uint8_t base_addr[4];
	memcpy(base_addr, addr, 4);

	err = esb_set_base_address_1(base_addr);
	if (err) {
		LOG_ERR("Failed to set base address 1");
		return err;
	}

	uint8_t prefixes[8];
	prefixes[0] = BROADCAST_PREFIX; // Broadcast prefix (preserved for Pipe 0)
	prefixes[1] = addr[4];		// New prefix for Pipe 1
	for (int i = 2; i < 8; i++) {
		prefixes[i] = 0xC0 + i; // Dummy prefixes
	}

	err = esb_set_prefixes(prefixes, 8);
	if (err) {
		LOG_ERR("Failed to set prefixes");
		return err;
	}

	err = esb_set_rf_channel(channel);
	if (err) {
		LOG_ERR("Failed to set channel");
		return err;
	}

	is_esb_paired = true;
	esb_driver_resume();
	return 0;
}