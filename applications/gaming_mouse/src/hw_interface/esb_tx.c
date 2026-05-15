/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#if defined(NRF54L15_XXAA)
#include <hal/nrf_clock.h>
#endif /* defined(NRF54L15_XXAA) */
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <nrf.h>
#include <esb.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif
#define MODULE esb_tx

#include <caf/events/module_state_event.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_INF);

#include "hid_report_desc.h"
#include "config_channel_transport.h"
#include "esb_pairing_def.h"
#include "esb_pairing.h"
#include "hwid.h"
#include "hid_event.h"
#include "esb_event.h"
#include "config_event.h"
#include "usb_event.h"

#include <caf/events/power_event.h>
#include <caf/events/click_event.h>
#include CONFIG_CAF_CLICK_DETECTOR_DEF_PATH
#define REPORT_ID_CONFIG_POLL 0xF0
#define POLL_INTERVAL_FAST    K_MSEC(1000)
#define POLL_INTERVAL_SLOW    K_MSEC(1000)
#define POLL_FAST_TIMEOUT     1000 // 1 second in ms
static int64_t last_config_activity_time = 0;

/* Global atomic flag to control traffic priority */
atomic_t g_config_tx_pending = ATOMIC_INIT(0);

static struct k_work config_set_report_work;
static struct k_work_q config_set_report_work_q;

static struct k_work config_get_report_work;
static struct k_work_q config_get_report_work_q;
struct esb_config_channel_packet {
	uint8_t size;
	uint8_t data[REPORT_SIZE_USER_CONFIG + 1];
};
K_MSGQ_DEFINE(config_msgq, sizeof(struct esb_config_channel_packet), 4, 4);

K_MUTEX_DEFINE(esb_lock);
static struct esb_payload rx_payload;
// static struct esb_payload tx_payload;
static struct esb_payload tx_payload =
	ESB_CREATE_PAYLOAD(0, 0x01, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
// static bool ready = true;
static bool is_usb_active = false; // New state variable for mutual exclusion
static K_MUTEX_DEFINE(radio_mutex);

#define _RADIO_SHORTS_COMMON                                                                       \
	(RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |                             \
	 RADIO_SHORTS_ADDRESS_RSSISTART_Msk | RADIO_SHORTS_DISABLED_RSSISTOP_Msk)

enum {
	ESB_HID_BUF_ALLOCATED = BIT(0),
	ESB_HID_BUF_SENDING = BIT(1),
	ESB_HID_BUF_BOOT_REPORT_FORMAT = BIT(2),
};

#define REPORT_ID_SIZE	    1
#define REPORT_TYPE_INPUT   0x01
#define REPORT_TYPE_OUTPUT  0x02
#define REPORT_TYPE_FEATURE 0x03

/** ESB HID Class Boot protocol code */
#define HID_PROTOCOL_BOOT   0
/** ESB HID Class Report protocol code */
#define HID_PROTOCOL_REPORT 1

#define ESB_SUBSCRIBER_PRIORITY	     CONFIG_DESKTOP_ESB_SUBSCRIBER_REPORT_PRIORITY
#define ESB_SUBSCRIBER_PIPELINE_SIZE (IS_ENABLED(CONFIG_DESKTOP_ESB_HID_REPORT_SENT_ON_SOF) ? 2 : 1)
#define ESB_SUBSCRIBER_REPORT_MAX    ESB_SUBSCRIBER_PIPELINE_SIZE

#define _ESB_HID_BUF_ALIGN (sizeof(void *))
#define _ESB_HID_BUF_SIZE                                                                          \
	(ROUND_UP(REPORT_ID_SIZE + REPORT_BUFFER_SIZE_INPUT_REPORT, sizeof(void *)))

K_MSGQ_DEFINE(mouse_msgq, REPORT_SIZE_MOUSE + 1, 16, 4);
enum state {
	STATE_DISABLED,
	STATE_ACTIVE_IDLE,
	STATE_ACTIVE,
	STATE_SUSPENDED
};
static struct k_spinlock lock;
static enum state state;

struct esb_hid_buf {
	uint8_t data[_ESB_HID_BUF_SIZE];
	uint8_t size;
	uint8_t status_bm;
} __aligned(_ESB_HID_BUF_ALIGN);

struct esb_hid_device {
	const struct device *dev;
	uint32_t report_bm; // hid report id
	struct esb_hid_buf report_bufs[ESB_SUBSCRIBER_REPORT_MAX];
	atomic_ptr_t report_sent_on_sof;
	uint32_t idle_duration[REPORT_ID_COUNT];
	uint8_t hid_protocol;
	bool report_enabled[REPORT_ID_COUNT];
	bool enabled;
};

static struct config_channel_transport cfg_chan_transport;
#define THREAD_STACK_SIZE 1024
// #define THREAD_PRIORITY	 K_PRIO_PREEMPT(1)
#define THREAD_PRIORITY	  K_PRIO_COOP(7)
static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

static struct esb_hid_device esb_hid_device[CONFIG_ESB_HID_DEVICE_COUNT];

static uint32_t get_report_bm(size_t hid_id)
{
	BUILD_ASSERT(REPORT_ID_COUNT <= sizeof(esb_hid_device[0].report_bm) * CHAR_BIT);
#if CONFIG_DESKTOP_USB_SELECTIVE_REPORT_SUBSCRIPTION
	BUILD_ASSERT(ARRAY_SIZE(esb_hid_report_bm) == ARRAY_SIZE(esb_hid_device));
	return esb_hid_report_bm[hid_id];
#else
	return UINT32_MAX;
#endif
}

static struct k_work_delayable wakeup_work;
static struct k_work_delayable suspend_work;
static struct k_work_delayable config_poll_work;

static int esb_init_hid_device_init(struct esb_hid_device *esb_hid_dev, const struct device *dev,
				    uint32_t report_bm)
{
	esb_hid_dev->dev = dev;
	esb_hid_dev->hid_protocol = HID_PROTOCOL_REPORT;
	esb_hid_dev->report_bm = report_bm;
	return 0;
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		LOG_ERR("Clock request failed: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err);

#if defined(NRF54L15_XXAA)
	/* MLTPAN-20 */
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif /* defined(NRF54L15_XXAA) */

	LOG_DBG("HF clock started");
	return 0;
}

int clocks_stop(void)
{
	int err;
	struct onoff_manager *clk_mgr;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	err = onoff_release(clk_mgr);
	if (err < 0) {
		LOG_ERR("Clock release failed: %d", err);
		return err;
	}
#if defined(NRF54L15_XXAA)
	/* MLTPAN-20 */
	// nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
	BUILD_ASSERT(false, "Clock stop not implemented for NRF54L15_XXAA");
#endif /* defined(NRF54L15_XXAA) */
	LOG_DBG("HF clock stopped");
	return 0;
}

#elif defined(CONFIG_CLOCK_CONTROL_NRF2)

int clocks_stop(void)
{
	BUILD_ASSERT(false, "Clock stop not implemented for CONFIG_CLOCK_CONTROL_NRF2");
	// int err;
	// int res;
	// const struct device *radio_clk_dev =
	// 	DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	// struct onoff_client radio_cli;

	// /** Keep radio domain powered all the time to reduce latency. */
	// nrf_lrcconf_poweron_force_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_1, true);

	// sys_notify_init_spinwait(&radio_cli.notify);

	// err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);

	// do {
	// 	err = sys_notify_fetch_result(&radio_cli.notify, &res);
	// 	if (!err && res) {
	// 		LOG_ERR("Clock could not be started: %d", res);
	// 		return res;
	// 	}
	// } while (err == -EAGAIN);

	// nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
	// nrf_lrcconf_task_trigger(NRF_LRCCONF000, NRF_LRCCONF_TASK_CLKSTART_0);

	// LOG_DBG("HF clock started");
	// return 0;
}

#else
BUILD_ASSERT(false, "No Clock Control driver");
#endif /* defined(CONFIG_CLOCK_CONTROL_NRF2) */

static void report_sent(struct esb_hid_device *esb_hid, struct esb_hid_buf *buf, bool error);

static uint8_t esb_hid_buf_get_report_id(struct esb_hid_buf *buf)
{
	__ASSERT_NO_MSG(buf->status_bm & ESB_HID_BUF_ALLOCATED);
	uint8_t report_id = REPORT_ID_COUNT;

	if (!(buf->status_bm & ESB_HID_BUF_BOOT_REPORT_FORMAT)) {
		report_id = buf->data[0];
	} else {
		if (IS_ENABLED(CONFIG_DESKTOP_HID_BOOT_INTERFACE_MOUSE)) {
			report_id = REPORT_ID_BOOT_MOUSE;
		} else if (IS_ENABLED(CONFIG_DESKTOP_HID_BOOT_INTERFACE_KEYBOARD)) {
			report_id = REPORT_ID_BOOT_KEYBOARD;
		}
	}

	/* Should not happen. */
	__ASSERT_NO_MSG(report_id != REPORT_ID_COUNT);

	return report_id;
}

static struct esb_hid_buf *esb_hid_buf_alloc(struct esb_hid_device *esb_hid, const uint8_t *data,
					     size_t size)
{
	for (size_t i = 0; i < ARRAY_SIZE(esb_hid->report_bufs); i++) {
		struct esb_hid_buf *r = &esb_hid->report_bufs[i];

		if (r->status_bm == 0) {
			__ASSERT_NO_MSG(sizeof(r->data) >= size);

			memcpy(r->data, data, size);
			// LOG_WRN("COPY buf");
			r->size = size;
			r->status_bm |= ESB_HID_BUF_ALLOCATED;
			return r;
		}
	}

	return NULL;
}

static void esb_hid_buf_free(struct esb_hid_buf *report_buf)
{
	report_buf->status_bm = 0;
}

static struct esb_hid_buf *esb_hid_buf_find(struct esb_hid_device *esb_hid, uint8_t status_bm)
{
	for (size_t i = 0; i < ARRAY_SIZE(esb_hid->report_bufs); i++) {
		struct esb_hid_buf *r = &esb_hid->report_bufs[i];

		if ((status_bm & r->status_bm) == status_bm) {
			return r;
		}
	}

	return NULL;
}

static bool is_hid_boot_report(uint8_t report_id)
{
	return ((report_id == REPORT_ID_BOOT_MOUSE) || (report_id == REPORT_ID_BOOT_KEYBOARD));
}

static bool is_hid_boot_report_supported(uint8_t report_id)
{
	__ASSERT_NO_MSG(is_hid_boot_report(report_id));

	return ((IS_ENABLED(CONFIG_DESKTOP_HID_BOOT_INTERFACE_MOUSE) &&
		 (report_id == REPORT_ID_BOOT_MOUSE)) ||
		(IS_ENABLED(CONFIG_DESKTOP_HID_BOOT_INTERFACE_KEYBOARD) &&
		 (report_id == REPORT_ID_BOOT_KEYBOARD)));
}

static bool can_send_hid_report(struct esb_hid_device *esb_hid, uint8_t report_id)
{
	// Check the global USB-active state for mutual exclusion
	if (is_usb_active) {
		return false;
	}

	if (!esb_hid->enabled) {
		LOG_WRN("Cannot send report: ESB not connected");
		return false;
	}

	if (esb_hid->hid_protocol == HID_PROTOCOL_BOOT) {
		if (!is_hid_boot_report(report_id)) {
			LOG_WRN("Cannot send report: incompatible boot/report mode");
			return false;
		}

		if (!is_hid_boot_report_supported(report_id)) {
			LOG_WRN("Cannot send report: unsupported boot report");
			return false;
		}
	} else {
		__ASSERT_NO_MSG(esb_hid->hid_protocol == HID_PROTOCOL_REPORT);

		if (is_hid_boot_report(report_id)) {
			LOG_WRN("Cannot send report: incompatible boot/report mode");
			return false;
		}
	}

	return true;
}

void print_buffer(uint8_t *buf, size_t len)
{
	// LOG_HEXDUMP_INF(buf, len, "ESB TX:");
	// LOG_INF("%s", title);
	LOG_INF("%02x %02x %02x %02x %02x %02x %02x", buf[0], buf[1], buf[2], buf[3], buf[4],
		buf[5], buf[6]);
}

void init_esb_payload_config()
{
	tx_payload.noack = true;
}

static int configure_esb_addresses(void)
{
	int err;
	uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}

	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (err) {
		return err;
	}

	return 0;
}

int esb_write(uint8_t *data, size_t size, bool ack_required)
{
	k_mutex_lock(&esb_lock, K_FOREVER);

	// static struct esb_payload tx_payload;
	if (size > CONFIG_ESB_MAX_PAYLOAD_LENGTH) {
		k_mutex_unlock(&esb_lock);
		LOG_ERR("Data size exceeds maximum payload length");
		return -EINVAL;
	}
	// esb_flush_tx();
	// print_buffer(data, size);
	struct esb_payload local_tx_payload = tx_payload;
	local_tx_payload.length = size;
	local_tx_payload.noack = !ack_required;

	memcpy(local_tx_payload.data, data, size);

	int err = esb_write_payload(&local_tx_payload);
	if (err == -ENOMEM) {
		LOG_WRN("TX FIFO full, flushing oldest to make room");
		esb_flush_tx();

		/* Retry sending the CURRENT packet (Latest is Greatest for mouse) */
		err = esb_write_payload(&local_tx_payload);
	}
	if (err) {
		LOG_ERR("Failed to write payload to ESB (%d)", err);
		k_mutex_unlock(&esb_lock);
		return err;
	}
	k_mutex_unlock(&esb_lock);
	return 0;
}

int esb_write_safe(uint8_t *data, size_t size, bool ack_required)
{
	struct esb_payload local_tx_payload = tx_payload;
	// tx_payload.pipe = 0; // Assuming pipe 0 for simplicity
	// tx_payload.length = size;
	// tx_payload.rssi = 0; // RSSI is not used in this context
	// tx_payload.noack = 0; // Assuming ACK is required
	// tx_payload.pid = 0; // PID is not used in this context
	if (size > CONFIG_ESB_MAX_PAYLOAD_LENGTH) {
		LOG_ERR("Data size exceeds maximum payload length");
		return -EINVAL;
	}
	// print_buffer(data, size);
	// return 0;
	// esb_flush_tx();
	local_tx_payload.length = size;

	memcpy(local_tx_payload.data, data, size);
	local_tx_payload.noack = !ack_required;
	// Lock Radio Access
	k_mutex_lock(&radio_mutex, K_FOREVER);
	int err = esb_write_payload(&local_tx_payload);
	if (err) {
		LOG_ERR("Failed to write payload to ESB (%d)", err);
	}
	// if (ack_required) {
	// 	// Wait for the transmission to complete
	// 	if (k_sem_take(&sem, K_MSEC(1000)) != 0) {
	// 		LOG_ERR("Timeout waiting for ESB transmission");
	// 		return -ETIMEDOUT;
	// 	}
	// } else {
	// 	// LOG_INF("No ACK required, proceeding without waiting");
	// }
	k_mutex_unlock(&radio_mutex);
	return err;
}

static void esb_hid_buf_send(struct esb_hid_device *esb_hid, struct esb_hid_buf *buf)
{
	uint8_t report_id = esb_hid_buf_get_report_id(buf);

	buf->status_bm |= ESB_HID_BUF_SENDING;

	if (!can_send_hid_report(esb_hid, report_id)) {
		report_sent(esb_hid, buf, true);
		return;
	}

	uint8_t *data = buf->data;
	size_t size = buf->size;

	if (is_hid_boot_report(report_id)) {
		/* Omit report ID for HID boot reports. Keep proper memory alignment. */
		for (size_t i = 1; i < size; i++) {
			data[i - 1] = data[i];
		}
		size--;
		buf->status_bm |= ESB_HID_BUF_BOOT_REPORT_FORMAT;
	}

	// int err;

	// if (IS_ENABLED(CONFIG_DESKTOP_ESB_STACK_NEXT)) {
	// 	err = hid_device_submit_report(esb_hid->dev, size, data);
	// } else {
	// 	__ASSERT_NO_MSG(IS_ENABLED(CONFIG_DESKTOP_ESB_STACK_LEGACY));
	// 	err = hid_int_ep_write(esb_hid->dev, data, size, NULL);
	// }
	// err = esb_write_payload(&tx_payload);

	// static int32_t count = 0;
	// static int32_t start_time;
	// count++;
	// if(count == 1000){

	// 	int32_t now = k_uptime_get();
	// 	int32_t elapsed_seconds = (now - start_time);
	// 	if (elapsed_seconds > 0) {
	// 	LOG_ERR("receive %u data with %u ms", count, elapsed_seconds);
	// 	count = 0;
	// 	}
	// } else if (count == 1){
	// 	start_time = k_uptime_get();
	// }
	int ret = k_msgq_put(&mouse_msgq, data, K_NO_WAIT);
	// int ret = esb_write(data, size);
	if (ret == 0) {
		report_sent(esb_hid, buf, false);
	} else {
		report_sent(esb_hid, buf, true);
		if (ret == -ENOMSG) {
			LOG_WRN("ESB MSGQ full, dropping HID report");
		} else {
			LOG_ERR("Should not reach here ERR: (%d)", ret);
		}
	}
}

#define ESB_INTERVALS K_USEC(125)

static void report_sent(struct esb_hid_device *esb_hid, struct esb_hid_buf *buf, bool error)
{
	/* Ensure that the function is executed in a cooperative thread context and no extra
	 * synchronization is required.
	 */
	// this will execute in esb event isq
	// __ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(!k_is_preempt_thread());

	__ASSERT_NO_MSG(buf);
	__ASSERT_NO_MSG(buf->status_bm & ESB_HID_BUF_ALLOCATED);
	__ASSERT_NO_MSG(buf->status_bm & ESB_HID_BUF_SENDING);

	uint8_t report_id = esb_hid_buf_get_report_id(buf);
	struct hid_report_sent_event *event = new_hid_report_sent_event();

	event->report_id = report_id;
	event->subscriber = esb_hid;
	event->error = error;

	APP_EVENT_SUBMIT(event);

	esb_hid_buf_free(buf);

	/* Module uses very simple HID report buffering implementation that supports up to 2
	 * buffers. Configuring more buffers could break order of sent HID reports.
	 */
	BUILD_ASSERT(ARRAY_SIZE(esb_hid->report_bufs) <= 2);
	/* Make sure no report is currently being sent. */
	__ASSERT_NO_MSG(!esb_hid_buf_find(esb_hid, ESB_HID_BUF_SENDING));

	/* Send subsequent HID report if queued. */
	struct esb_hid_buf *next_buf = esb_hid_buf_find(esb_hid, ESB_HID_BUF_ALLOCATED);

	if (next_buf) {
		esb_hid_buf_send(esb_hid, next_buf);
	}
}

static struct esb_hid_device *subscriber_to_esb_hid(const void *subscriber)
{
	for (size_t i = 0; i < ARRAY_SIZE(esb_hid_device); i++) {
		if (subscriber == &esb_hid_device[i]) {
			return &esb_hid_device[i];
		}
	}

	return NULL;
}

static bool handle_hid_report_event(struct hid_report_event *event)
{
	/* Ensure that the function is executed in a cooperative thread context and no extra
	 * synchronization is required.
	 */
	__ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(!k_is_preempt_thread());

	struct esb_hid_device *esb_hid = subscriber_to_esb_hid(event->subscriber);

	if (!esb_hid) {
		/* Not us. */
		return false;
	}

	const uint8_t *data = event->dyndata.data;
	size_t size = event->dyndata.size;

	// find an avalilable buffer for sending
	struct esb_hid_buf *sending_buf = esb_hid_buf_find(esb_hid, ESB_HID_BUF_SENDING);
	struct esb_hid_buf *new_buf = esb_hid_buf_alloc(esb_hid, data, size);

	__ASSERT_NO_MSG(new_buf);

	/* Send HID report instantly only if there is no report that is currently being sent.
	 * Otherwise wait until the previous report is sent.
	 */
	if (!sending_buf) {
		esb_hid_buf_send(esb_hid, new_buf);
	} else {
		__ASSERT_NO_MSG(sending_buf->status_bm & ESB_HID_BUF_ALLOCATED);
	}

	return false;
}

// static void broadcast_esb_state(enum esb_state broadcast_state)
// {
// 	struct esb_state_event *event = new_esb_state_event();

// 	event->state = broadcast_state;

// 	APP_EVENT_SUBMIT(event);
// }

static void broadcast_subscriber_change(struct esb_hid_device *esb_hid)
{
	struct hid_report_subscriber_event *event = new_hid_report_subscriber_event();

	event->subscriber = esb_hid;
	event->params.pipeline_size = ESB_SUBSCRIBER_PIPELINE_SIZE;
	event->params.priority = ESB_SUBSCRIBER_PRIORITY;
	event->params.report_max = ESB_SUBSCRIBER_REPORT_MAX;
	event->connected = esb_hid->enabled;

	APP_EVENT_SUBMIT(event);
}

static void broadcast_subscription_change(struct esb_hid_device *esb_hid)
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

static void update_esb_hid(struct esb_hid_device *esb_hid, bool enabled)
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
	if (enabled && is_usb_active) {
		LOG_WRN("Attempted to enable ESB when USB is active. Ignoring.");
		return;
	}

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

static int esb_init_hids_init(void)
{
	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(esb_hid_device); i++) {
		err = esb_init_hid_device_init(&esb_hid_device[i], NULL, get_report_bm(i));
		if (err) {
			LOG_ERR("esb_init_hid_device_init failed for %zu (err: %d)", i, err);
			return err;
		}
	}

	return err;
}

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

static int get_report(void)
{
	int err = 0;

	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE)) {
		struct esb_config_channel_packet esb_config_packet;
		esb_config_packet.data[0] = REPORT_ID_USER_CONFIG;
		esb_config_packet.size = REPORT_SIZE_USER_CONFIG + 1;
		err = config_channel_transport_get(&cfg_chan_transport, &esb_config_packet.data[1],
						   REPORT_SIZE_USER_CONFIG);
		if (err) {
			LOG_ERR("Failed to get config transport: %d", err);
		} else {
			if (!is_usb_active) {
				err = esb_write(esb_config_packet.data, esb_config_packet.size,
						false);
				if (err) {
					LOG_ERR("Failed to write config report to ESB: %d", err);
				}
			}
		}
	}
	return err;
}

static void config_set_report_work_handler(struct k_work *work)
{
	struct esb_config_channel_packet esb_config_packet;
	while (k_msgq_get(&config_msgq, &esb_config_packet, K_NO_WAIT) == 0) {
		/* Process the config request in thread context where mutexes are allowed */
		LOG_INF("Processing deferred config request");

		/* Update activity timestamp and reschedule for FAST polling */
		last_config_activity_time = k_uptime_get();
		if (!is_usb_active) {
			// k_work_reschedule(&config_poll_work, POLL_INTERVAL_FAST);
		}
		LOG_HEXDUMP_INF(esb_config_packet.data, esb_config_packet.size, "config_msgq:");

		uint8_t report_id = esb_config_packet.data[0];
		uint8_t size = esb_config_packet.size - 1;
		uint8_t *buf = &esb_config_packet.data[1];

		int err = set_report(report_id, buf, size);
		if (err) {
			LOG_WRN("Failed to set config transport: %d", err);
		}
	}
}

static void config_get_report_work_handler(struct k_work *work)
{
	int err = 0;

	/* 1. Raise Red Light: Stop Motion Data */
	atomic_set(&g_config_tx_pending, 1);

	/* 2. Wait for the ESB FIFO to drain (wait ~2 motion intervals) */
	k_busy_wait(300);

	/* 3. Flush FIFO to be safe (optional, but ensures slot availability) */
	// esb_flush_tx();
	/* * Logic moved from get_report().
	 * Note: We don't return 'err' here because void functions can't return values,
	 * but we log errors as before.
	 */
	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE)) {
		struct esb_config_channel_packet esb_config_packet;
		esb_config_packet.data[0] = REPORT_ID_USER_CONFIG;
		esb_config_packet.size = REPORT_SIZE_USER_CONFIG + 1;

		err = config_channel_transport_get(&cfg_chan_transport, &esb_config_packet.data[1],
						   REPORT_SIZE_USER_CONFIG);

		if (err) {
			LOG_ERR("Failed to get config transport: %d", err);
		} else {
			/* Check global state (ensure variable scope is accessible here) */
			if (!is_usb_active) {
				// Retry loop to guarantee delivery
				LOG_HEXDUMP_INF(esb_config_packet.data, esb_config_packet.size,
						"get report:");
				for (int i = 0; i < 5; i++) {
					int err = esb_write(esb_config_packet.data,
							    esb_config_packet.size, true);
					if (err == 0) {
						break;
					}
					k_busy_wait(100);
					LOG_WRN("Retrying ESB config report send (%d)", i + 1);
				}
			} else {
				LOG_WRN("USB active, skipping ESB config report send");
			}
		}
	}

	atomic_set(&g_config_tx_pending, 0);
}

void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		// LOG_INF("TX SUCCESS EVENT");
		break;
	case ESB_EVENT_TX_FAILED:
		LOG_WRN("TX FAILED EVENT");
		/* 1. CRITICAL: Flush the stuck packet.
		 * If you don't flush, the radio might stall or the dead packet
		 * might sit in the FIFO blocking fresh motion data.
		 */
		esb_flush_tx();

		/* 2. CRITICAL: Release the "Red Light"
		 * If the config packet failed, we must let motion data resume.
		 * The WebHID on the PC side will detect the timeout (missing response)
		 * and send the request again automatically.
		 */
		atomic_set(&g_config_tx_pending, 0);
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			/* Handle Config Channel Requests (Write) */
			if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE) &&
			    rx_payload.data[0] == REPORT_ID_USER_CONFIG &&
			    rx_payload.length == 1 + REPORT_SIZE_USER_CONFIG) {
				LOG_INF("Received config request (ISR), deferring...");
				LOG_HEXDUMP_INF(rx_payload.data, rx_payload.length,
						"Config Payload:");
				// continue;

				/* Offload to work queue because config_channel_transport_set uses
				 * mutexes */
				struct esb_config_channel_packet config_packet;
				config_packet.size = rx_payload.length;
				memcpy(config_packet.data, rx_payload.data, rx_payload.length);
				if (k_msgq_put(&config_msgq, &config_packet, K_NO_WAIT) == 0) {
					k_work_submit(&config_set_report_work);
				} else {
					LOG_WRN("Config MsgQ full, dropping config packet");
				}
				continue;
			}
			/* ---------------------------------- */

			LOG_INF("Packet received, len %d", rx_payload.length);
		}
		break;
	}
}

static void suspend_esb()
{
	LOG_INF("disable ESB");
	int err = 0;

	// k_work_cancel_delayable(&config_poll_work);

	do {
		err = esb_suspend();
		if (err == -EBUSY) {
			k_sleep(K_MSEC(1));
		}
	} while (err == -EBUSY);
	if (err) {
		LOG_ERR("esb suspend failed (err:%d)", err);
	}
}

static void wakeup_esb()
{
	LOG_INF("wake up ESB");
	int err = 0;
	do {
		err = esb_start_tx();
		if (err == -EBUSY) {
			k_sleep(K_MSEC(1));
		}
	} while (err == -EBUSY);
	if (err == -ENODATA) {
		err = 0;
	}
	if (err) {
		LOG_ERR("ESB wake up failed (err:%d)", err);
	}

	/* Start periodic config polling if USB is not active */
	// if (!is_usb_active) {
	//     k_work_schedule(&config_poll_work, K_MSEC(200));
	// }
}

/* Polling Work Handler */
static void config_poll_work_handler(struct k_work *work)
{
	LOG_WRN("ESB Config Polling");
	/* Only poll if USB is not active and not in pairing mode */
	if (!is_usb_active) {
		uint8_t poll_pkt[1] = {REPORT_ID_CONFIG_POLL};
		/* Must set ack_required = true so the RX can piggyback data */
		int err = esb_write(poll_pkt, sizeof(poll_pkt), true);
		if (err) {
			LOG_ERR("Config poll failed (err:%d)", err);
		}
	}

	/* Reschedule */
	if (!is_usb_active) {
		k_timeout_t next_delay = POLL_INTERVAL_SLOW;

		/* Check if we are within the timeout window of the last config activity */
		if ((k_uptime_get() - last_config_activity_time) < POLL_FAST_TIMEOUT) {
			next_delay = POLL_INTERVAL_FAST;
		}

		k_work_schedule(&config_poll_work, next_delay);
	}
}

static int esb_initialize(void)
{
	int err;
	/* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */

	struct esb_config config = ESB_DEFAULT_CONFIG;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.retransmit_delay = 600;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.event_handler = event_handler;
	config.mode = ESB_MODE_PTX;
	config.retransmit_count = 0;
	config.selective_auto_ack = true;
	config.use_fast_ramp_up = true;

	if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);
	if (err) {
		return err;
	}

	err = configure_esb_addresses();
	if (err) {
		return err;
	}
	// uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	// uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	// uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};
	// err = esb_set_base_address_0(base_addr_0);
	// if (err) {return err;}
	// err = esb_set_base_address_1(base_addr_1);
	// if (err) { return err;}
	// err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	// if (err) { return err;}

	// err = esb_set_rf_channel(82);
	// if (err) {
	// 	return err;
	// }
	return 0;
}

// static int my_coop_work_q_init(void)
// {
//     k_work_queue_start(&config_set_report_work_q, config_set_report_work_q_stack,
//                        K_THREAD_STACK_SIZEOF(config_set_report_work_q),
//                        COOP_WORK_Q_PRIORITY, NULL);

//     /* Initialize the work item at runtime */
//     k_work_init(&config_set_report_work, config_set_report_work_handler);

//     return 0;
// }

static int init(void)
{
	int err;

	LOG_INF("ESB TX start to init");

	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE)) {
		config_channel_transport_init(&cfg_chan_transport);
	}

	err = clocks_start();
	if (err) {
		return 0;
	}

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return err;
	}

	init_esb_payload_config();

	err = esb_init_hids_init();
	if (err) {
		LOG_ERR("ESB hid init failed, err %d", err);
		return err;
	}

	// for (size_t i = 0; i < ARRAY_SIZE(esb_hid_device); i++) {
	// 	update_esb_hid(&esb_hid_device[i], true);
	// }

	// k_timer_start(&timer0, K_MSEC(125), K_MSEC(125));
	k_work_init_delayable(&wakeup_work, wakeup_esb);
	k_work_init_delayable(&suspend_work, suspend_esb);
	// k_work_init_delayable(&config_poll_work, config_poll_work_handler);
	k_work_init(&config_set_report_work, config_set_report_work_handler); // Init config work
	// my_coop_work_q_init();
	k_work_init(&config_get_report_work,
		    config_get_report_work_handler); // Init config get report work
	// #ifdef POLLING_MODE_WITH_COUNTER
	// 	err = init_counter();
	// 	if (err) {
	// 		LOG_ERR("failed to init counter, err: %u", err);
	// 	}
	// #endif
	LOG_INF("Initialization complete");

	return 0;
}

// New event handler for USB state changes
static bool handle_usb_state_event(const struct usb_state_event *event)
{
	/* Ensure that the function is executed in a cooperative thread context and no extra
	 * synchronization is required.
	 */
	__ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(!k_is_preempt_thread());

	// USB is considered active if it's connected or configured
	bool new_usb_active =
		(event->state == USB_STATE_POWERED) || (event->state == USB_STATE_ACTIVE);

	// Check if the state has genuinely changed to avoid redundant work
	if (is_usb_active == new_usb_active) {
		return false;
	}

	is_usb_active = new_usb_active;

	if (is_usb_active) {
		/* USB connected: Disable ESB */
		LOG_INF("USB connected/active. Disabling ESB reports.");

		// k_work_cancel_delayable(&config_poll_work);

		if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE)) {
			config_channel_transport_disconnect(&cfg_chan_transport);
		}
		// 1. Disable ESB reporting subscriptions (stops reports coming from motion_sensor
		// to ESB)
		for (size_t i = 0; i < ARRAY_SIZE(esb_hid_device); i++) {
			update_esb_hid(&esb_hid_device[i], false);
		}

		// 2. Suspend ESB hardware/thread
		k_work_cancel_delayable(&wakeup_work);
		k_work_reschedule(&suspend_work, K_NO_WAIT);

	} else {
		/* USB disconnected: Enable ESB */
		LOG_INF("USB disconnected. Enabling ESB reports.");

		// 1. Wake up ESB hardware/thread
		k_work_cancel_delayable(&suspend_work);
		k_work_reschedule(&wakeup_work, K_NO_WAIT);

		// 2. Enable ESB reporting subscriptions (must happen AFTER wakeup)
		for (size_t i = 0; i < ARRAY_SIZE(esb_hid_device); i++) {
			update_esb_hid(&esb_hid_device[i], true);
		}
	}

	return false;
}

static void thread_fn(void)
{
	static uint8_t report_data[REPORT_SIZE_MOUSE + 1];
	// static uint8_t motion_data[REPORT_SIZE_MOUSE + 1];
	while (true) {
		// LOG_INF("Waiting for mouse report msg...");
		if (!k_msgq_get(&mouse_msgq, &report_data, K_FOREVER)) {
			// k_sem_take(&sem, K_FOREVER);
			// if (!k_msgq_get(&motion_msgq, &motion_data, K_FOREVER)) {
			// continue;
			int err = 0;

			// Final check for mutual exclusion before writing
			if (!is_usb_active) {
				err = esb_write(report_data, REPORT_SIZE_MOUSE + 1, false);
			} else {
				// If USB became active while message was in queue, drop it.
				continue;
			}

			// err = esb_write(motion_data, REPORT_SIZE_MOUSE + 1);

			// static int32_t count = 0;
			// static int32_t start_time;
			// count++;
			// if(count == 1000){

			// 	int32_t now = k_uptime_get();
			// 	int32_t elapsed_seconds = (now - start_time);
			// 	if (elapsed_seconds > 0) {
			// 	LOG_ERR("tx %u data with %u ms", count, elapsed_seconds);
			// 	count = 0;
			// 	}
			// } else if (count == 1){
			// 	start_time = k_uptime_get();
			// }
			// k_sleep(K_USEC(10));

			if (err) {
				LOG_ERR("Failed to write ESB payload stack (%d)", err);
			}
		}
		// static uint8_t report1[REPORT_SIZE_MOUSE + 1] = {0x00, 0x00, 0x00, 0x00, 0x00,
		// 0xff, 0xff}; // Example mouse report
	}
	module_set_state(MODULE_STATE_ERROR);
}

static bool click_event_handler(const struct click_event *event)
{
	if (event->key_id != COMBO_KEY_ID) {
		return false;
	}

	/* Trigger Pairing on Long Click */
	if (event->click == CLICK_LONG) {
		// start_pairing();
		return false;
	}

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_click_event(aeh)) {
		return click_event_handler(cast_click_event(aeh));
	}

	if (is_hid_report_event(aeh)) {
		// return false;
		return handle_hid_report_event(cast_hid_report_event(aeh));
	}

	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			int err = init();

			if (!err) {
				k_spinlock_key_t key = k_spin_lock(&lock);

				state = STATE_ACTIVE;

				k_spin_unlock(&lock, key);
			}

			if (!err) {
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}

			k_thread_create(&thread, thread_stack, THREAD_STACK_SIZE,
					(k_thread_entry_t)thread_fn, NULL, NULL, NULL,
					THREAD_PRIORITY, 0, K_NO_WAIT);
			k_thread_name_set(&thread, MODULE_NAME "_thread");

			// Fix for missing initial USB event: If USB is not active when main is
			// ready, activate ESB.
			if (!is_usb_active) {
				LOG_INF("USB status unknown/disconnected at startup. Defaulting to "
					"ESB active.");
				k_work_reschedule(&wakeup_work, K_NO_WAIT);
				for (size_t i = 0; i < ARRAY_SIZE(esb_hid_device); i++) {
					update_esb_hid(&esb_hid_device[i], true);
				}
			}
			return false;
		}

		return false;
	}

	if (is_wake_up_event(aeh)) {
		int err;

		k_spinlock_key_t key = k_spin_lock(&lock);

		/* CRITICAL FIX: Only attempt ESB wake-up if USB is NOT active,
		 * ensuring mutual exclusion is respected during system wake-up.
		 * If USB is active, we rely on the USB event handler to manage ESB state.
		 */
		if (is_usb_active) {
			k_spin_unlock(&lock, key);
			return false;
		}

		switch (state) {
		case STATE_SUSPENDED:
			state = STATE_ACTIVE;
			// err = wakeup_esb();
			k_work_cancel_delayable(&suspend_work);
			k_work_reschedule(&wakeup_work, K_NO_WAIT);
			if (!err) {
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
			break;

		case STATE_ACTIVE:
		case STATE_ACTIVE_IDLE:
			/* No action */
			break;

		case STATE_DISABLED:
		default:
			__ASSERT_NO_MSG(false);
			break;
		}

		k_spin_unlock(&lock, key);

		return false;
	}

	if (is_power_down_event(aeh)) {
		int err;

		k_spinlock_key_t key = k_spin_lock(&lock);
		switch (state) {
		case STATE_ACTIVE:
			state = STATE_SUSPENDED;
			k_work_cancel_delayable(&wakeup_work);
			k_work_reschedule(&suspend_work, K_NO_WAIT);

			if (!err) {
				module_set_state(MODULE_STATE_STANDBY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
			break;

		case STATE_SUSPENDED:
			/* No action */
			break;

		case STATE_DISABLED:
		default:
			__ASSERT_NO_MSG(false);
			break;
		}

		k_spin_unlock(&lock, key);

		return false;
	}

	// Handle USB state changes for mutual exclusion (Requirement 1)
	if (is_usb_state_event(aeh)) {
		return handle_usb_state_event(cast_usb_state_event(aeh));
	}

	if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE) && is_config_event(aeh)) {
		struct config_event *event = cast_config_event(aeh);

		/* Process the event to see if it is a response intended for us */
		if (config_channel_transport_rsp_receive(&cfg_chan_transport, event)) {
			/* If true, the transport buffer has been filled with the response.
			 * We now need to send this data back to the host via ESB.
			 */
			LOG_ERR("Config event for us, sending response via ESB");

			/* Retrieve the data from transport (length is tracked internally) */
			k_work_submit(&config_get_report_work);

		} else {
			LOG_WRN("Config event not for us");
		}
		return false;
	}
	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_report_event);
APP_EVENT_SUBSCRIBE(MODULE, usb_state_event);
APP_EVENT_SUBSCRIBE(MODULE, click_event);
#if CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE
APP_EVENT_SUBSCRIBE(MODULE, config_event);
#endif