/*
 * Copyright (c) 2018-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device.h>

#include <caf/events/power_event.h>
#include <caf/events/led_event.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#define MODULE leds
#include <caf/events/module_state_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CAF_LEDS_LOG_LEVEL);

#define LED_ID(led) ((led) - &leds[0])

#ifdef CONFIG_CAF_LEDS_INDICATOR_POWER_CONTROL
static const struct gpio_dt_spec led_power_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(indicator), gpios);
#endif

struct led {
	const struct device *dev;
	uint8_t color_count;

	struct led_color color;
	const struct led_effect *effect;
	uint16_t effect_step;
	uint16_t effect_substep;

	struct k_work_delayable work;
};

#ifdef CONFIG_CAF_LEDS_PWM
#define DT_DRV_COMPAT pwm_leds
#elif defined(CONFIG_CAF_LEDS_GPIO)
#define DT_DRV_COMPAT gpio_leds
#else
#error "LED driver must be specified in configuration"
#endif

#define _LED_COLOR_ID(_unused) 0,

#define _LED_COLOR_COUNT(id)                                                                       \
	static const uint8_t led_colors_##id[] = {DT_INST_FOREACH_CHILD(id, _LED_COLOR_ID)};

DT_INST_FOREACH_STATUS_OKAY(_LED_COLOR_COUNT)

#define _LED_INSTANCE_DEF(id)                                                                      \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_DRV_INST(id)),                                             \
		.color_count = ARRAY_SIZE(led_colors_##id),                                        \
	},

static struct led leds[] = {DT_INST_FOREACH_STATUS_OKAY(_LED_INSTANCE_DEF)};

static int set_color_one_channel(struct led *led, struct led_color *color)
{
	/* For a single color LED convert color to brightness. */
	unsigned int brightness = 0;

	for (size_t i = 0; i < ARRAY_SIZE(color->c); i++) {
		brightness += color->c[i];
	}
	brightness /= ARRAY_SIZE(color->c);

	return led_set_brightness(led->dev, 0, brightness);
}

static int set_color_all_channels(struct led *led, struct led_color *color)
{
	int err = 0;

	for (size_t i = 0; (i < ARRAY_SIZE(color->c)) && !err; i++) {
		err = led_set_brightness(led->dev, i, color->c[i]);
	}

	return err;
}

static void set_color(struct led *led, struct led_color *color)
{
	int err;

	if (led->color_count == ARRAY_SIZE(color->c)) {
		err = set_color_all_channels(led, color);
	} else {
		err = set_color_one_channel(led, color);
	}

	if (err) {
		LOG_ERR("Cannot set LED brightness (err: %d)", err);
	}
}

static void set_off(struct led *led)
{
	struct led_color nocolor = {0};

	set_color(led, &nocolor);
}

static void work_handler(struct k_work *work)
{
	struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
	struct led *led = CONTAINER_OF(delayable_work, struct led, work);

	const struct led_effect_step *effect_step = &led->effect->steps[led->effect_step];

	__ASSERT_NO_MSG(effect_step->substep_count > 0);
	int substeps_left = effect_step->substep_count - led->effect_substep;

	for (size_t i = 0; i < ARRAY_SIZE(led->color.c); i++) {
		int diff = (effect_step->color.c[i] - led->color.c[i]) / substeps_left;
		led->color.c[i] += diff;
	}
	set_color(led, &led->color);

	led->effect_substep++;
	if (led->effect_substep == effect_step->substep_count) {
		led->effect_substep = 0;
		led->effect_step++;

		if (led->effect_step == led->effect->step_count) {
			if (led->effect->loop_forever) {
				led->effect_step = 0;
			} else {
				struct led_ready_event *ready_event = new_led_ready_event();

				ready_event->led_id = LED_ID(led);
				ready_event->led_effect = led->effect;

				APP_EVENT_SUBMIT(ready_event);
			}
		}
	}

	if (led->effect_step < led->effect->step_count) {
		int32_t next_delay = led->effect->steps[led->effect_step].substep_time;

		k_work_reschedule(&led->work, K_MSEC(next_delay));
	}
}

static void led_update(struct led *led)
{
	k_work_cancel_delayable(&led->work);

	led->effect_step = 0;
	led->effect_substep = 0;

	if (!led->effect) {
		LOG_ERR("No effect set");
		return;
	}

	__ASSERT_NO_MSG(led->effect->steps);

	if (led->effect->step_count > 0) {
		int32_t next_delay = led->effect->steps[led->effect_step].substep_time;

		k_work_reschedule(&led->work, K_MSEC(next_delay));
	} else {
		LOG_WRN("LED effect with no effect");
	}
}

static int leds_init(void)
{
	int err = 0;

	BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) > 0, "No LEDs defined");

	for (size_t i = 0; (i < ARRAY_SIZE(leds)) && !err; i++) {
		struct led *led = &leds[i];

		__ASSERT_NO_MSG((led->color_count == 1) || (led->color_count == 3));

		if (!device_is_ready(led->dev)) {
			LOG_ERR("Device %s is not ready", led->dev->name);
			err = -ENODEV;
		} else {
			k_work_init_delayable(&led->work, work_handler);

			/* 2. FORCE PINS TO OFF STATE
			 * This ensures that when we turn on the power rail below,
			 * the LEDs do not flash.
			 */
			set_off(led);

			led_update(led);
		}
	}

	if (err) {
		return err;
	}

/* 3. Configure and Turn ON Power Rail (U3)
 * Now that PWM pins are stable/off, we can safely apply power.
 */
#ifdef CONFIG_CAF_LEDS_INDICATOR_POWER_CONTROL
	if (gpio_is_ready_dt(&led_power_pin)) {
		/* Initialize to Active (High) to turn on LDO */
		err = gpio_pin_configure_dt(&led_power_pin, GPIO_OUTPUT_ACTIVE);
		if (err) {
			LOG_ERR("Failed to enable LED power rail (err: %d)", err);
		} else {
			LOG_INF("LED power rail enabled");
		}
	} else {
		LOG_ERR("LED power pin device not ready");
	}
#endif

	return err;
}

static void leds_start(void)
{
	/* 1. Ensure pins are logically OFF before applying power */
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		set_off(&leds[i]);
	}

#ifdef CONFIG_CAF_LEDS_INDICATOR_POWER_CONTROL
	/* 2. Turn ON Power Rail (Resume) */
	if (gpio_is_ready_dt(&led_power_pin)) {
		gpio_pin_set_dt(&led_power_pin, 1);
		LOG_INF("LED power rail resumed");
	}
#endif

	/* 3. Resume Effects */
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_CAF_LEDS_GPIO)
		int err = pm_device_action_run(leds[i].dev, PM_DEVICE_ACTION_RESUME);
		if (err) {
			LOG_ERR("Failed to set LED driver into active state (err: %d)", err);
		}
#endif
		led_update(&leds[i]);
	}
}

static void leds_stop(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		k_work_cancel_delayable(&leds[i].work);

		set_off(&leds[i]);

#if defined(CONFIG_PM_DEVICE) && !defined(CONFIG_CAF_LEDS_GPIO)
		int err = pm_device_action_run(leds[i].dev, PM_DEVICE_ACTION_SUSPEND);
		if (err) {
			LOG_ERR("Failed to set LED driver into suspend state (err: %d)", err);
		}
#endif
	}

#ifdef CONFIG_CAF_LEDS_INDICATOR_POWER_CONTROL
	/* Turn OFF Power Rail (Suspend) to save power */
	if (gpio_is_ready_dt(&led_power_pin)) {
		gpio_pin_set_dt(&led_power_pin, 0);
		LOG_INF("LED power rail suspended");
	}
#endif
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	static bool initialized;

	if (is_led_event(aeh)) {
		const struct led_event *event = cast_led_event(aeh);

		__ASSERT_NO_MSG(event->led_id < ARRAY_SIZE(leds));

		struct led *led = &leds[event->led_id];

		led->effect = event->led_effect;

		if (initialized) {
			led_update(led);
		}

		return false;
	}

	if (is_module_state_event(aeh)) {
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			int err = leds_init();

			if (!err) {
				initialized = true;
				module_set_state(MODULE_STATE_READY);
			} else {
				module_set_state(MODULE_STATE_ERROR);
			}
		}
		return false;
	}

	if (IS_ENABLED(CONFIG_CAF_LEDS_PM_EVENTS) && is_wake_up_event(aeh)) {
		if (!initialized) {
			leds_start();
			initialized = true;
			module_set_state(MODULE_STATE_READY);
		}

		return false;
	}

	if (IS_ENABLED(CONFIG_CAF_LEDS_PM_EVENTS) && is_power_down_event(aeh)) {
		const struct power_down_event *event = cast_power_down_event(aeh);

		/* Leds should keep working on system error. */
		if (event->error) {
			return false;
		}

		if (initialized) {
			leds_stop();
			initialized = false;
			module_set_state(MODULE_STATE_OFF);
		}

		return initialized;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, led_event);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
#if CONFIG_CAF_LEDS_PM_EVENTS
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
#endif