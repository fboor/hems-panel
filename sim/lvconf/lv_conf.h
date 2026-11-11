// LVGL configuration for the Home Energy Manager panel emulator.
//
// The firmware's own include/lv_conf.h is included verbatim so colours, fonts
// and heap are the same on both sides. Only the display driver is switched to
// SDL, plus the snapshot call the screenshot option needs.
//
// SPDX-License-Identifier: MIT
#ifndef HEMS_PANEL_SIM_LV_CONF_H
#define HEMS_PANEL_SIM_LV_CONF_H

#include "../../include/lv_conf.h"

#undef LV_USE_SDL
#define LV_USE_SDL 1
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT

#undef LV_USE_SNAPSHOT
#define LV_USE_SNAPSHOT 1

#endif  // HEMS_PANEL_SIM_LV_CONF_H
