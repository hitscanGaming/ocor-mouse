/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>

#define MODULE spi_master
#include <caf/events/module_state_event.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log.h>
#include "hid_report_desc.h"
#include "polling_rate.h"

// #include "buffer_pool.h" // Removed as we use k_mem_slab now
// #include "hid_event.h"   // Removed as we use direct slab communication now

// static struct buffer_pool my_buffer_pool; // Removed

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

// Use the DeviceTree macro to get the SPI device instance and configuration.
#define SPIOP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER)
struct gpio_dt_spec cs_gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(master));

uint8_t sending_buf[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
// static uint8_t spi_data[BUFFER_SIZE] __aligned(4); // Removed - using slab memory directly

extern int esb_write(uint8_t *data, size_t size);
extern void enter_pairing_mode(void);
/* --- ZERO-COPY OPTIMIZATION: SLAB DEFINITIONS --- */
#define REPORT_ID_SPI_MASTER_READ_ONLY 0xA0
#define REPORT_ID_PAIRING_REQUEST      0x07
#define SLAB_BLOCK_COUNT	       16 // Number of blocks in the slab for buffering
#define TRANSFER_SIZE		       14
#define USER_CONFIG_SIZE	       14 // Size of data to receive from CH32V305 Slave
// The slab holds the actual buffers, replacing the dynamically allocated memory
K_MEM_SLAB_DEFINE(tx_slab, TRANSFER_SIZE, SLAB_BLOCK_COUNT, 4);
// The message queue passes pointers (void*) to the slab blocks, eliminating copies
K_MSGQ_DEFINE(slab_ptr_msgq, sizeof(void *), SLAB_BLOCK_COUNT, 4);

static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));

static struct spi_config spi_cfg = {.frequency = 8000000,
				    .operation = SPIOP,
				    .slave = 0, // Use chip select 0
				    .cs = {
					    .delay = 0,
					    .gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(master)),
				    }};

struct spi_dt_spec spispec;

/* Initialize in setup function */
void setup_spi(void)
{
	spispec.bus = spi_dev;
	spispec.config = spi_cfg;
	// spispec.config.cs.gpio = cs_gpio;
}

// K_MSGQ_DEFINE(spi_msgq, 6, 1024, 4); // Removed - replaced by slab_ptr_msgq

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY	  K_PRIO_COOP(7)
// #define THREAD_PRIORITY	 K_PRIO_COOP(1)
static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

#define RX_THREAD_PRIORITY K_PRIO_COOP(5) // Lower priority than TX? Or same.
static K_THREAD_STACK_DEFINE(rx_thread_stack, THREAD_STACK_SIZE);
static struct k_thread rx_thread;

static const struct device *gpio0_dev;
static struct gpio_callback data_ready_cb_data;
static K_SEM_DEFINE(data_ready_sem, 0, 1); // Start with 0 (locked)
// Signal for RX Interrupt to wake up the main thread
static struct k_poll_signal rx_signal = K_POLL_SIGNAL_INITIALIZER(rx_signal);

/* Timings (in us) used in SPI communication. Since MCU should not do other tasks during wait,
 * k_busy_wait is used instead of k_sleep */
// - sub-us time is rounded to us, due to the limitation of k_busy_wait, see :
// https://github.com/zephyrproject-rtos/zephyr/issues/6498
#define T_NCS_SCLK    1 /* 120 ns (rounded to 1us) */
#define T_SRX	      2 /* 2 us */
#define T_SCLK_NCS_WR 1 /* 1 us for write operation */
#define T_SWX	      5 /* 5 us */
#define T_SWR	      5 /* 5 us */
#define T_SWW	      5 /* 5 us */
#define T_SRAD	      2 /* 2 us */
#define T_SRAD_MOTBR  2 /* same as T_SRAD */
#define T_BEXIT	      1 /* 500 ns (rounded to 1us)*/
/* write command bit position */
#define SPI_WRITE_BIT BIT(7)
/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */

// checked and keep
static int spi_cs_ctrl(const struct device *dev, bool enable)
{
	int err;

	if (!enable) {
		k_busy_wait(T_NCS_SCLK);
	}

	err = gpio_pin_set_dt(&cs_gpio, (int)enable);
	if (err) {
		LOG_ERR("SPI CS ctrl failed");
	}

	if (enable) {
		k_busy_wait(T_NCS_SCLK);
	}

	return err;
}

// checked and keep
static int reg_write(const struct device *dev, uint8_t val)
{
	int err;

	// __ASSERT_NO_MSG((reg & SPI_WRITE_BIT) == 0);

	err = spi_cs_ctrl(dev, true);
	if (err) {
		return err;
	}

	uint8_t buf[] = {// SPI_WRITE_BIT | reg,
			 val};
	const struct spi_buf tx_buf = {.buf = buf, .len = ARRAY_SIZE(buf)};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

	err = spi_write(dev, &spi_cfg, &tx);
	if (err) {
		LOG_ERR("Reg write failed on SPI write");
		return err;
	}

	k_busy_wait(T_SRAD);

	err = spi_cs_ctrl(dev, false);
	if (err) {
		return err;
	}

	k_busy_wait(T_SRX);

	/* data->last_read_burst = false; */

	return 0;
}

/** Writing an array of registers in sequence, used in power-up register initialization and running
 * mode switching */
static int burst_write(const struct device *dev, const uint8_t *buf, size_t size)
{
	int err;

	/* Write data */
	for (size_t i = 0; i < size; i++) {
		err = reg_write(dev, buf[i]);

		if (err) {
			LOG_ERR("Burst write failed on SPI write (data)");
			return err;
		}
	}

	/* struct pixart_data *data = dev->data; */
	/* data->last_read_burst = false; */

	return 0;
}

static int optimized_burst_write(const struct device *dev, const uint8_t *buf, size_t size)
{
	// The Zephyr API handles the CS line and the entire multi-byte transaction automatically.
	int err = 0;
	// err = spi_cs_ctrl(dev, true); // Manually enable CS if needed (optional, depending on
	// driver) if (err) { 	LOG_ERR("Failed to enable CS"); 	return err;
	// }

	const struct spi_buf tx_buf = {
		.buf = (void *)buf, // Pointer to the slab block memory
		.len = size	    // Should be 6
	};

	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	// This uses the memory from the slab directly, eliminating a copy here.
	err = spi_write(dev, &spi_cfg, &tx);

	if (err) {
		LOG_ERR("Optimized burst write failed (%d)", err);
	}
	// err = spi_cs_ctrl(dev, false); // Manually disable CS if needed (optional, depending on
	// driver) if (err) { 	LOG_ERR("Failed to enable CS"); 	return err;
	// }

	return err;
}

/* * Writes data to the SPI Slave.
 * Replaces spi_write with spi_transceive_dt for write-only operations.
 */
static int optimized_burst_write_with_transceive(const struct spi_dt_spec *spec, const uint8_t *buf,
						 size_t size)
{
	if (size == 0 || buf == NULL || spec == NULL) {
		return -EINVAL; // Invalid argument
	}
	// 1. Prepare TX Buffer
	const struct spi_buf tx_buf = {.buf = (void *)buf, .len = size};

	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

	uint8_t dummy_rx[size]; // Dummy RX buffer to satisfy the API

	const struct spi_buf rx_buf = {.buf = dummy_rx, .len = size};

	const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1};

	// 2. Transceive (Write Only)
	// The Zephyr API handles the CS line automatically via the spi_dt_spec.
	// We pass NULL for the RX buffer set since we are only writing.
	int err = spi_transceive_dt(spec, &tx, &rx);

	if (err) {
		LOG_ERR("SPI write failed (%d)", err);
	}

	return err;
}

/* * Reads data from the SPI Slave.
 * Sends dummy bytes (handled by NULL tx buffer in some controllers,
 * or you might need to send 0x00s explicitly depending on controller behavior).
 * For strictly reading, usually passing NULL for TX works, but the safest way
 * to ensure clocking is often to send 0x00s if the hardware requires it.
 */
static int optimized_burst_read(const struct spi_dt_spec *spec, uint8_t *buf, size_t size)
{
	if (size == 0 || buf == NULL || spec == NULL) {
		return -EINVAL; // Invalid argument
	}
	// 1. Prepare RX Buffer
	const struct spi_buf rx_buf = {.buf = buf, .len = size};

	const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1};

	uint8_t dummy_rx[size];			      // Dummy TX buffer if needed
	dummy_rx[0] = REPORT_ID_SPI_MASTER_READ_ONLY; // Initialize to zero

	// Prepare TX Buffer with dummy bytes if needed
	const struct spi_buf tx_buf = {.buf = (void *)dummy_rx, .len = size};

	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

	// 2. Transceive (Read Only)
	// Pass NULL for TX. The SPI driver will send idle bytes (usually 0x00)
	// to generate the clock for reading.
	int err = spi_transceive_dt(spec, &tx, &rx);

	if (err) {
		LOG_ERR("SPI read failed (%d)", err);
	}

	return err;
}

void print_buffer(char *title, uint8_t *buf, size_t len)
{
	// LOG_INF("%s", title);
	// LOG_HEXDUMP_INF(buf, len, title);
	// LOG_INF("%s: %02x %02x ", title, buf[3], buf[9]);
	LOG_HEXDUMP_INF(buf, len, title);
}

// Removed: buffer_pool_send_data

// Removed: timer0_handler

// static K_TIMER_DEFINE(timer0, timer0_handler, NULL); // Removed

// static int init(void)
// {
// 	LOG_INF("Start to spi master");
// 	int err;

// 	if (!device_is_ready(spi_dev)) {
// 		LOG_ERR("Error: SPI device is not ready");
// 		return 0;
// 	}

// 	if (!gpio_is_ready_dt(&cs_gpio)) {
// 		LOG_ERR("Error: CS GPIO device port: %s is not ready", cs_gpio.port->name);
// 		return 0;
// 	}

// 	err = gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
// 	if (err) {
// 		LOG_ERR("Cannot configure SPI CS GPIO, err: %d", err);
// 		return err;
// 	}

// 	setup_spi();

// 	if (!spi_is_ready_dt(&spispec)) {
// 		LOG_ERR("SPI device or CS GPIO not ready");
// 		return -ENODEV;
// 	}

// 	LOG_INF("Succeed to init spi master");

// 	return 0;
// }

void data_ready_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	// // Give semaphore to wake up the RX thread
	// LOG_ERR("Data Ready Interrupt Triggered on pin %d", CONFIG_DESKTOP_SPI_INTERRUPT_PIN);
	// k_sem_give(&data_ready_sem);
	// Signal the main thread using k_poll_signal
	// This is safer and faster than semaphores for this event loop structure
	k_poll_signal_raise(&rx_signal, 1);
}

static int init(void)
{
	LOG_INF("Start to spi master");
	int err;

	if (!device_is_ready(spi_dev)) {
		LOG_ERR("Error: SPI device is not ready");
		return 0;
	}

	if (!gpio_is_ready_dt(&cs_gpio)) {
		LOG_ERR("Error: CS GPIO device port: %s is not ready", cs_gpio.port->name);
		return 0;
	}

	err = gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Cannot configure SPI CS GPIO, err: %d", err);
		return err;
	}

	setup_spi();

	if (!spi_is_ready_dt(&spispec)) {
		LOG_ERR("SPI device or CS GPIO not ready");
		return -ENODEV;
	}

	// --- Initialize GPIO Interrupt for RX ---
	gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	if (!device_is_ready(gpio0_dev)) {
		LOG_ERR("GPIO0 device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure(gpio0_dev, CONFIG_DESKTOP_SPI_INTERRUPT_PIN, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error configuring GPIO P0.%d: %d", CONFIG_DESKTOP_SPI_INTERRUPT_PIN, err);
		return err;
	}

	err = gpio_pin_interrupt_configure(gpio0_dev, CONFIG_DESKTOP_SPI_INTERRUPT_PIN,
					   GPIO_INT_EDGE_FALLING);
	if (err != 0) {
		LOG_ERR("Error configuring interrupt: %d", err);
		return err;
	}

	gpio_init_callback(&data_ready_cb_data, data_ready_handler,
			   BIT(CONFIG_DESKTOP_SPI_INTERRUPT_PIN));
	gpio_add_callback(gpio0_dev, &data_ready_cb_data);

	// Initialize the k_poll_signal
	k_poll_signal_init(&rx_signal);

	LOG_INF("Succeed to init spi master");

	return 0;
}

/* Polling Rate Callback */
static void spi_8k_callback(void)
{
	// Signal main thread with result code 2 for Timer Tick
	k_poll_signal_raise(&rx_signal, 2);
}

static int setup_polling_rate(void)
{
	int err = polling_rate_init();
	if (err) {
		LOG_ERR("Failed to init polling rate: %d", err);
		return err;
	}
	polling_rate_set_callback(spi_8k_callback);
	polling_rate_set_hz(8000);
	return polling_rate_start();
}

// Removed: write_data_in_buffer_pool

// Removed: handle_hid_report_event

// static void thread_fn(void)
// {
// 	int err = init();
// 	if (err) {
// 		module_set_state(MODULE_STATE_ERROR);
// 		return;
// 	} else {
// 		module_set_state(MODULE_STATE_READY);
// 	}

// 	// The pointer to the allocated slab block will be stored here.
// 	void *slab_block_ptr;

// 	while (!err) {
// 		// Wait indefinitely for a pointer to a slab block containing data
// 		// This replaces k_msgq_get which caused Copy 3.
// 		if (!k_msgq_get(&slab_ptr_msgq, &slab_block_ptr, K_FOREVER)) {
// 			// slab_block_ptr now points directly to the data buffer (zero-copy
// retrieval)

// 			print_buffer("SPI TX from SLAB:", (uint8_t *)slab_block_ptr, TRANSFER_SIZE);

// 			// Write the data to SPI directly from the slab block
// 			// err = optimized_burst_write(spi_dev, (uint8_t *)slab_block_ptr,
// REPORT_DATA_SIZE); 			err = optimized_burst_write_with_transceive(&spispec,
// (uint8_t
// *)slab_block_ptr, TRANSFER_SIZE);
// 			// LOG_HEXDUMP_INF((uint8_t *)slab_block_ptr, TRANSFER_SIZE, "SPI TX
// Data:");
// 			// The data has been sent, so free the memory slab block
// 		    k_mem_slab_free(&tx_slab, slab_block_ptr);

// 			if (err) {
// 				LOG_ERR("Failed to write SPI data (%d)", err);
// 			}
// 		}
// 	}
// 	module_set_state(MODULE_STATE_ERROR);
// }

static void thread_fn(void)
{
	int err = init();
	if (err) {
		module_set_state(MODULE_STATE_ERROR);
		return;
	} else {
		module_set_state(MODULE_STATE_READY);
	}

	setup_polling_rate();

	void *slab_block_ptr;
	uint8_t user_config_rx[USER_CONFIG_SIZE];

	// --- Polling Events Definition ---
	// Event 0: MSGQ Data Available (TX)
	// Event 1: Signal Raised (RX Interrupt)
	struct k_poll_event events[2] = {
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &slab_ptr_msgq, 0),
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
						&rx_signal, 0),
	};

	static void *pending_dual_sample_ptr = NULL;

	while (1) {
		// Wait indefinitely for EITHER a TX request OR an RX Interrupt
		// This serializes access to the SPI bus efficiently.
		// NOTE: With 8K Loop, we primarily wait for the Timer Signal (reusing rx_signal
		// with code 2)
		int poll_ret = k_poll(events, 2, K_FOREVER);
		if (poll_ret == 0) {

			// --- CHECK FOR SIGNAL EVENTS (Priority 1) ---
			// We handle both RX Interrupts and 8K Timer Ticks here
			if (events[1].state == K_POLL_STATE_SIGNALED) {
				unsigned int signaled_state;
				int result;
				k_poll_signal_check(&rx_signal, &signaled_state, &result);
				k_poll_signal_reset(&rx_signal);

				// --- 8K TIMER TICK (Result Code 2) ---
				// This is the "Pulse" of our 8K system
				if (result == 2) {
					// 1. Check if we have a pending 2nd half of a Dual Report
					if (pending_dual_sample_ptr != NULL) {
						// Send the 2nd half (Bytes 7-12)
						// The buffer pointer still points to the START of
						// the 14-byte block
						uint8_t *full_buf =
							(uint8_t *)pending_dual_sample_ptr;

						// Construct 2nd Report: ID (0x01) + Data (Bytes
						// 7-12) We can use a small stack buffer since we
						// just need to send 7 bytes
						uint8_t report_2[TRANSFER_SIZE]; // ID + 6 Data
						report_2[0] = 0x01; // Force standard mouse ID
						memcpy(&report_2[1], &full_buf[7], 6);

						err = optimized_burst_write_with_transceive(
							&spispec, report_2, TRANSFER_SIZE);

						if (err) {
							LOG_ERR("Failed to write SPI 2nd Report "
								"(%d)",
								err);
						} else {
							// LOG_DBG("TX Split 2nd Half");
						}

						// NOW we are done with the slab block
						k_mem_slab_free(&tx_slab, pending_dual_sample_ptr);
						pending_dual_sample_ptr = NULL;
					}
					// 2. No pending sample, fetch NEW data from Queue
					else {
						// Non-blocking fetch
						if (k_msgq_get(&slab_ptr_msgq, &slab_block_ptr,
							       K_NO_WAIT) == 0) {
							uint8_t *buf = (uint8_t *)slab_block_ptr;

							// Check Report Type
							if (buf[0] ==
							    REPORT_ID_DUAL_MOUSE) { // 0x20
								// Dual Report: Send 1st half NOW,
								// Store 2nd half for LATER
								// LOG_DBG("RX Dual Report");

								uint8_t report_1[TRANSFER_SIZE];
								report_1[0] = 0x01; // Force ID 1
								memcpy(&report_1[1], &buf[1],
								       6); // Bytes 1-6

								err = optimized_burst_write_with_transceive(
									&spispec, report_1,
									TRANSFER_SIZE);

								if (err) {
									LOG_ERR("Failed to write "
										"SPI 1st Report "
										"(%d)",
										err);
								}

								// Store pointer for next tick
								pending_dual_sample_ptr =
									slab_block_ptr;
								// Do NOT free slab yet

							} else {
								// Standard Single Packet (e.g. ID
								// 0x01 or Config) Just send it all
								// (we assume max 14 bytes transfer
								// size is fine or match length)
								// Actually, standard mouse is 7
								// bytes. We should probably check
								// length or just send safe amount.
								// Default mouse report is 7 bytes.
								// NOTE:
								// optimized_burst_write_with_transceive
								// sends FIXED size "TRANSFER_SIZE"
								// (14)?? No, it takes `size` arg.
								// The 'buf' from slab is 14 bytes
								// (TRANSFER_SIZE).

								// Let's deduce size from ID?
								// Or just send 7 bytes if ID is
								// 0x01?
								size_t tx_size = 14;
								if (buf[0] == 0x01) {
									tx_size = 7;
								}

								err = optimized_burst_write_with_transceive(
									&spispec, buf,
									TRANSFER_SIZE);

								if (err) {
									LOG_ERR("Failed to write "
										"SPI Single Report "
										"(%d)",
										err);
								}

								k_mem_slab_free(&tx_slab,
										slab_block_ptr);
							}
						}
					}
				}

				// --- RX INTERRUPT (Result Code 1) ---
				else if (result == 1) {
					// Short delay to allow Slave to prepare data
					// k_busy_wait(1000); // reduced wait or remove if polling
					// is fast enough

					err = optimized_burst_read(&spispec, user_config_rx,
								   USER_CONFIG_SIZE);
					// ... (Keep existing RX logic) ...

					// RX Logic Copy-Paste for context preservation
					// (abbreviated) Verify result == 1 logic structure in final
					// code
					if (!err) {
						// ... handle RX ... (Keep existing logic here)
					}
					// (Re-inserting logic below)
					uint8_t report_id = user_config_rx[0];
					uint8_t recipient = user_config_rx[1];

					if (!err && report_id == 0x00) {
						// Retry logic
						k_busy_wait(500);
						err = optimized_burst_read(&spispec, user_config_rx,
									   USER_CONFIG_SIZE);
					}
					if (!err && report_id == REPORT_ID_USER_CONFIG) {
						if (recipient == 0x00) {
							esb_write(user_config_rx, USER_CONFIG_SIZE);
						} else if (recipient == 0x01) {
							enter_pairing_mode();
						}
					}
				}

				// Reset event state for next poll
				events[1].state = K_POLL_STATE_NOT_READY;
			}

			// EVENT 0 (MSGQ) is IGNORED in purely 8K Timer Mode?
			// Actually, removing Event 0 from k_poll array entirely is cleaner if we
			// rely solely on timer. But sticking to the loop structure: we just don't
			// handle Event 0 block specifically since we check MSGQ inside the Timer
			// block.
		}
	}
	module_set_state(MODULE_STATE_ERROR);
}

/** * Receive User Configuration from Slave
 * Reads 30 bytes (USER_CONFIG_SIZE) from the slave.
 * Note: Since SPI is full-duplex, the master will send dummy bytes (0x00) while reading.
 */
static int spi_receive_user_config(const struct device *dev, uint8_t *rx_buf, size_t size)
{
	// We only provide an RX buffer. Zephyr's spi_read will transmit 0x00 bytes automatically.
	const struct spi_buf rx_bufs = {.buf = rx_buf, .len = size};
	const struct spi_buf_set rx = {.buffers = &rx_bufs, .count = 1};

	int err = spi_read(dev, &spi_cfg, &rx);

	if (err) {
		LOG_ERR("SPI receive failed (%d)", err);
	}

	return err;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	// Removed subscription to is_hid_report_event

	if (is_module_state_event(aeh)) {
		struct module_state_event *event = cast_module_state_event(aeh);

		if (check_state(event, MODULE_ID(esb_rx), MODULE_STATE_READY)) {
			k_thread_create(&thread, thread_stack, THREAD_STACK_SIZE,
					(k_thread_entry_t)thread_fn, NULL, NULL, NULL,
					THREAD_PRIORITY, 0, K_NO_WAIT);
			k_thread_name_set(&thread, MODULE_NAME "_thread");

			// Create RX thread
			// k_thread_create(&rx_thread, rx_thread_stack,
			// 		THREAD_STACK_SIZE,
			// 		(k_thread_entry_t)rx_thread_fn,
			// 		NULL, NULL, NULL,
			// 		RX_THREAD_PRIORITY, 0, K_NO_WAIT);
			// k_thread_name_set(&rx_thread, MODULE_NAME "_rx_thread");

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
// APP_EVENT_SUBSCRIBE(MODULE, hid_report_event); // Removed subscription