/* esb_pairing_def.h */
#ifndef ESB_PAIRING_DEF_H
#define ESB_PAIRING_DEF_H

#include <zephyr/types.h>

/* Fixed "Doorbell" Address for Discovery */
#define BROADCAST_ADDR_BASE {0xE7, 0xE7, 0xE7, 0xE7}
#define BROADCAST_PREFIX    0xE7
#define DISCOVERY_CHANNEL   80
#define PAIRED_CHANNEL	    66
#define DEFAULT_CHANNEL	    16

/* Default Settings for Fallback */
#define DEFAULT_ADDR_BASE {0xC2, 0xC2, 0xC2, 0xC2}
#define DEFAULT_PREFIX	  0xC2

enum esb_packet_type {
	ESB_PKT_PAIR_REQ = 0xAA,
	ESB_PKT_PAIR_RSP = 0xBB
};

#define PAIRING_TIMEOUT_MS 60000 /* 60 seconds */

/* Mouse sends this */
struct esb_pairing_req {
	uint8_t type;	 // ESB_PKT_PAIR_REQ
	uint8_t hwid[8]; // 8-byte ID from hwid.h
	uint16_t vid;
	uint16_t pid;
} __packed;

/* Dongle replies with this */
struct esb_pairing_rsp {
	uint8_t type;	     // ESB_PKT_PAIR_RSP
	uint8_t new_base[4]; // Derived from HWID[0..3]
	uint8_t new_prefix;  // Derived from HWID[4]
	uint8_t new_channel; // Data Channel
} __packed;

struct esb_pairing_info {
	uint8_t addr[5]; /* 4-byte Base + 1-byte Prefix */
	uint8_t rf_channel;
	uint8_t paired_device_mac[6]; /* Optional: Store MAC of paired device if needed */
	bool is_paired;
};

#endif /* ESB_PAIRING_DEF_H */