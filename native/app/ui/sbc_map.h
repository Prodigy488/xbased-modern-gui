/*
 * hakuX front end — the Steel Battalion controller from a keyboard and a
 * gamepad (sbc_map.c): its 40-odd buttons, two levers, sight stick, pedals,
 * tuner dial and gear lever, read from the keys (all of them) and the pad
 * (the ones the game is played with, the rest under the Back button).
 */
#ifndef NATIVE_APP_UI_SBC_MAP_H
#define NATIVE_APP_UI_SBC_MAP_H

#include "native/dev/usb.h"

#include <SDL3/SDL.h>

/* One poll (the controls' state is kept between polls: the gear lever, the tuner dial and the
 * toggle switches change on a press). keys: SDL_GetKeyboardState, or NULL for none; pad: or NULL. */
void sbc_read(const bool *keys, SDL_Gamepad *pad, NcSbcState *out);

/* The layout as text, for the README and the log. */
const char *sbc_layout_text(void);

/* Steel Battalion's title id. */
#define SBC_TITLE_STEEL_BATTALION 0x43430002u

#endif
