# Out-of-tree paw3395 sensor driver module
# Vendored from C:\ncs\v3.0.2\nrf\drivers\sensor\paw3395 because the driver is
# not part of upstream sdk-nrf v3.0.2 (custom/NDA driver from PixArt).

# Expose the public header (consumers do `#include <sensor/paw3395.h>`).
zephyr_include_directories(${ZEPHYR_PAW3395_MODULE_DIR}/include)

add_subdirectory(${ZEPHYR_PAW3395_MODULE_DIR}/drivers/sensor/paw3395 drivers/sensor/paw3395)
