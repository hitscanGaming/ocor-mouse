/*
 * Copyright (c) 2018-2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Implements: REQ-DPI-001, REQ-DPI-002
 * (PAW3395 motion sensor + DPI step/range enforcement + preset cycling)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/settings/settings.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

#include "polling_rate.h"

#include "motion_sensor.h"

#include <app_event_manager.h>
#include "motion_event.h"
#include <caf/events/power_event.h>
#include <caf/events/click_event.h>
#include "hid_event.h"
#include "config_event.h"
#include "usb_event.h"

#define MODULE motion
#include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DESKTOP_MOTION_LOG_LEVEL);

#define THREAD_STACK_SIZE CONFIG_DESKTOP_MOTION_SENSOR_THREAD_STACK_SIZE
#define THREAD_PRIORITY	  K_PRIO_COOP(15)
// #define THREAD_PRIORITY		K_PRIO_PREEMPT(0)

#define NODATA_LIMIT CONFIG_DESKTOP_MOTION_SENSOR_EMPTY_SAMPLES_COUNT

#define MAX_KEY_LEN 20

// #define POLLING_MODE
#define POLLING_INTERVAL       K_USEC(250)
// K_MSGQ_DEFINE(motion_msgq, REPORT_SIZE_MOTION, 1024, 4);
// K_MSGQ_DEFINE(motion_msgq, REPORT_SIZE_MOUSE+1, 1024, 4);
#define CPI_KEY_ID	       5
#define SENSOR_CPI_STAGE_COUNT 4

/* External function from esb_tx.c for direct fast-path transmission */
extern int esb_write(uint8_t *data, size_t size);

// 1. Add static variable for the callback
static motion_fast_path_cb_t fast_path_cb = NULL;
static motion_fast_path_flush_cb_t fast_path_flush_cb = NULL;

// 2. Implement the registration function
void motion_sensor_set_fast_path_cb(motion_fast_path_cb_t cb)
{
	fast_path_cb = cb;
}

void motion_sensor_set_fast_path_flush_cb(motion_fast_path_flush_cb_t cb)
{
	fast_path_flush_cb = cb;
}

enum State {
	STATE_DISABLED,
	STATE_DISABLED_SUSPENDED,
	STATE_DISCONNECTED,
	STATE_IDLE,
	STATE_FETCHING,
	STATE_SUSPENDED,
	STATE_SUSPENDED_DISCONNECTED,
};

struct SensorState {
	struct k_spinlock lock;

	enum State state;
	bool sample;
	uint8_t peer_count;
	uint8_t cpi_stage;
	uint32_t option[MOTION_SENSOR_OPTION_COUNT];
	uint32_t option_mask;
	bool usb_is_connected; /* Flag to block PM when USB is powered */
};

enum sensor_opt {
	SENSOR_OPT_VARIANT,
	SENSOR_OPT_CPI,
	SENSOR_OPT_DOWNSHIFT_RUN,
	SENSOR_OPT_DOWNSHIFT_REST1,
	SENSOR_OPT_DOWNSHIFT_REST2,
	SENSOR_OPT_CPI_STAGE_1,
	SENSOR_OPT_CPI_STAGE_2,
	SENSOR_OPT_CPI_STAGE_3,
	SENSOR_OPT_CPI_STAGE_4,
	SENSOR_OPT_CPI_STAGE_ACTIVE,
	SENSOR_OPT_POLLING_RATE_ESB,
	SENSOR_OPT_POLLING_RATE_USB,
	SENSOR_OPT_RIPPLE_CONTROL,
	SENSOR_OPT_ANGLE_SNAP,
	SENSOR_OPT_LOD,
	SENSOR_OPT_COUNT
};

static K_SEM_DEFINE(sem, 0, 1);
static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

static const struct device *sensor_dev = DEVICE_DT_GET_ONE(MOTION_SENSOR_COMPATIBLE);

static struct SensorState state;

static const char *const opt_descr[] = {
	[SENSOR_OPT_VARIANT] = OPT_DESCR_MODULE_VARIANT,
	[SENSOR_OPT_CPI] = "cpi",
	[SENSOR_OPT_DOWNSHIFT_RUN] = "downshift",
	[SENSOR_OPT_DOWNSHIFT_REST1] = "rest1",
	[SENSOR_OPT_DOWNSHIFT_REST2] = "rest2",
	[SENSOR_OPT_CPI_STAGE_1] = "cpi_stage_1",
	[SENSOR_OPT_CPI_STAGE_2] = "cpi_stage_2",
	[SENSOR_OPT_CPI_STAGE_3] = "cpi_stage_3",
	[SENSOR_OPT_CPI_STAGE_4] = "cpi_stage_4",
	[SENSOR_OPT_CPI_STAGE_ACTIVE] = "cpi_stage",
	[SENSOR_OPT_POLLING_RATE_ESB] = "poll_esb",
	[SENSOR_OPT_POLLING_RATE_USB] = "poll_usb",
	[SENSOR_OPT_RIPPLE_CONTROL] = "ripple_control",
	[SENSOR_OPT_ANGLE_SNAP] = "angle_snap",
	[SENSOR_OPT_LOD] = "lod",
};

static const enum motion_sensor_option motion_sensor_cpi_stage[SENSOR_CPI_STAGE_COUNT] = {
	MOTION_SENSOR_OPTION_CPI_STAGE_1, MOTION_SENSOR_OPTION_CPI_STAGE_2,
	MOTION_SENSOR_OPTION_CPI_STAGE_3, MOTION_SENSOR_OPTION_CPI_STAGE_4};

static enum motion_sensor_option config_opt_id_2_option(uint8_t config_opt_id)
{
	switch (config_opt_id) {
	case SENSOR_OPT_CPI:
		return MOTION_SENSOR_OPTION_CPI;

	case SENSOR_OPT_DOWNSHIFT_RUN:
		return MOTION_SENSOR_OPTION_SLEEP1_TIMEOUT;

	case SENSOR_OPT_DOWNSHIFT_REST1:
		return MOTION_SENSOR_OPTION_SLEEP2_TIMEOUT;

	case SENSOR_OPT_DOWNSHIFT_REST2:
		return MOTION_SENSOR_OPTION_SLEEP3_TIMEOUT;

	case SENSOR_OPT_CPI_STAGE_1:
		return MOTION_SENSOR_OPTION_CPI_STAGE_1;

	case SENSOR_OPT_CPI_STAGE_2:
		return MOTION_SENSOR_OPTION_CPI_STAGE_2;

	case SENSOR_OPT_CPI_STAGE_3:
		return MOTION_SENSOR_OPTION_CPI_STAGE_3;

	case SENSOR_OPT_CPI_STAGE_4:
		return MOTION_SENSOR_OPTION_CPI_STAGE_4;

	case SENSOR_OPT_CPI_STAGE_ACTIVE:
		return MOTION_SENSOR_OPTION_CPI_STAGE_ACTIVE;

	case SENSOR_OPT_POLLING_RATE_ESB:
		return MOTION_SENSOR_OPTION_POLLING_RATE_ESB;

	case SENSOR_OPT_POLLING_RATE_USB:
		return MOTION_SENSOR_OPTION_POLLING_RATE_USB;

	case SENSOR_OPT_RIPPLE_CONTROL:
		return MOTION_SENSOR_OPTION_RIPPLE_CONTROL;

	case SENSOR_OPT_ANGLE_SNAP:
		return MOTION_SENSOR_OPTION_ANGLE_SNAP;

	case SENSOR_OPT_LOD:
		return MOTION_SENSOR_OPTION_LOD;

	default:
		LOG_WRN("Unsupported sensor option (%" PRIu8 ")", config_opt_id);
		return MOTION_SENSOR_OPTION_COUNT;
	}
}

static bool set_option(enum motion_sensor_option option, uint32_t value)
{
	if (option < MOTION_SENSOR_OPTION_COUNT) {
		k_spinlock_key_t key = k_spin_lock(&state.lock);
		state.option[option] = value;
		WRITE_BIT(state.option_mask, option, true);
		k_spin_unlock(&state.lock, key);
		k_sem_give(&sem);
		LOG_ERR("k_sem_give, set option");

		return true;
	}

	LOG_INF("Sensor option %d is not supported", option);

	return false;
}

static int settings_set(const char *key, size_t len_rd, settings_read_cb read_cb, void *cb_arg)
{
	BUILD_ASSERT(SENSOR_OPT_VARIANT == 0);

	for (size_t i = (SENSOR_OPT_VARIANT + 1); i < ARRAY_SIZE(opt_descr); i++) {
		if (!strcmp(key, opt_descr[i])) {
			uint32_t readout;

			BUILD_ASSERT(sizeof(readout) == sizeof(state.option[i]));

			ssize_t len = read_cb(cb_arg, &readout, sizeof(readout));

			if ((len != sizeof(readout)) || (len != len_rd)) {
				LOG_ERR("Can't read option %s from storage", opt_descr[i]);
				return len;
			}

			set_option(config_opt_id_2_option(i), readout);
			break;
		}
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(motion_sensor, MODULE_NAME, NULL, settings_set, NULL, NULL);

static void data_ready_handler(const struct device *dev, const struct sensor_trigger *trig);

static int enable_trigger(void)
{
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ALL,
	};

	int err = sensor_trigger_set(sensor_dev, &trig, data_ready_handler);

	return err;
}

static int disable_trigger(void)
{
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ALL,
	};

	int err = sensor_trigger_set(sensor_dev, &trig, NULL);

	return err;
}

static int motion_read(bool send_event)
{
	struct sensor_value value_x;
	struct sensor_value value_y;
	// LOG_ERR("motion_read");
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

	int err = sensor_sample_fetch(sensor_dev);

	if (!err) {
		err = sensor_channel_get(sensor_dev, SENSOR_CHAN_POS_DX, &value_x);
	}
	if (!err) {
		err = sensor_channel_get(sensor_dev, SENSOR_CHAN_POS_DY, &value_y);
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

	static unsigned int nodata = 0;
	if (!value_x.val1 && !value_y.val1) {
		if (nodata < NODATA_LIMIT) {
			// return 0;
			nodata++;
		} else {
			// LOG_WRN("NODATA_LIMIT %d", NODATA_LIMIT);
			nodata = 0;
			if (fast_path_flush_cb) {
				LOG_WRN("FLUSH");
				fast_path_flush_cb();
			}
			return -ENODATA;
		}
	} else {
		nodata = 0;
	}
	// return 0;

	// if (err || !send_event) {
	// 	return err;
	// }
	// 	static uint32_t count = 0;
	// 	static uint32_t start_time;
	// 	count++;
	// 	if(count == 1000){

	// 		uint32_t now = k_uptime_get();
	// 		uint32_t elapsed_seconds = (now - start_time);
	// 		if (elapsed_seconds > 0) {
	// 		LOG_ERR("receive %u data with %u ms", count, elapsed_seconds);
	// 		count = 0;
	// 		}
	// 	} else if (count == 1){
	// 		start_time = k_uptime_get();
	// 	}
	// 	return 0;
	/* Check for USB Subscriber (indicates USB connected and configured) */
	k_spinlock_key_t key = k_spin_lock(&state.lock);
	bool usb_active = state.usb_is_connected;
	k_spin_unlock(&state.lock, key);
	if (usb_active) {
		// LOG_ERR("USB");
		struct motion_event *event = new_motion_event();

		event->dx = value_x.val1;
		event->dy = value_y.val1;
		APP_EVENT_SUBMIT(event);
	} else {
		// LOG_ERR("ESB");
		// uint16_t dx = value_x.val1;
		// uint16_t dy = value_y.val1;
		// uint8_t x_buff[sizeof(dx)];
		// uint8_t y_buff[sizeof(dy)];
		// sys_put_le16(dx, x_buff);
		// sys_put_le16(-dy, y_buff);
		// uint8_t data[REPORT_SIZE_MOUSE + 1];
		// data[0] = 0x01;
		// data[1] = 0;
		// data[2] = 0;
		// data[3] = x_buff[0];
		// data[4] = x_buff[1] & 0xFF;
		// data[5] = y_buff[0];
		// data[6] = y_buff[1] & 0xFF;

		// /* OPTIMIZATION: Direct call to ESB TX, skipping MsgQ and Event Manager */
		// err = esb_write(data, sizeof(data));
		// if (err != 0) {
		// 	LOG_WRN("ESB TX fail/busy: %d", err);
		// }
		// FAST PATH: Decoupled!
		// We only send raw X/Y. We don't care about packet formats or ESB here.
		// fast_path_motion_handler(value_x.val1, value_y.val1);
		if (fast_path_cb) {
			fast_path_cb((int16_t)value_x.val1, (int16_t)value_y.val1);
		}
	}
	return err;
}

static void set_sampling_time_in_sleep3(bool connected)
{
	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_SAMPLE_TIME_DEFAULT ==
	    CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_SAMPLE_TIME_CONNECTED) {
		return;
	}

	uint32_t sampling_time =
		(connected) ? (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_SAMPLE_TIME_CONNECTED)
			    : (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_SAMPLE_TIME_DEFAULT);

	set_option(MOTION_SENSOR_OPTION_SLEEP3_SAMPLE_TIME, sampling_time);
}

static void set_default_configuration(void)
{
	BUILD_ASSERT((MOTION_SENSOR_OPTION_COUNT < 8 * sizeof(state.option_mask)), "");

	if (CONFIG_DESKTOP_MOTION_SENSOR_CPI) {
		set_option(MOTION_SENSOR_OPTION_CPI, CONFIG_DESKTOP_MOTION_SENSOR_CPI);
	}

	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP1_TIMEOUT_MS) {
		set_option(MOTION_SENSOR_OPTION_SLEEP1_TIMEOUT,
			   CONFIG_DESKTOP_MOTION_SENSOR_SLEEP1_TIMEOUT_MS);
	}

	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP2_TIMEOUT_MS) {
		set_option(MOTION_SENSOR_OPTION_SLEEP2_TIMEOUT,
			   CONFIG_DESKTOP_MOTION_SENSOR_SLEEP2_TIMEOUT_MS);
	}

	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_TIMEOUT_MS) {
		set_option(MOTION_SENSOR_OPTION_SLEEP3_TIMEOUT,
			   CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_TIMEOUT_MS);
	}

	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP1_SAMPLE_TIME_DEFAULT) {
		set_option(MOTION_SENSOR_OPTION_SLEEP1_SAMPLE_TIME,
			   CONFIG_DESKTOP_MOTION_SENSOR_SLEEP1_SAMPLE_TIME_DEFAULT);
	}

	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP2_SAMPLE_TIME_DEFAULT) {
		set_option(MOTION_SENSOR_OPTION_SLEEP2_SAMPLE_TIME,
			   CONFIG_DESKTOP_MOTION_SENSOR_SLEEP2_SAMPLE_TIME_DEFAULT);
	}

	if (CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_SAMPLE_TIME_DEFAULT) {
		set_option(MOTION_SENSOR_OPTION_SLEEP3_SAMPLE_TIME,
			   CONFIG_DESKTOP_MOTION_SENSOR_SLEEP3_SAMPLE_TIME_DEFAULT);
	}

	// Initialize new options default values
	set_option(MOTION_SENSOR_OPTION_POLLING_RATE_USB, 1000); // Default 1000Hz
	set_option(MOTION_SENSOR_OPTION_POLLING_RATE_ESB, 8000); // Default 8000Hz
	set_option(MOTION_SENSOR_OPTION_LOD, 1);		 // Default 1mm
	set_option(MOTION_SENSOR_OPTION_RIPPLE_CONTROL, 0);	 // Default Disable
	set_option(MOTION_SENSOR_OPTION_ANGLE_SNAP, 0);		 // Default Disable

	// Default CPI Stages
	set_option(MOTION_SENSOR_OPTION_CPI_STAGE_1, 400);
	set_option(MOTION_SENSOR_OPTION_CPI_STAGE_2, 1600);
	set_option(MOTION_SENSOR_OPTION_CPI_STAGE_3, 3200);
	set_option(MOTION_SENSOR_OPTION_CPI_STAGE_4, 16000);
	set_option(MOTION_SENSOR_OPTION_CPI_STAGE_ACTIVE, 1);

	set_option(MOTION_SENSOR_OPTION_SLEEP_ENABLE, true);
}

#ifdef POLLING_MODE
static void timer0_handler(struct k_timer *dummy)
{
	/*Interrupt Context - Sysetm Timer ISR */
	k_work_submit(&motion_read_work);
}

static K_TIMER_DEFINE(timer0, timer0_handler, NULL);
#endif

#ifdef CONFIG_POLLING_RATE_ENABLE
#define DEFAULT_POLLING_RATE_HZ 1000

/**
 * @brief Polling timer callback - signals the motion thread
 */
static void polling_callback(void)
{
	k_sem_give(&sem);
}

/**
 * @brief Get the appropriate polling rate based on USB/ESB mode
 */
static uint32_t get_polling_rate_hz(void)
{
	uint32_t rate = 0;
	if (state.usb_is_connected) {
		rate = state.option[MOTION_SENSOR_OPTION_POLLING_RATE_USB];
	} else {
		rate = state.option[MOTION_SENSOR_OPTION_POLLING_RATE_ESB];
	}

	if (rate == 0) {
		rate = DEFAULT_POLLING_RATE_HZ;
	}

	return rate;
}

static int start_polling(void)
{
	uint32_t rate_hz = get_polling_rate_hz();
	polling_rate_set_hz(rate_hz);
	return polling_rate_start();
}

static int stop_polling(void)
{
	return polling_rate_stop();
}

static int init_polling(void)
{
	int err = polling_rate_init();
	if (err) {
		LOG_ERR("Failed to init polling rate module: %d", err);
		return err;
	}
	polling_rate_set_callback(polling_callback);
	return 0;
}
#else
static int start_polling(void)
{
	return 0;
}
static int stop_polling(void)
{
	return 0;
}
static int init_polling(void)
{
	return 0;
}
#endif

static void data_ready_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	k_spinlock_key_t key = k_spin_lock(&state.lock);

	switch (state.state) {
	case STATE_IDLE:
	case STATE_DISCONNECTED:
		/* Switch from Interrupt mode to Polling mode */
		disable_trigger();
		state.state = STATE_FETCHING;
		state.sample = true;

		start_polling();
		/* Wake thread immediately for the first sample */
		k_sem_give(&sem);

		break;

	case STATE_SUSPENDED:
	case STATE_SUSPENDED_DISCONNECTED:
		if (IS_ENABLED(CONFIG_DESKTOP_MOTION_PM_EVENTS)) {
			/* Wake up system - this will wake up thread */
			APP_EVENT_SUBMIT(new_wake_up_event());
			break;
		}
		/* Fall-through */

	case STATE_FETCHING:
		break;
	case STATE_DISABLED:
	case STATE_DISABLED_SUSPENDED:
		/* Invalid state */
		__ASSERT_NO_MSG(false);
		break;

	default:
		__ASSERT_NO_MSG(false);
		break;
	}
	k_spin_unlock(&state.lock, key);
}

static int init(void)
{
	int err;

	if (!device_is_ready(sensor_dev)) {
		LOG_ERR("Sensor device not ready");
		return -ENODEV;
	}

	k_spinlock_key_t key = k_spin_lock(&state.lock);
	bool is_connected = (state.peer_count != 0);

	switch (state.state) {
	case STATE_DISABLED:
		if (is_connected) {
			state.state = STATE_IDLE;
			LOG_ERR("STATE_IDLE");
		} else {
			state.state = STATE_DISCONNECTED;
			LOG_ERR("STATE_DISCONNECTED");
		}
		break;
	case STATE_DISABLED_SUSPENDED:
		if (is_connected) {
			state.state = STATE_SUSPENDED;
			LOG_ERR("STATE_SUSPENDED");
		} else {
			state.state = STATE_SUSPENDED_DISCONNECTED;
			LOG_ERR("STATE_SUSPENDED_DISCONNECTED");
		}
		break;
	default:
		/* No action */
		LOG_ERR("No action");
		break;
	}

	k_spin_unlock(&state.lock, key);

	do {
		err = enable_trigger();
		if (err == -EBUSY) {
			k_sleep(K_MSEC(1));
		}
	} while (err == -EBUSY);

	if (err) {
		LOG_ERR("Cannot enable trigger");
	}

	// err = cpi_key_init();
	// if (err) {
	// 	LOG_ERR("failed to init cpi key");
	// }
#ifdef POLLING_MODE
	k_timer_start(&timer0, POLLING_INTERVAL, POLLING_INTERVAL);
#endif

#ifdef CONFIG_POLLING_RATE_ENABLE
	err = init_polling();
	if (err) {
		LOG_ERR("Failed to init polling: %d", err);
	}
#endif

	return err;
}

static void fetch_config(const uint8_t opt_id, uint8_t *data, size_t *size)
{
	if (opt_id == SENSOR_OPT_VARIANT) {
		*size = strlen(CONFIG_DESKTOP_MOTION_SENSOR_TYPE);
		__ASSERT_NO_MSG((*size != 0) && (*size < CONFIG_CHANNEL_FETCHED_DATA_MAX_SIZE));
		strcpy((char *)data, CONFIG_DESKTOP_MOTION_SENSOR_TYPE);
	} else {
		enum motion_sensor_option option = config_opt_id_2_option(opt_id);

		if (option < MOTION_SENSOR_OPTION_COUNT) {
			k_spinlock_key_t key = k_spin_lock(&state.lock);
			sys_put_le32(state.option[option], data);
			k_spin_unlock(&state.lock, key);
			*size = sizeof(state.option[option]);
			LOG_WRN("fetch opt_id: %d, value %d", opt_id, *size);
		} else {
			LOG_WRN("Unsupported fetch opt_id: %" PRIu8, opt_id);
		}
	}
}

static void store_config(uint8_t opt_id, const uint8_t *data, size_t data_size)
{
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		char key[MAX_KEY_LEN];

		int err = snprintk(key, sizeof(key), MODULE_NAME "/%s", opt_descr[opt_id]);

		if ((err > 0) && (err < MAX_KEY_LEN)) {
			err = settings_save_one(key, data, data_size);
		}

		if (err) {
			LOG_ERR("Problem storing %s (err = %d)", opt_descr[opt_id], err);
		}
	}
}

static void update_config(const uint8_t opt_id, const uint8_t *data, const size_t size)
{
	enum motion_sensor_option option = config_opt_id_2_option(opt_id);

	if (option < MOTION_SENSOR_OPTION_COUNT) {
		if (size != sizeof(state.option[option])) {
			LOG_WRN("Invalid option size (%zu)", size);
			return;
		}

		uint32_t value = sys_get_le32(data);

		LOG_ERR("try to set Option[%d]: %d, %s set to %d", opt_id, option,
			opt_descr[opt_id], value);

		if (set_option(option, value)) {
			store_config(opt_id, data, size);
			LOG_INF("Option: %s set to %" PRIu32, opt_descr[opt_id], value);
		}
	} else {
		LOG_WRN("Unsupported set opt_id: %" PRIu8, opt_id);
	}
}

static void write_config(void)
{
	uint32_t option[MOTION_SENSOR_OPTION_COUNT];
	uint32_t mask;

	BUILD_ASSERT(sizeof(option) == sizeof(state.option), "");

	k_spinlock_key_t key = k_spin_lock(&state.lock);
	mask = state.option_mask;
	memcpy(option, state.option, sizeof(option));
	state.option_mask = 0;
	k_spin_unlock(&state.lock, key);

	for (enum motion_sensor_option i = 0; i < MOTION_SENSOR_OPTION_COUNT; ++i) {
		int attr = motion_sensor_option_attr[i];

		if (attr == -ENOTSUP) {
			LOG_WRN("attr not supported in sensor, OPTION: %u", i);
			continue;
		}

		if ((mask & BIT(i)) == 0) {
			continue;
		}

		__ASSERT_NO_MSG(attr != -ENOTSUP);

		struct sensor_value val = {.val1 = option[i]};

		int err = sensor_attr_set(sensor_dev, SENSOR_CHAN_ALL, (enum sensor_attribute)attr,
					  &val);

		LOG_WRN("OPTION: %u, value: %d", i, option[i]);

		if (err) {
			LOG_WRN("Cannot update attr[%d]: %d (err:%d)", i, attr, err);
		}
	}

	if ((mask & BIT(MOTION_SENSOR_OPTION_POLLING_RATE_ESB)) ||
	    (mask & BIT(MOTION_SENSOR_OPTION_POLLING_RATE_USB))) {
		k_spinlock_key_t key = k_spin_lock(&state.lock);
		bool is_fetching = (state.state == STATE_FETCHING);
		k_spin_unlock(&state.lock, key);
		if (is_fetching) {
			start_polling();
		}
	}

	if (mask & BIT(MOTION_SENSOR_OPTION_CPI_STAGE_ACTIVE)) {
		/* Cycle CPI stages */
		k_spinlock_key_t key = k_spin_lock(&state.lock);
		uint32_t cpi_stage = state.option[MOTION_SENSOR_OPTION_CPI_STAGE_ACTIVE];
		uint32_t new_cpi = state.option[motion_sensor_cpi_stage[cpi_stage]];
		/* Get the value associated with this stage */
		k_spin_unlock(&state.lock, key);
		/* Set the ACTIVE CPI option to the new value */
		set_option(MOTION_SENSOR_OPTION_CPI, new_cpi);
		LOG_INF("Set cpi stage %u, val %u", cpi_stage, new_cpi);
	}
}

static void motion_thread_fn(void)
{
	int err;

	while (true) {
		/* Wait for Semaphore (From Counter ISR, IRQ Trigger, or Config change) */
		k_sem_take(&sem, K_FOREVER);

		k_spinlock_key_t key = k_spin_lock(&state.lock);
		bool perform_fetch = (state.state == STATE_FETCHING);
		uint32_t option_bm = state.option_mask;
		k_spin_unlock(&state.lock, key);

		/* Only attempt fetch if we are actually in fetching state */
		if (perform_fetch) {
			err = motion_read(true);

			if (err == -ENODATA) {
				/* No motion: Stop Polling, Re-enable Interrupts */
				stop_polling();

				key = k_spin_lock(&state.lock);
				state.state = STATE_IDLE;
				k_spin_unlock(&state.lock, key);

				enable_trigger();
			}
		}

		if (IS_ENABLED(CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE) && unlikely(option_bm)) {
			write_config();
		}
	}
}

static bool handle_usb_state_event(const struct usb_state_event *event)
{
	switch (event->state) {
	case USB_STATE_POWERED:
		set_option(MOTION_SENSOR_OPTION_SLEEP_ENABLE, false);
		break;
	case USB_STATE_ACTIVE:
		state.usb_is_connected = true;
		LOG_ERR("USB connected");
		/* Immediately switch to USB polling rate */
#ifdef CONFIG_POLLING_RATE_ENABLE
		{
			k_spinlock_key_t key = k_spin_lock(&state.lock);
			if (state.state == STATE_IDLE || state.state == STATE_DISCONNECTED) {
				/* Switch to fetching state and start polling */
				disable_trigger();
				state.state = STATE_FETCHING;
				k_spin_unlock(&state.lock, key);
				start_polling();
				k_sem_give(&sem);
			} else if (state.state == STATE_FETCHING) {
				/* Already fetching, just restart with USB rate */
				k_spin_unlock(&state.lock, key);
				start_polling();
			} else {
				k_spin_unlock(&state.lock, key);
			}
		}
#endif
		break;

	case USB_STATE_DISCONNECTED:
		set_option(MOTION_SENSOR_OPTION_SLEEP_ENABLE, true);
		break;
	case USB_STATE_SUSPENDED:
		state.usb_is_connected = false;
		LOG_ERR("USB disconnected");
		/* Switch back to ESB polling rate if still fetching */
#ifdef CONFIG_POLLING_RATE_ENABLE
		{
			k_spinlock_key_t key = k_spin_lock(&state.lock);
			if (state.state == STATE_FETCHING) {
				k_spin_unlock(&state.lock, key);
				start_polling(); /* Will use ESB rate now */
			} else {
				k_spin_unlock(&state.lock, key);
			}
		}
#endif
		break;

	default:
		break;
	}
	return false;
}

static bool click_event_handler(const struct click_event *event)
{
	if (event->key_id != CPI_KEY_ID) {
		return false;
	}

	/* Cycle CPI stages */
	k_spinlock_key_t key = k_spin_lock(&state.lock);
	uint32_t cpi_stage = state.option[MOTION_SENSOR_OPTION_CPI_STAGE_ACTIVE];
	uint32_t new_cpi_stage = (cpi_stage + 1) % SENSOR_CPI_STAGE_COUNT;
	state.option[MOTION_SENSOR_OPTION_CPI_STAGE_ACTIVE] = new_cpi_stage;
	uint32_t new_cpi = state.option[motion_sensor_cpi_stage[new_cpi_stage]];
	/* Get the value associated with this stage */
	k_spin_unlock(&state.lock, key);
	/* Set the ACTIVE CPI option to the new value */
	set_option(MOTION_SENSOR_OPTION_CPI, new_cpi);
	LOG_INF("Set cpi stage %u, val %u", cpi_stage, new_cpi);

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	// if (is_hid_report_sent_event(aeh)) {
	// 	const struct hid_report_sent_event *event =
	// 		cast_hid_report_sent_event(aeh);

	// 	if ((event->report_id == REPORT_ID_MOUSE) ||
	// 	    (event->report_id == REPORT_ID_BOOT_MOUSE)) {
	// 		k_spinlock_key_t key = k_spin_lock(&state.lock);
	// 		if (state.state == STATE_FETCHING) {
	// 			state.sample = true;
	// 			k_sem_give(&sem);
	// 			// LOG_ERR("k_sem_give, hid report sent");
	// 		}
	// 		k_spin_unlock(&state.lock, key);
	// 	}

	// 	return false;
	// }

	if (is_hid_report_subscription_event(aeh)) {
		const struct hid_report_subscription_event *event =
			cast_hid_report_subscription_event(aeh);

		if ((event->report_id == REPORT_ID_MOUSE) ||
		    (event->report_id == REPORT_ID_BOOT_MOUSE)) {
			if (event->enabled) {
				__ASSERT_NO_MSG(state.peer_count < UCHAR_MAX);
				state.peer_count++;
			} else {
				__ASSERT_NO_MSG(state.peer_count > 0);
				state.peer_count--;
			}

			bool is_connected = (state.peer_count != 0);

			set_sampling_time_in_sleep3(is_connected);

			k_spinlock_key_t key = k_spin_lock(&state.lock);
			/* Reset state machine based on connection */
			if (state.state == STATE_DISCONNECTED && is_connected) {
				state.state = STATE_IDLE;
				enable_trigger();
			} else if (!is_connected) {
				disable_trigger();
				stop_polling();
				state.state = STATE_DISCONNECTED;
			}
			k_spin_unlock(&state.lock, key);
		}

		return false;
	}

	if (is_module_state_event(aeh)) {
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			/* Start state machine thread */
			__ASSERT_NO_MSG(state.state == STATE_DISABLED);

			set_default_configuration();

			int err = init();

			if (!err) {
				module_set_state(MODULE_STATE_READY);
				LOG_INF("Motion sensor initialized");
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}

			k_thread_create(&thread, thread_stack, THREAD_STACK_SIZE,
					(k_thread_entry_t)motion_thread_fn, NULL, NULL, NULL,
					THREAD_PRIORITY, 0, K_NO_WAIT);
			k_thread_name_set(&thread, MODULE_NAME "_thread");

			return false;
		}

		// if (IS_ENABLED(CONFIG_DESKTOP_ESB_TX_ENABLE)){
		// if (check_state(event, MODULE_ID(esb_tx), MODULE_STATE_READY)) {
		// 	k_spinlock_key_t key = k_spin_lock(&state.lock);
		// 	if ((state.state == STATE_SUSPENDED) ||
		// 		(state.state == STATE_SUSPENDED_DISCONNECTED)) {
		// 		disable_trigger();
		// 		if (state.state == STATE_SUSPENDED) {
		// 			state.state = STATE_FETCHING;
		// 			state.sample = true;
		// 		} else {
		// 			state.state = STATE_DISCONNECTED;
		// 		}
		// 		k_sem_give(&sem);

		// 		module_set_state(MODULE_STATE_READY);
		// 	} else if (state.state == STATE_DISABLED_SUSPENDED) {
		// 		state.state = STATE_DISABLED;
		// 	}
		// 	k_spin_unlock(&state.lock, key);

		// 	return false;
		// }}

		return false;
	}

	if (is_click_event(aeh)) {
		return click_event_handler(cast_click_event(aeh));
	}

	if (IS_ENABLED(CONFIG_DESKTOP_MOTION_PM_EVENTS) && is_wake_up_event(aeh)) {
#ifdef POLLING_MODE_WITH_COUNTER
		// counter_reset(counter_dev);
		counter_start(counter_dev);
#endif // POLLING_MODE_WITH_COUNTER
		k_spinlock_key_t key = k_spin_lock(&state.lock);
		if (state.state == STATE_SUSPENDED) {
			/* Wake up: transition to Fetching */
			start_polling();
			state.state = STATE_FETCHING;
			state.sample = true;
			k_sem_give(&sem);
			module_set_state(MODULE_STATE_READY);
		}

		k_spin_unlock(&state.lock, key);

		return false;
	}

	if (IS_ENABLED(CONFIG_DESKTOP_MOTION_PM_EVENTS) && is_power_down_event(aeh)) {
		k_spinlock_key_t key = k_spin_lock(&state.lock);
		stop_polling();
		enable_trigger(); /* Ensure wake-on-motion is active */
		state.state = STATE_SUSPENDED;
		module_set_state(MODULE_STATE_STANDBY);
		k_spin_unlock(&state.lock, key);
		return false;
	}

	if (IS_ENABLED(CONFIG_DESKTOP_MOTION_SENSOR_SLEEP_DISABLE_ON_USB) &&
	    is_usb_state_event(aeh)) {
		return handle_usb_state_event(cast_usb_state_event(aeh));
	}

	GEN_CONFIG_EVENT_HANDLERS(STRINGIFY(MODULE), opt_descr, update_config, fetch_config);

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
// APP_EVENT_SUBSCRIBE(MODULE, hid_report_sent_event);
APP_EVENT_SUBSCRIBE(MODULE, click_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_report_subscription_event);
#if CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE
APP_EVENT_SUBSCRIBE_EARLY(MODULE, config_event);
#endif
#if CONFIG_DESKTOP_MOTION_SENSOR_SLEEP_DISABLE_ON_USB
APP_EVENT_SUBSCRIBE(MODULE, usb_state_event);
#endif
#if CONFIG_DESKTOP_MOTION_PM_EVENTS
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, power_down_event);
#endif
