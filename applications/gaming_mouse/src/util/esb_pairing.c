#include "esb_pairing.h"
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/types.h>
#include <string.h>
#include <errno.h>
#include "hwid.h"

LOG_MODULE_REGISTER(esb_pairing, LOG_LEVEL_INF);

/* Global Pairing Manager Instance */
#define SETTINGS_PAIRING_KEY "esb/pairing"

/* Global Pairing Manager Instance */
static pairing_manager_t pairing_manager = {
	.state = PAIRING_IDLE,
	.config = {.base_addr = {0xC2, 0xC2, 0xC2, 0xC2}, // Default values
		   .prefix = 0xC2,
		   .channel = 82,
		   .timestamp = 0,
		   .valid = false},
	.is_pairing_mode = false};

/* Timeout Handler Function */
static void pairing_timeout_handler(struct k_work *work)
{
	pairing_manager_t *manager = pairing_manager_get_instance();

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	if (manager->state == PAIRING_WAITING_RESPONSE) {
		LOG_WRN("Pairing timeout - no response received");
		manager->state = PAIRING_TIMEOUT;
		manager->is_pairing_mode = false;
	}

	k_spin_unlock(&manager->lock, key);
}

/* Get Pairing Manager Instance */
pairing_manager_t *pairing_manager_get_instance(void)
{
	return &pairing_manager;
}

/* Initialize Pairing Manager */
int pairing_manager_init(void)
{
	int ret = 0;
	pairing_manager_t *manager = &pairing_manager;

	/* Initialize Mutex - No explicit init needed for spinlock generally,
	   but ensures it's zeroed if not already. */
	memset(&manager->lock, 0, sizeof(manager->lock));

	/* Initialize Timeout Work */
	k_work_init_delayable(&manager->timeout_work, pairing_timeout_handler);

	/* Initialize Settings Subsystem */
	ret = settings_subsys_init();
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("Failed to initialize settings subsystem: %d", ret);
		return ret;
	}

	/* Load Saved Configuration from Storage */
	ret = pairing_load_config();
	if (ret == 0 && manager->config.valid) {
		LOG_INF("Loaded pairing configuration from storage");
		LOG_HEXDUMP_INF(manager->config.base_addr, 4, "Base address:");
		LOG_INF("Prefix: 0x%02X, Channel: %d", manager->config.prefix,
			manager->config.channel);
	} else {
		LOG_INF("No valid pairing configuration found");
	}

	LOG_INF("Pairing manager initialized, state: %d", manager->state);
	return 0;
}

/* Start Pairing Process */
int pairing_start(uint8_t *hwid)
{
	int ret = 0;
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	/* Check Current State */
	if (manager->state != PAIRING_IDLE && manager->state != PAIRING_FAILED &&
	    manager->state != PAIRING_TIMEOUT) {
		LOG_WRN("Cannot start pairing, current state: %d", manager->state);
		ret = -EBUSY;
		goto exit;
	}

	/* Save HWID */
	if (hwid != NULL) {
		memcpy(manager->current_hwid, hwid, sizeof(manager->current_hwid));
	} else {
		LOG_ERR("HWID is NULL");
		ret = -EINVAL;
		goto exit;
	}

	/* Update State */
	manager->state = PAIRING_WAITING_RESPONSE;
	manager->is_pairing_mode = true;

	/* Start Timeout Timer */
	k_work_reschedule(&manager->timeout_work, K_MSEC(PAIRING_TIMEOUT_MS));

	LOG_INF("Pairing started, timeout in %d ms", PAIRING_TIMEOUT_MS);
	LOG_HEXDUMP_INF(manager->current_hwid, 8, "HWID for pairing:");

exit:
	k_spin_unlock(&manager->lock, key);
	return ret;
}

/* Handle Pairing Response */
int pairing_handle_response(uint8_t *response_data, size_t response_len)
{
	int ret = 0;
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	/* Check State */
	if (manager->state != PAIRING_WAITING_RESPONSE) {
		LOG_WRN("Unexpected pairing response, state: %d", manager->state);
		ret = -EINVAL;
		goto exit;
	}

	/* Verify Response Length (see struct esb_pairing_rsp in esb_pairing_def.h) */
	if (response_len != sizeof(struct esb_pairing_rsp)) {
		LOG_WRN("Invalid response length: %d (expected %d)", response_len,
			sizeof(struct esb_pairing_rsp));
		ret = -EINVAL;
		goto exit;
	}

	/* Parse Response Data */
	struct esb_pairing_rsp *rsp = (struct esb_pairing_rsp *)response_data;
	if (rsp->type != ESB_PKT_PAIR_RSP) {
		LOG_WRN("Invalid response type: 0x%02X", rsp->type);
		ret = -EINVAL;
		goto exit;
	}

	/* Extract Configuration Information */
	memcpy(manager->config.base_addr, rsp->new_base, 4);
	manager->config.prefix = rsp->new_prefix;
	manager->config.channel = rsp->new_channel;
	manager->config.timestamp = k_uptime_get_32();
	manager->config.valid = true;

	/* Update State */
	manager->state = PAIRING_COMPLETE;
	manager->is_pairing_mode = false;

	/* Cancel Timeout Timer */
	k_work_cancel_delayable(&manager->timeout_work);

exit:
	k_spin_unlock(&manager->lock, key);

	/* Save Configuration to Storage (Outside spinlock) */
	if (ret == 0) {
		ret = pairing_save_config();
		if (ret != 0) {
			LOG_WRN("Failed to save pairing config: %d", ret);
		}
	}

	return ret;
}

/* Get Current Pairing State */
pairing_state_t pairing_get_state(void)
{
	pairing_state_t state;
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);
	state = manager->state;
	k_spin_unlock(&manager->lock, key);

	return state;
}

/* Check if Pairing is Active */
bool pairing_is_active(void)
{
	bool active;
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);
	active = manager->is_pairing_mode;
	k_spin_unlock(&manager->lock, key);

	return active;
}

/* Get Pairing Configuration */
pairing_config_t *pairing_get_config(void)
{
	return &pairing_manager.config;
}

/* Apply Pairing Configuration to ESB (Placeholder) */
int pairing_apply_config(void)
{
	pairing_manager_t *manager = &pairing_manager;
	int ret = 0;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	if (!manager->config.valid) {
		LOG_WRN("No valid pairing configuration to apply");
		ret = -ENODATA;
	}

	/* Configuration application logic should be handled by the caller (esb_driver) */
	k_spin_unlock(&manager->lock, key);
	return ret;
}

/* Clear Pairing Information */
int pairing_clear(void)
{
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	/* Clear Configuration */
	memset(&manager->config, 0, sizeof(manager->config));
	manager->config.valid = false;

	/* Reset State */
	manager->state = PAIRING_IDLE;
	manager->is_pairing_mode = false;

	/* Cancel Timeout Timer */
	k_work_cancel_delayable(&manager->timeout_work);

	k_spin_unlock(&manager->lock, key);

	/* Delete from Storage (Outside spinlock) */
	settings_delete(SETTINGS_PAIRING_KEY);

	LOG_INF("Pairing information cleared");

	return 0;
}

/* Reset Pairing State */
void pairing_reset(void)
{
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	if (manager->state == PAIRING_WAITING_RESPONSE) {
		/* Cancel Timeout Timer */
		k_work_cancel_delayable(&manager->timeout_work);
	}

	manager->state = PAIRING_IDLE;
	manager->is_pairing_mode = false;

	LOG_INF("Pairing state reset to IDLE");

	k_spin_unlock(&manager->lock, key);
}

/* Check if Configuration Should Be Saved */
bool pairing_should_save_config(void)
{
	bool should_save;
	pairing_manager_t *manager = &pairing_manager;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);
	should_save = (manager->state == PAIRING_COMPLETE && manager->config.valid);
	k_spin_unlock(&manager->lock, key);

	return should_save;
}

/* Save Pairing Configuration to Storage */
int pairing_save_config(void)
{
	pairing_manager_t *manager = &pairing_manager;
	pairing_config_t temp_config;
	int ret;

	k_spinlock_key_t key = k_spin_lock(&manager->lock);

	if (!manager->config.valid) {
		LOG_WRN("No valid config to save");
		k_spin_unlock(&manager->lock, key);
		return -ENODATA;
	}

	/* Copy to temporary buffer to save outside spinlock */
	memcpy(&temp_config, &manager->config, sizeof(temp_config));
	k_spin_unlock(&manager->lock, key);

	ret = settings_save_one(SETTINGS_PAIRING_KEY, &temp_config, sizeof(temp_config));
	if (ret == 0) {
		LOG_INF("Pairing configuration saved to storage");
	} else {
		LOG_ERR("Failed to save pairing config: %d", ret);
	}

	return ret;
}

/* Load Pairing Configuration from Storage */
int pairing_load_config(void)
{
	/* settings_load_subtree triggers pairing_settings_load callback,
	   which handles its own locking */
	int ret = settings_load_subtree(SETTINGS_PAIRING_KEY);
	if (ret == 0) {
		LOG_INF("Pairing configuration loaded from storage");
	} else {
		LOG_WRN("Failed to load pairing config: %d", ret);
	}

	return ret;
}

/* Settings Load Callback */
static int pairing_settings_load(const char *name, size_t len, settings_read_cb read_cb,
				 void *cb_arg)
{
	pairing_manager_t *manager = &pairing_manager;
	pairing_config_t temp_config;
	int ret;

	if (name != NULL && name[0] != '\0') {
		/* We are handling "esb/pairing" directly as a leaf value,
		   so we don't expect sub-keys. */
		return 0;
	}

	if (len != sizeof(temp_config)) {
		return -EINVAL;
	}

	ret = read_cb(cb_arg, &temp_config, sizeof(temp_config));
	if (ret > 0) {
		k_spinlock_key_t key = k_spin_lock(&manager->lock);
		memcpy(&manager->config, &temp_config, sizeof(manager->config));
		k_spin_unlock(&manager->lock, key);
		ret = 0;
	}

	return ret;
}

/* Settings Static Handler Definition */
SETTINGS_STATIC_HANDLER_DEFINE(esb_pairing, SETTINGS_PAIRING_KEY, NULL, pairing_settings_load, NULL,
			       NULL);

void esb_pairing_gen_address(uint8_t *addr_buf)
{
	if (!addr_buf) {
		return;
	}
	/* Use HWID */
	uint8_t id[8];
	hwid_get(id, sizeof(id));
	/* Use first 5 bytes */
	memcpy(addr_buf, id, 5);
}

bool esb_pairing_is_valid_req(const struct esb_pairing_req *req)
{
	if (!req) {
		return false;
	}
	if (req->type != ESB_PKT_PAIR_REQ) {
		return false;
	}
	return true;
}

void pairing_set_config(const pairing_config_t *config)
{
	if (!config) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&pairing_manager.lock);
	memcpy(&pairing_manager.config, config, sizeof(pairing_config_t));
	pairing_manager.config.valid = true;
	pairing_manager.config.timestamp = k_uptime_get_32();
	k_spin_unlock(&pairing_manager.lock, key);
}
