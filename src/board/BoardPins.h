// Which board this firmware is for.
//
// SPDX-License-Identifier: MIT
#ifndef WARP_BOARD_BOARDPINS_H
#define WARP_BOARD_BOARDPINS_H

#if defined(WARP_BOARD_WAVESHARE_TOUCH_LCD7)

#include "board/waveshare_touch_lcd7/Pins.h"

#else
// Without a flag the build stops here rather than shipping one board's pin map
// to another panel.
#error "No board selected. Set -DWARP_BOARD_WAVESHARE_TOUCH_LCD7."
#endif

#endif  // WARP_BOARD_BOARDPINS_H
