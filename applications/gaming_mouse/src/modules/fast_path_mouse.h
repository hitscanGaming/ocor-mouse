/* fast_path_mouse.h */
#ifndef FAST_PATH_MOUSE_H
#define FAST_PATH_MOUSE_H

#include <zephyr/types.h>

/**
 * @brief Callback to be invoked by the Motion Sensor thread.
 * This is the "Trigger" for sending a packet at 4K/8K.
 */
void fast_path_motion_handler(int16_t dx, int16_t dy);

#endif /* FAST_PATH_MOUSE_H */