/* REQ-USB-002: vendor HID protocol — encode/decode helpers */
#include <stddef.h>
#include <stdint.h>
#include "protocol_version.h"

uint8_t ocor_protocol_version(void)
{
	return OCOR_PROTOCOL_VERSION;
}

/* REQ-DPI-001: validate DPI value before applying */
int ocor_dpi_is_valid(uint16_t dpi)
{
	if (dpi < 100 || dpi > 32000) {
		return 0;
	}
	if (dpi % 50 != 0) {
		return 0;
	}
	return 1;
}
