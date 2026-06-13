#ifndef BOOT_H
#define BOOT_H

#include "DEV_Config.h"   /* UBYTE */
#include <stdint.h>

/* Power-on boot splash: a 3D starfield "hyperdrive / light-speed" warp.
 *
 * Blocking — runs for `duration_ms`, animating into `image` (the current Paint
 * image, Scale=16) and pushing each frame to the OLED. Call once after the OLED
 * and Paint image are initialised, before the main UI starts.
 */
void boot_splash(UBYTE *image, uint32_t duration_ms);

#endif
