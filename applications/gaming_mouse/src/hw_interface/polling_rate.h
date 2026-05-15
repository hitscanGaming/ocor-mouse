/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef POLLING_RATE_H_
#define POLLING_RATE_H_

/**
 * @file polling_rate.h
 * @brief Polling rate timer module for high-frequency sensor polling.
 *
 * This module encapsulates counter-based timer functionality for
 * configurable polling rates (e.g., 125Hz - 8000Hz for gaming mice).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type invoked on each polling timer tick.
 *
 * This callback is executed in ISR context. Keep operations minimal.
 */
typedef void (*polling_rate_callback_t)(void);

/**
 * @brief Initialize the polling rate timer module.
 *
 * Must be called before start/stop operations.
 *
 * @return 0 on success, negative errno on failure.
 */
int polling_rate_init(void);

/**
 * @brief Set the callback for polling timer ticks.
 *
 * @param cb Callback function. Pass NULL to disable.
 */
void polling_rate_set_callback(polling_rate_callback_t cb);

/**
 * @brief Set polling rate in Hz.
 *
 * Value is clamped to configured min/max bounds.
 *
 * @param rate_hz Polling rate in Hz.
 */
void polling_rate_set_hz(uint32_t rate_hz);

/**
 * @brief Get current polling interval in microseconds.
 *
 * @return Interval in microseconds.
 */
uint32_t polling_rate_get_interval_us(void);

/**
 * @brief Get current polling rate in Hz.
 *
 * @return Polling rate in Hz.
 */
uint32_t polling_rate_get_hz(void);

/**
 * @brief Start the polling timer.
 *
 * Uses the currently configured rate.
 *
 * @return 0 on success, negative errno on failure.
 */
int polling_rate_start(void);

/**
 * @brief Stop the polling timer.
 *
 * @return 0 on success, negative errno on failure.
 */
int polling_rate_stop(void);

/**
 * @brief Check if the polling timer is running.
 *
 * @return true if running, false otherwise.
 */
bool polling_rate_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* POLLING_RATE_H_ */
