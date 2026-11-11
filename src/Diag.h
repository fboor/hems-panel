// Phase marker used by the display driver.
//
// The HEMS panel does not run the heartbeat task the marker original came from;
// the calls stay in the copied display code, so this header keeps them free and
// does nothing. Replace with a real implementation when a watchdog is added.
//
// SPDX-License-Identifier: MIT
#pragma once

inline void diagPhase(const char *) {}
