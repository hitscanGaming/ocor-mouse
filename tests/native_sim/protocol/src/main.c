#include <zephyr/ztest.h>
#include "protocol_version.h"

extern int ocor_dpi_is_valid(unsigned short dpi);
extern unsigned char ocor_protocol_version(void);

ZTEST_SUITE(protocol, NULL, NULL, NULL, NULL, NULL);

ZTEST(protocol, test_protocol_version_nonzero)
{
	zassert_true(ocor_protocol_version() > 0, "protocol version should be > 0");
}

/* REQ-DPI-001 */
ZTEST(protocol, test_dpi_valid_within_range)
{
	zassert_true(ocor_dpi_is_valid(800), "800 should be valid");
	zassert_true(ocor_dpi_is_valid(1600), "1600 should be valid");
	zassert_true(ocor_dpi_is_valid(32000), "32000 (max) should be valid");
	zassert_true(ocor_dpi_is_valid(100), "100 (min) should be valid");
}

/* REQ-DPI-001 */
ZTEST(protocol, test_dpi_invalid_step)
{
	zassert_false(ocor_dpi_is_valid(123), "123 not aligned to 50");
}

/* REQ-DPI-001 */
ZTEST(protocol, test_dpi_out_of_range)
{
	zassert_false(ocor_dpi_is_valid(50), "below min");
	zassert_false(ocor_dpi_is_valid(40000), "above max");
}
