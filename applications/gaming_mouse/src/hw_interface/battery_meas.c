/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>

#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include <hal/nrf_saadc.h>

#include <app_event_manager.h>
#include <caf/events/power_event.h>
#include "battery_event.h"
#include "battery_def.h"

#define MODULE battery_meas
#include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DESKTOP_BATTERY_MEAS_LOG_LEVEL);

#define ADC_NODE	       DT_NODELABEL(adc)
#define ADC_RESOLUTION	       12
#define ADC_OVERSAMPLING       4    /* 2^ADC_OVERSAMPLING samples are averaged */
#define ADC_MAX		       4096 // 2^12
#define ADC_GAIN	       BATTERY_MEAS_ADC_GAIN
#define ADC_REFERENCE	       ADC_REF_INTERNAL
#define ADC_REF_INTERNAL_MV    600UL
#define ADC_ACQUISITION_TIME   ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_CHANNEL_ID	       0
#define ADC_CHANNEL_INPUT      BATTERY_MEAS_ADC_INPUT
#define BATTERY_CHARGING_LEVEL 15 /* in percent */
/* ADC asynchronous read is scheduled on odd works. Value read happens during
 * even works. This is done to avoid creating a thread for battery monitor.
 */
#define BATTERY_WORK_INTERVAL  (CONFIG_DESKTOP_BATTERY_MEAS_POLL_INTERVAL_MS / 2)

#if IS_ENABLED(CONFIG_DESKTOP_BATTERY_MEAS_HAS_VOLTAGE_DIVIDER)
#define BATTERY_VOLTAGE(sample)                                                                    \
	((uint32_t)((uint64_t)(sample) * (uint64_t)BATTERY_MEAS_VOLTAGE_GAIN *                     \
		    (uint64_t)ADC_REF_INTERNAL_MV *                                                \
		    ((uint64_t)CONFIG_DESKTOP_BATTERY_MEAS_VOLTAGE_DIVIDER_UPPER +                 \
		     (uint64_t)CONFIG_DESKTOP_BATTERY_MEAS_VOLTAGE_DIVIDER_LOWER) /                \
		    (uint64_t)CONFIG_DESKTOP_BATTERY_MEAS_VOLTAGE_DIVIDER_LOWER /                  \
		    (uint64_t)ADC_MAX))
#else
#define BATTERY_VOLTAGE(sample)                                                                    \
	((uint32_t)((uint64_t)(sample) * (uint64_t)BATTERY_MEAS_VOLTAGE_GAIN *                     \
		    (uint64_t)ADC_REF_INTERNAL_MV * (uint64_t)ADC_MAX))
#endif

static const struct device *const adc_dev = DEVICE_DT_GET(ADC_NODE);
static int16_t adc_buffer;
static bool adc_async_read_pending;

static struct k_work_delayable battery_lvl_read;
static struct k_poll_signal async_sig = K_POLL_SIGNAL_INITIALIZER(async_sig);
static struct k_poll_event async_evt =
	K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &async_sig);

static const struct device *const gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static atomic_t active;
static bool sampling;
struct State {
	struct k_spinlock lock;
	uint8_t bat_level;
};

static struct State state;

static int init_adc(void)
{
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	static const struct adc_channel_cfg channel_cfg = {
		.gain = ADC_GAIN,
		.reference = ADC_REFERENCE,
		.acquisition_time = ADC_ACQUISITION_TIME,
		.channel_id = ADC_CHANNEL_ID,
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
		.input_positive = ADC_CHANNEL_INPUT,
#endif
	};
	int err = adc_channel_setup(adc_dev, &channel_cfg);
	if (err) {
		LOG_ERR("Setting up the ADC channel failed");
		return err;
	}

	/* Check if number of elements in LUT is proper */
	BUILD_ASSERT(CONFIG_DESKTOP_BATTERY_MEAS_MAX_LEVEL -
				     CONFIG_DESKTOP_BATTERY_MEAS_MIN_LEVEL ==
			     (ARRAY_SIZE(battery_voltage_to_soc) - 1) *
				     CONFIG_DESKTOP_VOLTAGE_TO_SOC_DELTA,
		     "Improper number of elements in lookup table");

	return 0;
}

static int battery_monitor_start(void)
{
	if (IS_ENABLED(CONFIG_DESKTOP_BATTERY_MEAS_HAS_ENABLE_PIN)) {
		int err = gpio_pin_set_raw(gpio_dev, CONFIG_DESKTOP_BATTERY_MEAS_ENABLE_PIN, 1);
		if (err) {
			LOG_ERR("Cannot enable battery monitor circuit");
			return err;
		}
	}

	sampling = true;
	k_work_reschedule(&battery_lvl_read, K_MSEC(BATTERY_WORK_INTERVAL));

	return 0;
}

static void battery_monitor_stop(void)
{
	/* Cancel cannot fail if executed from another work's context. */
	(void)k_work_cancel_delayable(&battery_lvl_read);
	sampling = false;
	int err = 0;

	if (IS_ENABLED(CONFIG_DESKTOP_BATTERY_MEAS_HAS_ENABLE_PIN)) {
		err = gpio_pin_set_raw(gpio_dev, CONFIG_DESKTOP_BATTERY_MEAS_ENABLE_PIN, 0);
		if (err) {
			LOG_ERR("Cannot disable battery monitor circuit");
			module_set_state(MODULE_STATE_ERROR);

			return;
		}
	}

	module_set_state(MODULE_STATE_STANDBY);
}

static void battery_lvl_process(void)
{
	uint32_t voltage = BATTERY_VOLTAGE(adc_buffer);
	uint8_t level;
	// LOG_ERR("Battery ADC val: %u voltage: %u ", adc_buffer, voltage);

	if (voltage > CONFIG_DESKTOP_BATTERY_MEAS_MAX_LEVEL) {
		level = 100;
	} else if (voltage < CONFIG_DESKTOP_BATTERY_MEAS_MIN_LEVEL) {
		level = 0;
		LOG_WRN("Low battery");
	} else {
		/* Using lookup table to convert voltage[mV] to SoC[%] */
		size_t lut_id = (voltage - CONFIG_DESKTOP_BATTERY_MEAS_MIN_LEVEL +
				 (CONFIG_DESKTOP_VOLTAGE_TO_SOC_DELTA >> 1)) /
				CONFIG_DESKTOP_VOLTAGE_TO_SOC_DELTA;
		level = battery_voltage_to_soc[lut_id];
	}

	if (level < BATTERY_CHARGING_LEVEL) {
		struct low_battery_event *low_bat_event = new_low_battery_event();
		low_bat_event->level = level;
		APP_EVENT_SUBMIT(low_bat_event);
	}
	k_spinlock_key_t key = k_spin_lock(&state.lock);
	state.bat_level = level;
	k_spin_unlock(&state.lock, key);

	struct battery_level_event *event = new_battery_level_event();
	event->level = level;

	APP_EVENT_SUBMIT(event);

	// LOG_INF("Battery level: %u%% (%u mV)", level, voltage);
}

static void battery_lvl_read_fn(struct k_work *work)
{
	int err;

	if (!adc_async_read_pending) {
		static const struct adc_sequence sequence = {
			.options = NULL,
			.channels = BIT(ADC_CHANNEL_ID),
			.buffer = &adc_buffer,
			.buffer_size = sizeof(adc_buffer),
			.resolution = ADC_RESOLUTION,
			.oversampling = ADC_OVERSAMPLING,
			.calibrate = false,
		};
		static const struct adc_sequence sequence_calibrate = {
			.options = NULL,
			.channels = BIT(ADC_CHANNEL_ID),
			.buffer = &adc_buffer,
			.buffer_size = sizeof(adc_buffer),
			.resolution = ADC_RESOLUTION,
			.oversampling = ADC_OVERSAMPLING,
			.calibrate = true,
		};

		static bool calibrated;

		if (likely(calibrated)) {
			err = adc_read_async(adc_dev, &sequence, &async_sig);
		} else {
			err = adc_read_async(adc_dev, &sequence_calibrate, &async_sig);
			calibrated = true;
		}

		if (err) {
			LOG_WRN("Battery level async read failed");
		} else {
			adc_async_read_pending = true;
		}
	} else {
		err = k_poll(&async_evt, 1, K_NO_WAIT);
		if (err) {
			LOG_WRN("Battery level poll failed");
		} else {
			adc_async_read_pending = false;
			battery_lvl_process();
		}
	}

	if (atomic_get(&active) || adc_async_read_pending) {
		k_work_reschedule(&battery_lvl_read, K_MSEC(BATTERY_WORK_INTERVAL));
	} else {
		battery_monitor_stop();
	}
}

static int init_fn(void)
{
	int err = 0;

	if (IS_ENABLED(CONFIG_DESKTOP_BATTERY_MEAS_HAS_ENABLE_PIN)) {
		if (!device_is_ready(gpio_dev)) {
			LOG_ERR("GPIO device not ready");
			err = -ENODEV;
			goto error;
		}

		/* Enable battery monitoring */
		err = gpio_pin_configure(gpio_dev, CONFIG_DESKTOP_BATTERY_MEAS_ENABLE_PIN,
					 GPIO_OUTPUT);
	}

	if (!err) {
		err = init_adc();
	}

	if (err) {
		goto error;
	}

	k_work_init_delayable(&battery_lvl_read, battery_lvl_read_fn);

	err = battery_monitor_start();
error:
	return err;
}

#if CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE

#include "config_event.h"

/* Description of the option representing module variant. */
#define OPT_DESCR_MODULE_VARIANT "module_variant"
enum bat_opt {
	// BAT_OPT_VARIANT,
	BAT_OPT_BAT_LEVEL,
	BAT_OPT_COUNT
};

const static char *const opt_descr[] = {
	// [BAT_OPT_VARIANT] = OPT_DESCR_MODULE_VARIANT,
	[BAT_OPT_BAT_LEVEL] = "bat_level",

};

static void update_config(const uint8_t opt_id, const uint8_t *data, const size_t size)
{
	switch (opt_id) {
	default:
		/* Ignore unknown event. */
		LOG_WRN("Unknown event");
		break;
	}
}

static void fetch_config(const uint8_t opt_id, uint8_t *data, size_t *size)
{
	switch (opt_id) {
		// case BAT_OPT_VARIANT:
		// 	*size = strlen("bat");
		// 	__ASSERT_NO_MSG((*size != 0) &&
		// 			(*size < CONFIG_CHANNEL_FETCHED_DATA_MAX_SIZE));
		// 	strcpy((char *)data, "bat");
		// 	break;

	case BAT_OPT_BAT_LEVEL:
		k_spinlock_key_t key = k_spin_lock(&state.lock);
		sys_put_le32(state.bat_level, data);
		*size = sizeof(state.bat_level);
		k_spin_unlock(&state.lock, key);
		break;

	default:
		/* Ignore unknown event. */
		LOG_WRN("Unknown event");
		break;
	}
}
#endif

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			static bool initialized;

			__ASSERT_NO_MSG(!initialized);
			initialized = true;

			int err = init_fn();

			if (err) {
				module_set_state(MODULE_STATE_ERROR);
			} else {
				module_set_state(MODULE_STATE_READY);
				atomic_set(&active, true);
			}

			return false;
		}

		return false;
	}

	if (is_wake_up_event(aeh)) {
		if (!atomic_get(&active)) {
			atomic_set(&active, true);

			int err = battery_monitor_start();

			if (!err) {
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
		}

		return false;
	}

	if (is_power_down_event(aeh)) {
		if (atomic_get(&active)) {
			atomic_set(&active, false);

			if (adc_async_read_pending) {
				__ASSERT_NO_MSG(sampling);
				/* Poll ADC and postpone shutdown */
				k_work_reschedule(&battery_lvl_read, K_MSEC(0));
			} else {
				/* No ADC sample left to read, go to standby */
				battery_monitor_stop();
			}
		}

		return sampling;
	}

	GEN_CONFIG_EVENT_HANDLERS(STRINGIFY(MODULE), opt_descr, update_config, fetch_config);

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
#if CONFIG_DESKTOP_CONFIG_CHANNEL_ENABLE
APP_EVENT_SUBSCRIBE_EARLY(MODULE, config_event);
#endif
