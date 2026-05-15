#ifndef ESB_PAIRING_H
#define ESB_PAIRING_H

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/spinlock.h>
#include <stdbool.h>
#include "esb_pairing_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pairing States */
typedef enum {
	PAIRING_IDLE = 0,
	PAIRING_STARTED,
	PAIRING_WAITING_RESPONSE,
	PAIRING_COMPLETE,
	PAIRING_FAILED,
	PAIRING_TIMEOUT
} pairing_state_t;

/* Pairing Configuration Information */
typedef struct {
	uint8_t base_addr[4];
	uint8_t prefix;
	uint8_t channel;
	uint32_t timestamp;
	bool valid;
} pairing_config_t;

/* Pairing Manager Structure */
typedef struct {
	pairing_state_t state;
	pairing_config_t config;
	uint8_t current_hwid[8];
	struct k_spinlock lock;
	struct k_work_delayable timeout_work;
	bool is_pairing_mode;
} pairing_manager_t;

/* Initialize Pairing Manager */
int pairing_manager_init(void);

/* Helper to generate a random 4-byte base address (using HWID) */
void esb_pairing_gen_address(uint8_t *addr_buf);

/* Check if pairing request is valid */
bool esb_pairing_is_valid_req(const struct esb_pairing_req *req);

/* Set current pairing configuration (PRX side) */
void pairing_set_config(const pairing_config_t *config);

/* Get Pairing Manager Instance */
pairing_manager_t *pairing_manager_get_instance(void);

/* Start Pairing Process */
int pairing_start(uint8_t *hwid);

/* Handle Pairing Response */
int pairing_handle_response(uint8_t *response_data, size_t response_len);

/* Get Current Pairing State */
pairing_state_t pairing_get_state(void);

/* Check if Pairing is Active */
bool pairing_is_active(void);

/* Get Pairing Configuration */
pairing_config_t *pairing_get_config(void);

/* Apply Pairing Configuration to ESB (Placeholder for caller) */
int pairing_apply_config(void);

/* Clear Pairing Information */
int pairing_clear(void);

/* Reset Pairing State */
void pairing_reset(void);

/* Check if Configuration Should Be Saved */
bool pairing_should_save_config(void);

/* Save Pairing Configuration to Storage */
int pairing_save_config(void);

/* Load Pairing Configuration from Storage */
int pairing_load_config(void);

#ifdef __cplusplus
}
#endif

#endif /* ESB_PAIRING_H */
