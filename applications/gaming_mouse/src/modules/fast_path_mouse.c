/* fast_path_mouse.c */
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <caf/events/button_event.h>
#include <caf/events/module_state_event.h>
#include <caf/key_id.h>

#include "wheel_event.h"
#include "fast_path_mouse.h"
#include "motion_sensor.h"
#include "polling_rate.h"

/* HID Headers to understand the keymap */
#include "hid_keymap.h"
#include "hid_report_desc.h"
#include "esb_driver.h"

/* * INCLUDE THE KEYMAP DEFINITION
 * WARNING: You must ensure 'hid_keymap_def.h' does not define global symbols
 * that conflict with hid_state.c. Make the guard variable in that file 'static'.
 */
#include "hid_keymap_def.h"
#include <stdint.h>

#define MODULE fast_path
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DESKTOP_HID_STATE_LOG_LEVEL);

/* --- 配置：平滑阈值 ---
 * 限制单次无线数据包发送的最大滚轮数值。
 * 建议值：
 * 1: 最平滑，但在极速滚动时会有延迟感（因为要分很多次发）。
 * 3: 平衡点，既能快速泄洪，又不会让 Windows/微信 觉得单次位移太大而跳帧。
 * 127: 无限制（直通模式），响应最快，但可能卡顿。
 */
#define WHEEL_SEND_LIMIT 1

/* Counter to track when to "Pulse" an ACK */
static uint32_t packet_counter = 0;

/* -------------------------------------------------------------------------
 * DYNAMIC KEYMAP STORAGE
 * ------------------------------------------------------------------------- */
#define MAX_FAST_KEYS 8 /* Max buttons supported in fast path */

struct fast_keymap_entry {
	uint16_t key_id;
	uint8_t mask;
};

/* RAM-based lookup table, populated at startup */
static struct fast_keymap_entry fast_keymap[MAX_FAST_KEYS];
static size_t fast_keymap_count = 0;

/* -------------------------------------------------------------------------
 * STATE MANAGEMENT
 * ------------------------------------------------------------------------- */
static atomic_t g_btn_state = ATOMIC_INIT(0);
static atomic_t g_wheel_acc = ATOMIC_INIT(0);

/* 引入 esb_write */
extern int esb_write(uint8_t *data, size_t size, bool ack_required);

/* -------------------------------------------------------------------------
 * HELPER: Send Mouse Report immediately
 * ------------------------------------------------------------------------- */
static void send_mouse_report(int16_t dx, int16_t dy)
{
	/* 1. PRIORITY CHECK: If config is trying to send, DROP this motion packet */
	if (esb_is_config_tx_pending()) {
		return; // Drop packet to let config pass
	}

	uint8_t buttons = (uint8_t)atomic_get(&g_btn_state);
	int8_t wheel_to_send = 0;

	/* * [Modified] 恢复平滑逻辑，但带有限幅 (Clamped Drain)
	 * 使用 CAS 循环安全地从累加器中取出数值，但每次取出的量不超过 WHEEL_SEND_LIMIT。
	 */
	atomic_val_t expected, desired;
	do {
		expected = atomic_get(&g_wheel_acc);

		if (expected == 0) {
			wheel_to_send = 0;
			desired = 0;
			break;
		}

		/* 限制正向最大发送量 */
		if (expected > WHEEL_SEND_LIMIT) {
			wheel_to_send = WHEEL_SEND_LIMIT;
			desired = expected - WHEEL_SEND_LIMIT;
		}
		/* 限制负向最大发送量 */
		else if (expected < -WHEEL_SEND_LIMIT) {
			wheel_to_send = -WHEEL_SEND_LIMIT;
			desired = expected + WHEEL_SEND_LIMIT;
		}
		/* 如果在限制范围内，则一次性全发 */
		else {
			wheel_to_send = (int8_t)expected;
			desired = 0;
		}

		/* 如果在计算间隙 g_wheel_acc 被修改，CAS 失败并重试 */
	} while (!atomic_cas(&g_wheel_acc, expected, desired));

	/* 组装数据包 */
	uint8_t packet[7];
	packet[0] = 0x01;		    /* Report ID (Mouse) */
	packet[1] = buttons;		    /* Button Mask */
	packet[2] = (uint8_t)wheel_to_send; /* Wheel */

	sys_put_le16(dx, &packet[3]);  /* X Axis */
	sys_put_le16(-dy, &packet[5]); /* Y Axis */

	/* 发送 */

	/* 2. Hybrid Polling Logic */
	bool ack_request = false;
	packet_counter++;

	/* User requested ACK interval configurable via Kconfig */
	/* Interval = Rate (Hz) * Config(ms) / 1000 */
	/* e.g. 4000Hz * 100ms / 1000 = 400 packets */
	uint32_t ack_interval =
		(uint64_t)polling_rate_get_hz() * CONFIG_DESKTOP_ESB_TX_ACK_INTERVAL_MS / 1000;

	if (ack_interval == 0) {
		ack_interval =
			1; /* Ensure at least 1 to avoid division by zero or infinite sending */
	}

	if (packet_counter >= ack_interval) {
		ack_request = true; // Request ACK to check for Config Data
		// LOG_ERR("Requesting ACK on packet %u", packet_counter);
		packet_counter = 0; // Reset counter
	}

	/* 3. Send to ESB */
	/* Note: Pass 'ack_request' to your esb_write wrapper */
	int err = esb_write_data(packet, sizeof(packet), ack_request);
	if (err) {
		/* [Critical] 发送失败处理
		 * 必须把取出的值加回去，防止丢包
		 */
		if (wheel_to_send != 0) {
			atomic_add(&g_wheel_acc, wheel_to_send);
			LOG_WRN("TX Failed, restored wheel: %d", wheel_to_send);
		}
	}
}

/* -------------------------------------------------------------------------
 * LOGIC: Initialization
 * ------------------------------------------------------------------------- */
static void init_fast_keymap(void)
{
	fast_keymap_count = 0;
	size_t total_keys = ARRAY_SIZE(hid_keymap);

	LOG_INF("Initializing Fast Path Keymap from definitions...");

	for (size_t i = 0; i < total_keys; i++) {
		if (hid_keymap[i].report_id != REPORT_ID_MOUSE) {
			continue;
		}

		if (fast_keymap_count >= MAX_FAST_KEYS) {
			LOG_WRN("Fast Path Keymap full! Ignored key_id 0x%x", hid_keymap[i].key_id);
			continue;
		}

		uint8_t usage_id = hid_keymap[i].usage_id;

		if (usage_id >= 1 && usage_id <= 8) {
			fast_keymap[fast_keymap_count].key_id = hid_keymap[i].key_id;
			fast_keymap[fast_keymap_count].mask = BIT(usage_id - 1);
			fast_keymap_count++;
		}
	}
}

static void update_button_state(uint16_t key_id, bool pressed)
{
	uint8_t mask = 0;

	for (size_t i = 0; i < fast_keymap_count; i++) {
		if (fast_keymap[i].key_id == key_id) {
			mask = fast_keymap[i].mask;
			break;
		}
	}

	if (mask == 0) {
		return;
	}

	if (pressed) {
		atomic_or(&g_btn_state, mask);
	} else {
		atomic_and(&g_btn_state, ~mask);
	}

	/* 立即发送状态更新 */
	send_mouse_report(0, 0);
}

/* -------------------------------------------------------------------------
 * CALLBACK: Motion Thread Handler
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * CALLBACK: Motion Thread Handler
 * ------------------------------------------------------------------------- */
static uint8_t pending_report[6];
static bool has_pending_report = false;

/* Helper to populate a report buffer (no Report ID) */
static void populate_report_buffer(uint8_t *buf, int16_t dx, int16_t dy)
{
	uint8_t buttons = (uint8_t)atomic_get(&g_btn_state);
	int8_t wheel_to_send = 0;
	atomic_val_t expected, desired;

	/* Wheel Logic (Same as before) */
	do {
		expected = atomic_get(&g_wheel_acc);
		if (expected == 0) {
			wheel_to_send = 0;
			desired = 0;
			break;
		}
		if (expected > WHEEL_SEND_LIMIT) {
			wheel_to_send = WHEEL_SEND_LIMIT;
			desired = expected - WHEEL_SEND_LIMIT;
		} else if (expected < -WHEEL_SEND_LIMIT) {
			wheel_to_send = -WHEEL_SEND_LIMIT;
			desired = expected + WHEEL_SEND_LIMIT;
		} else {
			wheel_to_send = (int8_t)expected;
			desired = 0;
		}
	} while (!atomic_cas(&g_wheel_acc, expected, desired));

	buf[0] = buttons;
	buf[1] = (uint8_t)wheel_to_send;
	sys_put_le16(dx, &buf[2]);
	sys_put_le16(-dy, &buf[4]);
}

static void send_dual_report(uint8_t *r1, uint8_t *r2)
{
	if (esb_is_config_tx_pending()) {
		return;
	}

	/* 14 bytes: ID (1) + R1(6) + R2(6) + Padding(1) */
	uint8_t packet[14];
	packet[0] = REPORT_ID_DUAL_MOUSE;
	memcpy(&packet[1], r1, 6);
	memcpy(&packet[7], r2, 6);
	packet[13] = 0x00;

	packet_counter++;
	bool ack_request = false;
	/* Since we send at 4kHz (every 2 reports), we check interval against 4000 equivalent */
	uint32_t ack_interval = (uint64_t)4000 * CONFIG_DESKTOP_ESB_TX_ACK_INTERVAL_MS / 1000;
	if (ack_interval == 0) {
		ack_interval = 1;
	}

	if (packet_counter >= ack_interval) {
		ack_request = true;
		packet_counter = 0;
	}

	int err = esb_write_data(packet, sizeof(packet), ack_request);
	if (err) {
		/* Restore Wheel Delta if failed?
		 * Complexity: It's hard to distinguish which report failed or both.
		 * For now, we accept the loss or log it.
		 */
		// LOG_WRN("Dual TX Failed");
	}

	/* Diagnostic Log: Print Deltas */
	// int16_t dx1 = sys_get_le16(&packet[2]);
	// int16_t dy1 = sys_get_le16(&packet[4]); // Note: This is -dy encoded
	// int16_t dx2 = sys_get_le16(&packet[8]);
	// int16_t dy2 = sys_get_le16(&packet[10]);
	// LOG_INF("DualTX: R1(%d, %d) R2(%d, %d)", dx1, dy1, dx2, dy2);
	// LOG_HEXDUMP_INF(&packet[1], 6, "Dual R1");
	// LOG_HEXDUMP_INF(&packet[7], 6, "Dual R2");
}

void fast_path_motion_handler(int16_t dx, int16_t dy)
{
	/* SIMULATION OVERRIDE: Uncomment to force constant motion */
	// static int16_t dx = 0;
	// static int16_t dy = 0;
	// ++dx;

	uint32_t current_rate = polling_rate_get_hz();

	/* Fake 8K Logic: Aggregation */
	if (current_rate == 8000) {
		if (!has_pending_report) {
			/* First sample of the pair: Buffer it */
			populate_report_buffer(pending_report, dx, dy);
			has_pending_report = true;
		} else {
			/* Second sample: Aggregate and Send */
			uint8_t second_report[6];
			populate_report_buffer(second_report, dx, dy);

			send_dual_report(pending_report, second_report);
			has_pending_report = false;
		}
	}
	/* Normal Logic (1K, 2K, 4K) */
	else {
		has_pending_report = false; /* Reset if mode switched */
		send_mouse_report(dx, dy);
	}
}

void fast_path_flush_handler(void)
{
	/* If we have a pending 8K half-packet, send it immediately as a standard packet */
	if (has_pending_report) {
		uint8_t packet[7];
		packet[0] = 0x01;		       /* Report ID (Mouse) */
		memcpy(&packet[1], pending_report, 6); /* Copy Buttons, Wheel, X, Y */

		/* Send without ACK request (best effort flush) */
		esb_write_data(packet, sizeof(packet), false);

		has_pending_report = false;
	}
}

/* -------------------------------------------------------------------------
 * EVENT LISTENER
 * ------------------------------------------------------------------------- */
static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_button_event(aeh)) {
		struct button_event *evt = cast_button_event(aeh);
		update_button_state(evt->key_id, evt->pressed);
		return false;
	}

	if (is_wheel_event(aeh)) {
		struct wheel_event *evt = cast_wheel_event(aeh);
		atomic_add(&g_wheel_acc, evt->wheel);

		/* 立即触发发送 */
		send_mouse_report(0, 0);

		return false;
	}

	if (is_module_state_event(aeh)) {
		struct module_state_event *evt = cast_module_state_event(aeh);
		if (check_state(evt, MODULE_ID(main), MODULE_STATE_READY)) {
			init_fast_keymap();
			motion_sensor_set_fast_path_cb(fast_path_motion_handler);
			motion_sensor_set_fast_path_flush_cb(fast_path_flush_handler);
		}
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
APP_EVENT_SUBSCRIBE(MODULE, wheel_event);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);