/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <assert.h>

#include <zephyr/types.h>

#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <pinctrl_soc.h>

#include <app_event_manager.h>
#include "wheel_event.h"
#include <caf/events/power_event.h>

#define MODULE wheel
#include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_DESKTOP_WHEEL_LOG_LEVEL);

#define PINS_PER_GPIO_PORT     32
#define SENSOR_IDLE_TIMEOUT    (CONFIG_DESKTOP_WHEEL_SENSOR_IDLE_TIMEOUT * MSEC_PER_SEC) /* ms */
#define WHEEL_STEP_INTERVAL_MS 1

/* 精度缩放因子 (1000 = Milli-degrees) */
#define PRECISION_SCALE 1000

enum state {
	STATE_DISABLED,
	STATE_ACTIVE_IDLE,
	STATE_ACTIVE,
	STATE_SUSPENDED
};

#if DT_NODE_HAS_STATUS(DT_ALIAS(nrfdesktop_wheel_qdec), okay)
#define QDEC_DT_NODE_ID DT_ALIAS(nrfdesktop_wheel_qdec)
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(qdec), okay)
#define QDEC_DT_NODE_ID DT_NODELABEL(qdec)
#else
#error DT node for QDEC must be specified.
#endif

#define QDEC_PIN_INIT(node_id, prop, idx) NRF_GET_PIN(DT_PROP_BY_IDX(node_id, prop, idx)),

static const uint32_t qdec_pin[] = {
	DT_FOREACH_CHILD_VARGS(DT_PINCTRL_BY_NAME(QDEC_DT_NODE_ID, default, 0),
			       DT_FOREACH_PROP_ELEM, psels, QDEC_PIN_INIT)};

static const struct sensor_trigger qdec_trig = {
	.type = SENSOR_TRIG_DATA_READY,
	.chan = SENSOR_CHAN_ROTATION,
};

static const struct device *qdec_dev = DEVICE_DT_GET(QDEC_DT_NODE_ID);
static struct gpio_callback gpio_cbs[ARRAY_SIZE(qdec_pin)];
static struct k_spinlock lock;
static struct k_work_delayable idle_timeout;
static struct k_work_delayable report_work;
static bool qdec_triggered;
static enum state state;

/* 使用高精度累加器 (单位：毫度) */
static int32_t wheel_accumulator_mdeg = 0;
/* 待发送事件队列 */
static int32_t pending_events = 0;

static int enable_qdec(enum state next_state);

static const struct device *map_gpio_port(int pin_number)
{
	__ASSERT_NO_MSG(pin_number >= 0);
	size_t port_idx = pin_number / PINS_PER_GPIO_PORT;
	const struct device *dev = NULL;

	switch (port_idx) {
	case 0:
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio0));
		break;
	case 1:
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio1));
		break;
	default:
		break;
	}
	__ASSERT_NO_MSG(dev);
	return dev;
}

static gpio_pin_t map_gpio_pin(int pin_number)
{
	__ASSERT_NO_MSG(pin_number >= 0);
	return pin_number % PINS_PER_GPIO_PORT;
}

static void report_work_handler(struct k_work *work)
{
	struct wheel_event *event;
	int32_t val = 0;

	k_spinlock_key_t key = k_spin_lock(&lock);
	if (pending_events > 0) {
		val = 1;
		pending_events--;
	} else if (pending_events < 0) {
		val = -1;
		pending_events++;
	}
	k_spin_unlock(&lock, key);

	if (val != 0) {
		event = new_wheel_event();
		event->wheel = val;
		APP_EVENT_SUBMIT(event);
		LOG_DBG("Wheel Event: %d (Pending: %d)", val, pending_events);
	}

	key = k_spin_lock(&lock);
	if (pending_events != 0) {
		k_work_reschedule(&report_work, K_MSEC(WHEEL_STEP_INTERVAL_MS));
	}
	k_spin_unlock(&lock, key);
}

static void data_ready_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	if (IS_ENABLED(CONFIG_ASSERT)) {
		k_spinlock_key_t key = k_spin_lock(&lock);
		__ASSERT_NO_MSG(state == STATE_ACTIVE);
		k_spin_unlock(&lock, key);
	}

	int err = sensor_sample_fetch_chan(qdec_dev, SENSOR_CHAN_ROTATION);
	if (err) {
		LOG_ERR("Cannot fetch sensor sample (err %d)", err);
		return;
	}

	struct sensor_value value;
	err = sensor_channel_get(qdec_dev, SENSOR_CHAN_ROTATION, &value);
	if (err) {
		LOG_ERR("Cannot get sensor value");
		return;
	}

	/* --- 高精度计算逻辑 Start --- */

	int32_t delta_mdeg =
		value.val1 * PRECISION_SCALE + value.val2 / (1000000 / PRECISION_SCALE);

	if (!IS_ENABLED(CONFIG_DESKTOP_WHEEL_INVERT_AXIS)) {
		delta_mdeg *= -1;
	}

	/* 2. 累加 */
	wheel_accumulator_mdeg += delta_mdeg;

	/* 3. 获取 Divider 并转换为毫度 (例如 15 -> 15000) */
	int32_t config_divider = CONFIG_DESKTOP_WHEEL_SENSOR_VALUE_DIVIDER;
	if (config_divider < 1) {
		config_divider = 1;
	}

	int32_t divider_mdeg = config_divider * PRECISION_SCALE;

	/* 4. 计算步数 (15000 / 15000 = 1) */
	int32_t events_to_send = wheel_accumulator_mdeg / divider_mdeg;

	/* 5. 保留余数 */
	wheel_accumulator_mdeg %= divider_mdeg;

	/* Logic Updated: Queue events for delayed reporting */
	if (events_to_send != 0) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		/* Check if direction changed. If so, discard pending events. */
		bool direction_change = (pending_events > 0 && events_to_send < 0) ||
					(pending_events < 0 && events_to_send > 0);

		if (direction_change) {
			pending_events = events_to_send;
			/* Force immediate report for new direction */
			k_work_reschedule(&report_work, K_NO_WAIT);
		} else {
			bool start_immediately = (pending_events == 0);
			pending_events += events_to_send;

			if (start_immediately) {
				k_work_reschedule(&report_work, K_NO_WAIT);
			}
		}
		k_spin_unlock(&lock, key);

		LOG_DBG("Queued %d events. Total Pending: %d", events_to_send, pending_events);
	}

	/* --- 高精度计算逻辑 End --- */

	qdec_triggered = true;
}

static int wakeup_int_ctrl_nolock(bool enable)
{
	int err = 0;
	for (size_t i = 0; (i < ARRAY_SIZE(qdec_pin)) && !err; i++) {
		const struct device *port = map_gpio_port(qdec_pin[i]);
		gpio_pin_t pin = map_gpio_pin(qdec_pin[i]);

		if (enable) {
			int val = gpio_pin_get_raw(port, pin);
			if (val < 0) {
				LOG_ERR("Cannot read pin %zu", i);
				err = val;
				continue;
			}
			err = gpio_pin_interrupt_configure(
				port, pin, val ? GPIO_INT_LEVEL_LOW : GPIO_INT_LEVEL_HIGH);
		} else {
			err = gpio_pin_interrupt_configure(port, pin, GPIO_INT_DISABLE);
		}
		if (err) {
			LOG_ERR("Cannot control cb (pin:%zu)", i);
		}
	}
	return err;
}

static void wakeup_cb(const struct device *gpio_dev, struct gpio_callback *cb, uint32_t pins)
{
	struct wake_up_event *event;
	int err;
	k_spinlock_key_t key = k_spin_lock(&lock);
	err = wakeup_int_ctrl_nolock(false);

	if (!err) {
		switch (state) {
		case STATE_ACTIVE_IDLE:
			err = enable_qdec(STATE_ACTIVE);
			break;
		case STATE_SUSPENDED:
			event = new_wake_up_event();
			APP_EVENT_SUBMIT(event);
			break;
		case STATE_ACTIVE:
		case STATE_DISABLED:
		default:
			__ASSERT_NO_MSG(false);
			break;
		}
	}
	k_spin_unlock(&lock, key);
	if (err) {
		module_set_state(MODULE_STATE_ERROR);
	}
}

static int setup_wakeup(void)
{
	int enable_pin = DT_PROP(QDEC_DT_NODE_ID, enable_pin);
	const struct device *port = map_gpio_port(enable_pin);
	gpio_pin_t pin = map_gpio_pin(enable_pin);

	int err = gpio_pin_configure(port, pin, GPIO_OUTPUT);
	if (err) {
		LOG_ERR("Cannot configure enable pin");
		return err;
	}
	err = gpio_pin_set_raw(port, pin, 0);
	if (err) {
		LOG_ERR("Failed to set enable pin");
		return err;
	}

	for (size_t i = 0; i < ARRAY_SIZE(qdec_pin); i++) {
		const struct device *port = map_gpio_port(qdec_pin[i]);
		gpio_pin_t pin = map_gpio_pin(qdec_pin[i]);
		err = gpio_pin_configure(port, pin, GPIO_INPUT);
		if (err) {
			LOG_ERR("Cannot configure pin %zu", i);
			return err;
		}
	}
	return wakeup_int_ctrl_nolock(true);
}

static int enable_qdec(enum state next_state)
{
	__ASSERT_NO_MSG(next_state == STATE_ACTIVE);
	int err = 0;
	if (state != STATE_DISABLED) {
		err = pm_device_action_run(qdec_dev, PM_DEVICE_ACTION_RESUME);
	}
	if (err) {
		LOG_ERR("Cannot resume QDEC");
		return err;
	}
	err = sensor_trigger_set(qdec_dev, (struct sensor_trigger *)&qdec_trig, data_ready_handler);
	if (err) {
		LOG_ERR("Cannot setup trigger");
	} else {
		state = next_state;
		if (SENSOR_IDLE_TIMEOUT > 0) {
			qdec_triggered = false;
			k_work_reschedule(&idle_timeout, K_MSEC(SENSOR_IDLE_TIMEOUT));
		}
	}
	return err;
}

static int disable_qdec(enum state next_state)
{
	if (SENSOR_IDLE_TIMEOUT > 0) {
		__ASSERT_NO_MSG((next_state == STATE_ACTIVE_IDLE) ||
				(next_state == STATE_SUSPENDED));
	} else {
		__ASSERT_NO_MSG(next_state == STATE_SUSPENDED);
	}
	int err = sensor_trigger_set(qdec_dev, (struct sensor_trigger *)&qdec_trig, NULL);
	if (err) {
		LOG_ERR("Cannot disable trigger");
		return err;
	}
	err = pm_device_action_run(qdec_dev, PM_DEVICE_ACTION_SUSPEND);
	if (err) {
		LOG_ERR("Cannot suspend QDEC");
	} else {
		err = setup_wakeup();
		if (!err) {
			if (SENSOR_IDLE_TIMEOUT > 0) {
				(void)k_work_cancel_delayable(&idle_timeout);
			}
			/* If fully suspending, cancel reporting and clear queue */
			if (next_state == STATE_SUSPENDED) {
				(void)k_work_cancel_delayable(&report_work);
				pending_events = 0;
			}
			state = next_state;
		}
	}
	return err;
}

static void idle_timeout_fn(struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	__ASSERT_NO_MSG(state == STATE_ACTIVE);
	if (!qdec_triggered) {
		int err = disable_qdec(STATE_ACTIVE_IDLE);
		if (err) {
			module_set_state(MODULE_STATE_ERROR);
		}
	} else {
		qdec_triggered = false;
		k_work_reschedule(&idle_timeout, K_MSEC(SENSOR_IDLE_TIMEOUT));
	}
	k_spin_unlock(&lock, key);
}

static int init(void)
{
	__ASSERT_NO_MSG(state == STATE_DISABLED);
	if (SENSOR_IDLE_TIMEOUT > 0) {
		k_work_init_delayable(&idle_timeout, idle_timeout_fn);
	}
	k_work_init_delayable(&report_work, report_work_handler);

	if (!device_is_ready(qdec_dev)) {
		LOG_ERR("QDEC device not ready");
		return -ENODEV;
	}
	int err = 0;
	for (size_t i = 0; (i < ARRAY_SIZE(qdec_pin)) && !err; i++) {
		const struct device *port = map_gpio_port(qdec_pin[i]);
		gpio_pin_t pin = map_gpio_pin(qdec_pin[i]);
		if (!device_is_ready(port)) {
			LOG_ERR("GPIO port %p not ready", (void *)port);
			err = -ENODEV;
		}
		if (!err) {
			gpio_init_callback(&gpio_cbs[i], wakeup_cb, BIT(pin));
			err = gpio_add_callback(port, &gpio_cbs[i]);
			if (err) {
				LOG_ERR("Cannot configure cb (pin:%zu)", i);
			}
		}
	}
	return err;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);
		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			int err = init();
			if (!err) {
				k_spinlock_key_t key = k_spin_lock(&lock);
				err = enable_qdec(STATE_ACTIVE);
				k_spin_unlock(&lock, key);
			}
			if (!err) {
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
			return false;
		}
		return false;
	}
	if (is_wake_up_event(aeh)) {
		int err;
		k_spinlock_key_t key = k_spin_lock(&lock);
		switch (state) {
		case STATE_SUSPENDED:
			err = wakeup_int_ctrl_nolock(false);
			if (!err) {
				err = enable_qdec(STATE_ACTIVE);
			}
			if (!err) {
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
			break;
		case STATE_ACTIVE:
		case STATE_ACTIVE_IDLE:
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
			err = disable_qdec(STATE_SUSPENDED);
			if (!err) {
				module_set_state(MODULE_STATE_STANDBY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
			break;
		case STATE_ACTIVE_IDLE:
			state = STATE_SUSPENDED;
			/* If moving from IDLE to SUSPENDED, we must also cancel work/clear queue */
			(void)k_work_cancel_delayable(&report_work);
			pending_events = 0;
			break;
		case STATE_SUSPENDED:
			break;
		case STATE_DISABLED:
		default:
			__ASSERT_NO_MSG(false);
			break;
		}
		k_spin_unlock(&lock, key);
		return false;
	}
	__ASSERT_NO_MSG(false);
	return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, power_down_event);