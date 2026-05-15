/*
 * esb_driver.h
 * Header file for Low-level ESB driver.
 * ONLY Declarations here. NO Code.
 */

#ifndef ESB_DRIVER_H
#define ESB_DRIVER_H

#include <zephyr/types.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "hid_report_desc.h"

/* Define the callback function type */
typedef void (*esb_driver_rx_cb_t)(uint8_t *data, size_t size);

/* Traffic Light Interface */
/* Set to true to pause mouse motion (for config), false to resume */
void esb_set_config_tx_pending(bool pending);

/* Check if config is pending (returns true if mouse should pause) */
bool esb_is_config_tx_pending(void);

/* Function Prototypes */

/* Init Hardware */
int esb_driver_init(void);

/* Suspend/Resume Radio */
int esb_driver_suspend(void);
int esb_driver_resume(void);

/* Send Data (Direct Fast Path) */
int esb_write_data(uint8_t *data, size_t size, bool ack_required);

/* Register callback for RX data (e.g., Config) */
void esb_driver_register_rx_cb(esb_driver_rx_cb_t cb);

/* Get last activity for PM */
uint32_t esb_get_last_activity(void);

#define _ESB_HID_BUF_ALIGN (sizeof(void *))
#define _ESB_HID_BUF_SIZE                                                                          \
	(ROUND_UP(REPORT_ID_SIZE + REPORT_BUFFER_SIZE_INPUT_REPORT, sizeof(void *)))
#define ESB_SUBSCRIBER_PIPELINE_SIZE 1
#define ESB_SUBSCRIBER_REPORT_MAX    ESB_SUBSCRIBER_PIPELINE_SIZE
struct esb_hid_buf {
	uint8_t data[_ESB_HID_BUF_SIZE];
	uint8_t size;
	uint8_t status_bm;
} __aligned(_ESB_HID_BUF_ALIGN);
struct esb_hid_device_t {
	const struct device *dev;
	uint32_t report_bm; // hid report id
	struct esb_hid_buf report_bufs[ESB_SUBSCRIBER_REPORT_MAX];
	atomic_ptr_t report_sent_on_sof;
	uint32_t idle_duration[REPORT_ID_COUNT];
	uint8_t hid_protocol;
	bool report_enabled[REPORT_ID_COUNT];
	bool enabled;
};

int esb_init_hids_init(struct esb_hid_device_t *esb_hid_device);
void update_esb_hid(struct esb_hid_device_t *esb_hid, bool enabled);

/* Update ESB Address and Channel (for pairing) */
int esb_driver_update_pairing(const uint8_t *addr, uint8_t channel);
int esb_driver_set_address_default(void);

#endif /* ESB_DRIVER_H */