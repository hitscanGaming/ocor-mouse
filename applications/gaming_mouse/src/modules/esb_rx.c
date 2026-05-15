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
#include <zephyr/kernel.h>

#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif
#define MODULE esb_rx

#include <caf/events/module_state_event.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

#include "hid_report_desc.h"
#include "config_channel_transport.h"
#include "esb_pairing_def.h"
#include "esb_pairing.h"
// #include "hid_event.h" // Removed to eliminate copy 1 and use direct slab buffer
#include "esb_event.h"
#include "esb_driver.h"

enum state {
	STATE_DISABLED,
	STATE_ACTIVE_IDLE,
	STATE_ACTIVE,
	STATE_SUSPENDED,
	STATE_PAIRING
};
static struct k_spinlock lock;
static enum state state;

static struct k_work_delayable connection_timeout_work;
static bool is_connected = false;
#define CONNECTION_TIMEOUT_MS 5000

static void connection_timeout_handler(struct k_work *work)
{
	if (is_connected) {
		is_connected = false;
		if (state != STATE_PAIRING) {
			struct esb_state_event *event = new_esb_state_event();
			event->state = ESB_STATE_DISCONNECTED;
			APP_EVENT_SUBMIT(event);
		}
		LOG_INF("ESB Disconnected (Timeout)");
	}
}

/* --- ZERO-COPY OPTIMIZATION: EXTERN DEFINITIONS --- */
// These are defined in spi_master.c
#define REPORT_DATA_SIZE                                                                           \
	(REPORT_ID_SIZE + REPORT_SIZE_MOUSE) // 6 bytes of actual data to send over SPI
// #define REPORT_ID_DUAL_MOUSE  0x20 // Now in hid_report_desc.h
#define REPORT_DATA_DUAL_SIZE 14
extern struct k_mem_slab tx_slab;
extern struct k_msgq slab_ptr_msgq;
#define REPORT_ID_CONFIG_POLL 0xF0

static esb_driver_rx_cb_t payload_cb;

void esb_driver_register_rx_cb(esb_driver_rx_cb_t cb)
{
	payload_cb = cb;
}

static struct esb_payload rx_payload;
static struct esb_payload tx_payload =
	ESB_CREATE_PAYLOAD(0, 0x01, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);

#include "esb_pairing_def.h"

#define REPORT_ID_SIZE	    1
#define REPORT_TYPE_INPUT   0x01
#define REPORT_TYPE_OUTPUT  0x02
#define REPORT_TYPE_FEATURE 0x03

/** ESB HID Class Boot protocol code */
#define HID_PROTOCOL_BOOT   0
/** ESB HID Class Report protocol code */
#define HID_PROTOCOL_REPORT 1

/* Pairing Data managed by pairing_manager */
static struct k_work pairing_work;
static struct k_work_delayable pairing_timeout_work;

/* Helper to set ESB address from pairing info */
static void apply_pairing_info(void)
{
	int err = 0;
	pairing_config_t *config = pairing_get_config();
	uint8_t *base_addr;
	uint8_t prefix;
	uint8_t channel;

	if (config->valid) {
		base_addr = config->base_addr;
		prefix = config->prefix;
		channel = config->channel;
		LOG_INF("Applying Paired Settings: Ch %d", channel);
	} else {
		static uint8_t default_base[] = DEFAULT_ADDR_BASE;
		base_addr = default_base;
		prefix = DEFAULT_PREFIX;
		channel = DEFAULT_CHANNEL;
		LOG_INF("Applying Default Settings: Ch %d", channel);
	}

	/* Set Pipe 1 (Data) address */
	err = esb_set_base_address_1(base_addr);
	if (err) {
		LOG_ERR("failed to set esb base address1. err: %d", err);
	}

	/* Update prefix for Pipe 1 */
	uint8_t prefixes[8];
	prefixes[0] = BROADCAST_PREFIX;
	prefixes[1] = prefix;

	/* Fill others with dummy */
	for (int i = 2; i < 8; i++) {
		prefixes[i] = 0xC0 + i;
	}

	err = esb_set_prefixes(prefixes, 8);
	if (err) {
		LOG_ERR("failed to set esb prefixes. err: %d", err);
	}

	/* Set Channel */
	err = esb_set_rf_channel(channel);
	if (err) {
		LOG_ERR("failed to set esb rf channel. err: %d", err);
	}
}

static void pairing_timeout_handler(struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	if (state != STATE_PAIRING) {
		k_spin_unlock(&lock, key);
		return;
	}
	state = STATE_ACTIVE;
	k_spin_unlock(&lock, key);

	LOG_WRN("Pairing timeout - no mouse found, reverting to normal mode");

	esb_stop_rx();
	apply_pairing_info();
	esb_start_rx();

	/* LED indication: off or normal */
	struct esb_state_event *event = new_esb_state_event();
	event->state = ESB_STATE_DISCONNECTED;
	APP_EVENT_SUBMIT(event);
}

static void pairing_work_handler(struct k_work *work)
{
	LOG_INF("Saving pairing info and switching address");

	pairing_save_config();

	k_msleep(100); /* Wait for ACK to flush */
	/* Stop RX before changing configuration (required by ESB API) */
	esb_stop_rx();

	apply_pairing_info();

	/* Restart RX in new configuration */
	esb_start_rx();

	state = STATE_ACTIVE;
}

void enter_pairing_mode(void)
{
	/* Switch to Broadcast Address on Pipe 0 (Already configured in init?) */
	/* We need to listen on discovery channel */

	esb_stop_rx();

	int err = esb_set_rf_channel(DISCOVERY_CHANNEL);
	if (err) {
		LOG_ERR("failed to set esb rf channel. err: %d", err);
	}

	esb_start_rx();

	state = STATE_PAIRING;
	LOG_INF("Entered Pairing Mode");

	k_work_reschedule(&pairing_timeout_work, K_MSEC(PAIRING_TIMEOUT_MS));

	/* LED indication */
	struct esb_state_event *event = new_esb_state_event();
	event->state = ESB_STATE_PAIRING; // Need to define this in esb_event.h or reuse
	APP_EVENT_SUBMIT(event);
}

// k_msgq is no longer used for data, zero-copy method uses k_mem_slab and pointer msgq
// static uint8_t report_buffer[6 + 1] = {0x01,0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
// K_MSGQ_DEFINE(mouse_msgq, 6+1, 1024, 4);

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY	  K_PRIO_COOP(7)
static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

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

#elif defined(CONFIG_CLOCK_CONTROL_NRF2)

int clocks_start(void)
{
	int err;
	int res;
	const struct device *radio_clk_dev =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client radio_cli;

	/** Keep radio domain powered all the time to reduce latency. */
	nrf_lrcconf_poweron_force_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_1, true);

	sys_notify_init_spinwait(&radio_cli.notify);

	err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);

	do {
		err = sys_notify_fetch_result(&radio_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err == -EAGAIN);

	nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
	nrf_lrcconf_task_trigger(NRF_LRCCONF000, NRF_LRCCONF_TASK_CLKSTART_0);

	LOG_DBG("HF clock started");
	return 0;
}

#else
BUILD_ASSERT(false, "No Clock Control driver");
#endif /* defined(CONFIG_CLOCK_CONTROL_NRF2) */

static void esb_wakeup(void)
{
	int err = 0;

	// err = esb_wakeup_request();

	if (!err) {
		LOG_INF("ESB wakeup requested");
	} else if (err == -EAGAIN) {
		/* Already woken up - waiting for host */
		LOG_WRN("ESB wakeup pending");
	} else if (err == -EACCES) {
		LOG_INF("ESB wakeup was not enabled by the host");
	} else {
		LOG_ERR("ESB wakeup request failed (err:%d)", err);
	}
}

/**
 * @brief Queues data to be sent back to the ESB PTX via ACK payload.
 * * Called by spi_master.c to forward data received from SPI.
 */
int esb_write(uint8_t *data, size_t size)
{
	// struct esb_payload tx_payload;

	// Send on Pipe 1 (Private Data Channel).
	// This ensures the data goes to the paired device when it next polls this pipe.
	// tx_payload.pipe = 0;

	if (size > CONFIG_ESB_MAX_PAYLOAD_LENGTH) {
		LOG_ERR("Data size exceeds maximum payload length");
		return -EINVAL;
	}

	tx_payload.length = size;
	// tx_payload.noack = false;
	// tx_payload.rssi = 0;
	if (pairing_get_config()->valid) {
		tx_payload.pipe = 1;
	} else {
		tx_payload.pipe = 0;
	}

	memcpy(tx_payload.data, data, size);

	// add retry
	int retry_count = 0;
	int max_retries = 5;
	int retry_delay_ms = 1;

	while (retry_count < max_retries) {
		int err = esb_write_payload(&tx_payload);

		if (!err) {
			return 0; // succeed
		}
		if (err == -EAGAIN) {
			k_sleep(K_MSEC(retry_delay_ms));
			retry_count++;
			retry_delay_ms *= 2; // exponential backoff
		} else if (err == -ENOMEM) {
			LOG_WRN("ESB buffer full, flushing TX");
			esb_flush_tx();
			k_sleep(K_MSEC(2));
			retry_count++;
		} else {
			LOG_ERR("Failed to write payload to ESB (%d)", err);
			return err;
		}
	}

	LOG_ERR("Failed to write payload after %d retries", max_retries);
	return -EAGAIN;
}

/**
 * @brief Sends a HID report by copying the data into a slab block and passing the pointer.
 *
 * This function implements the zero-copy optimization for the ESB to SPI data path.
 * 1. Allocates a block from the shared memory slab (tx_slab).
 * 2. Copies the payload data (skipping the Report ID) into the slab block. (Unavoidable Copy 1)
 * 3. Sends the pointer to the slab block via the slab_ptr_msgq. (Copies 2 & 3 eliminated)
 */
static void send_hid_report(uint8_t *payload_data, size_t payload_size)
{
	if (payload_size < 1) {
		LOG_WRN("Invalid HID report received");
		return;
	}

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

	// The original code was:
	// struct hid_report_event *event = new_hid_report_event(payload_size);
	// memcpy(event->dyndata.data, payload_data, payload_size); // *** COPY 1 ***
	// APP_EVENT_SUBMIT(event);

	// --- Optimized Zero-Copy Replacement ---

	void *slab_block_ptr;
	int err = k_mem_slab_alloc(&tx_slab, &slab_block_ptr, K_NO_WAIT);

	if (err) {
		LOG_WRN("TX slab full, dropping ESB packet");
		return;
	}

	// The report data starts at index 1 (after the 1-byte Report ID)
	// size_t data_len = payload_size - REPORT_ID_SIZE;

	// Only copy up to the defined size (6 bytes)
	// if (data_len > REPORT_DATA_SIZE) {
	// 	data_len = REPORT_DATA_SIZE;
	// }

	// Perform the single unavoidable copy from the ESB buffer to the slab block
	memset(slab_block_ptr, 0, REPORT_DATA_DUAL_SIZE);
	memcpy(slab_block_ptr, &payload_data[0], payload_size);

	// Pass the slab block pointer (address) to the SPI thread via the message queue
	// This eliminates the buffer copies associated with data-based message queues.
	err = k_msgq_put(&slab_ptr_msgq, &slab_block_ptr, K_NO_WAIT);

	if (err) {
		// If pointer queue is full, free the allocated slab block
		LOG_WRN("Pointer MSGQ full, freeing slab block");
		k_mem_slab_free(&tx_slab, slab_block_ptr);
	}
}

uint32_t rx_packet_counter = 0;
uint32_t rx_packet_length = 0;

static void timer0_handler(struct k_timer *dummy)
{
	LOG_INF("Packets received: %d pts %d kbps %d bytes", rx_packet_counter,
		rx_packet_counter * rx_payload.length * 8 / 1000, rx_payload.length);
	rx_packet_counter = 0;
}

void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		LOG_INF("TX SUCCESS EVENT");
		break;

	case ESB_EVENT_TX_FAILED:
		LOG_INF("TX FAILED EVENT");
		break;

	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			/* Pairing Request Handling */
			if (rx_payload.length == sizeof(struct esb_pairing_req) &&
			    rx_payload.data[0] == ESB_PKT_PAIR_REQ) {
				LOG_INF("Pairing Req Received");

				if (state != STATE_PAIRING) {
					LOG_WRN("not in pairing state");
					continue;
				}

				struct esb_pairing_req *req =
					(struct esb_pairing_req *)rx_payload.data;

				if (!esb_pairing_is_valid_req(req)) {
					LOG_WRN("Invalid Pairing Request");
					continue;
				}

				/* Generate Response */
				struct esb_pairing_rsp rsp;
				rsp.type = ESB_PKT_PAIR_RSP;

				/* Generate new address/channel */
				uint8_t full_addr[5];
				esb_pairing_gen_address(full_addr);
				memcpy(rsp.new_base, full_addr, 4);
				rsp.new_prefix = full_addr[4];
				rsp.new_channel = PAIRED_CHANNEL;

				LOG_INF("Generated Pairing Info: Addr %02X:%02X:%02X:%02X:%02X Ch "
					"%d",
					full_addr[0], full_addr[1], full_addr[2], full_addr[3],
					full_addr[4], rsp.new_channel);

				/* Send Response */
				/* We need to write this back to the pipe we received on (Pipe 0)
				 * with ACK */
				/* But ESB ACK payloads are pre-loaded.
				   So we must load the ACK payload for the NEXT packet?
				   Or does ESB allow immediate ACK payload?
				   nRF ESB: esb_write_payload accounts for ACK if it's PRX.
				*/

				struct esb_payload ack_payload;
				ack_payload.length = sizeof(rsp);
				ack_payload.pipe = 0; // Broadcast pipe
				memcpy(ack_payload.data, &rsp, sizeof(rsp));

				esb_write_payload(&ack_payload); // Load into TX FIFO for ACK

				LOG_INF("Pairing RSP queued");

				/* Populate pairing info */
				pairing_config_t config;
				memcpy(config.base_addr, rsp.new_base, 4);
				config.prefix = rsp.new_prefix;
				config.channel = rsp.new_channel;
				config.valid = true;
				pairing_set_config(&config);

				/* Cancel timeout */
				k_work_cancel_delayable(&pairing_timeout_work);

				/* Update state immediately to prevent processing retransmissions */
				state = STATE_ACTIVE;

				/* Switch address delayed to allow ACK to go out */
				k_work_submit(&pairing_work);

				continue;
			}

			if (rx_payload.length == 1 && rx_payload.data[0] == REPORT_ID_CONFIG_POLL) {
				LOG_INF("Config poll received");
				continue;
			}
			if (rx_payload.length == REPORT_DATA_SIZE &&
			    rx_payload.data[0] == REPORT_ID_MOUSE) {
				// LOG_HEXDUMP_INF(rx_payload.data, rx_payload.length, "Mouse
				// Report:");
				send_hid_report(rx_payload.data, rx_payload.length);
				continue;
			}
			if (rx_payload.length == REPORT_DATA_DUAL_SIZE &&
			    rx_payload.data[0] == REPORT_ID_DUAL_MOUSE) {
				send_hid_report(rx_payload.data, rx_payload.length);
				continue;
			}
			if (rx_payload.length == REPORT_SIZE_USER_CONFIG + REPORT_ID_SIZE &&
			    rx_payload.data[0] == REPORT_ID_USER_CONFIG) {
				LOG_HEXDUMP_INF(rx_payload.data, rx_payload.length,
						"Config Report:");
				send_hid_report(rx_payload.data, rx_payload.length);
				continue;
			}
			if (rx_payload.length > 0) {
				// LOG_ERR("LENGTH=%d", rx_payload.length);
				rx_packet_length = rx_payload.length;
				rx_packet_counter++;
			}
		}

		if (!is_connected) {
			is_connected = true;
			if (state != STATE_PAIRING) {
				struct esb_state_event *event = new_esb_state_event();
				event->state = ESB_STATE_ACTIVE;
				APP_EVENT_SUBMIT(event);
			}
			LOG_INF("ESB Connected");
		}
		k_work_reschedule(&connection_timeout_work, K_MSEC(CONNECTION_TIMEOUT_MS));
		break;
	}
}

int esb_initialize(void)
{
	int err;
	/* These are arbitrary default addresses. In end user products
	 * different addresses should be used for each set of devices.
	 */
	/* Pipe 0 = Broadcast "Doorbell" (Fixed) */
	uint8_t base_addr_0[4] = BROADCAST_ADDR_BASE; // <--- MODIFIED

	/* Pipe 1 = Private Data (Default or Loaded from Flash) */
	uint8_t base_addr_1[4] = DEFAULT_ADDR_BASE;

	/* Prefixes: Pipe 0 uses Broadcast Prefix, Pipe 1 uses Data Prefix */
	uint8_t addr_prefix[8] = {
		BROADCAST_PREFIX, DEFAULT_PREFIX, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

	struct esb_config config = ESB_DEFAULT_CONFIG;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.retransmit_delay = 250;
	config.retransmit_count = 0;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.event_handler = event_handler;
	config.mode = ESB_MODE_PRX;
	config.selective_auto_ack = true;
	config.use_fast_ramp_up = true;

	if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);

	if (err) {
		return err;
	}

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

	err = esb_set_rf_channel(DEFAULT_CHANNEL);
	if (err) {
		return err;
	}

	return 0;
}

static K_TIMER_DEFINE(timer0, timer0_handler, NULL);

static int init(void)
{
	int err;

	LOG_INF("Enhanced ShockBurst ptx sample");

	err = clocks_start();
	if (err) {
		return 0;
	}

	err = pairing_manager_init();
	if (err) {
		LOG_WRN("ESB pairing init failed: %d", err);
	}

	k_work_init(&pairing_work, pairing_work_handler);

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return err;
	}

	// k_timer_start(&timer0, K_NO_WAIT, K_MSEC(1000));

	LOG_INF("Initialization complete");

	if (pairing_get_config()->valid) {
		apply_pairing_info();
	}

	err = esb_start_rx();
	if (err) {
		LOG_ERR("RX setup failed, err %d", err);
		return 0;
	}

	k_work_init_delayable(&connection_timeout_work, connection_timeout_handler);
	k_work_init_delayable(&pairing_timeout_work, pairing_timeout_handler);

	return 0;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			int err = init();

			if (!err) {
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}

			// k_thread_create(&thread, thread_stack,
			// 		THREAD_STACK_SIZE,
			// 		(k_thread_entry_t)thread_fn,
			// 		NULL, NULL, NULL,
			// 		THREAD_PRIORITY, 0, K_NO_WAIT);
			// k_thread_name_set(&thread, MODULE_NAME "_thread");
			return false;
		}

		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);