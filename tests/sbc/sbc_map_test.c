/*
 * Host test of the Steel Battalion mapping (native/app/ui/sbc_map.c) with a
 * fake keyboard and gamepad. Build (from the source root, SDL3's headers in
 * SDL3_INC):
 *   cc -std=gnu11 -I. -I$SDL3_INC -o /tmp/sbc_map_test tests/sbc/sbc_map_test.c native/app/ui/sbc_map.c && /tmp/sbc_map_test
 */
#include "native/app/ui/sbc_map.h"

#include <stdio.h>
#include <string.h>

static bool g_btn[SDL_GAMEPAD_BUTTON_COUNT];
static int g_axis[SDL_GAMEPAD_AXIS_COUNT];
bool SDL_GetGamepadButton(SDL_Gamepad *p, SDL_GamepadButton b) { (void)p; return g_btn[b]; }
Sint16 SDL_GetGamepadAxis(SDL_Gamepad *p, SDL_GamepadAxis a) { (void)p; return (Sint16)g_axis[a]; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static bool keys[SDL_SCANCODE_COUNT];
static SDL_Gamepad *const PAD = (SDL_Gamepad *)1;

int main(void)
{
    NcSbcState s;
    sbc_read(keys, NULL, &s);
    CHECK(s.aim_x == 0x8000 && s.aim_y == 0x8000 && s.gear == -1 && !s.buttons[0] && !s.pedal[2]);

    /* keyboard: Space fires (byte 2 0x02), the arrows aim, Q shifts up once a press */
    keys[SDL_SCANCODE_SPACE] = keys[SDL_SCANCODE_RIGHT] = true;
    sbc_read(keys, NULL, &s);
    CHECK(s.buttons[0] == 0x02 && s.aim_x == 0x8000 + 32767 && s.aim_y == 0x8000);
    keys[SDL_SCANCODE_SPACE] = keys[SDL_SCANCODE_RIGHT] = false;
    keys[SDL_SCANCODE_Q] = true;
    sbc_read(keys, NULL, &s);
    CHECK(s.gear == 1);
    sbc_read(keys, NULL, &s);           /* still held: no second step */
    CHECK(s.gear == 1);
    keys[SDL_SCANCODE_Q] = false;
    sbc_read(keys, NULL, &s);
    keys[SDL_SCANCODE_Q] = true;
    sbc_read(keys, NULL, &s);
    CHECK(s.gear == 2);
    keys[SDL_SCANCODE_Q] = false;
    sbc_read(keys, NULL, &s);
    keys[SDL_SCANCODE_E] = true;        /* down: 2nd -> 1st */
    sbc_read(keys, NULL, &s);
    CHECK(s.gear == 1);
    keys[SDL_SCANCODE_E] = false;
    sbc_read(keys, NULL, &s);
    keys[SDL_SCANCODE_E] = true;        /* 1st -> N */
    sbc_read(keys, NULL, &s);
    CHECK(s.gear == -1);
    keys[SDL_SCANCODE_E] = false;
    sbc_read(keys, NULL, &s);
    keys[SDL_SCANCODE_E] = true;        /* N -> R */
    sbc_read(keys, NULL, &s);
    CHECK(s.gear == -2);
    keys[SDL_SCANCODE_E] = false;

    /* a toggle switch stays where it was put */
    keys[SDL_SCANCODE_P] = true;
    sbc_read(keys, NULL, &s);
    CHECK(s.buttons[4] == 0x04);
    keys[SDL_SCANCODE_P] = false;
    sbc_read(keys, NULL, &s);
    CHECK(s.buttons[4] == 0x04);
    keys[SDL_SCANCODE_P] = true;
    sbc_read(keys, NULL, &s);
    CHECK(s.buttons[4] == 0);
    keys[SDL_SCANCODE_P] = false;

    /* a pedal comes down over a few polls and is full in the end */
    keys[SDL_SCANCODE_C] = true;
    sbc_read(keys, NULL, &s);
    CHECK(s.pedal[2] > 0 && s.pedal[2] < 0xffff);
    for (int i = 0; i < 8; i++) {
        sbc_read(keys, NULL, &s);
    }
    CHECK(s.pedal[2] == 0xffff);
    keys[SDL_SCANCODE_C] = false;
    for (int i = 0; i < 8; i++) {
        sbc_read(keys, NULL, &s);
    }
    CHECK(s.pedal[2] == 0);

    /* tuner: the period steps up, clamped at 12 */
    for (int i = 0; i < 15; i++) {
        keys[SDL_SCANCODE_PERIOD] = true;
        sbc_read(keys, NULL, &s);
        keys[SDL_SCANCODE_PERIOD] = false;
        sbc_read(keys, NULL, &s);
    }
    CHECK(s.tuner == 12);

    /* gamepad: A fires; the right stick aims past its dead zone; RT is the right pedal */
    g_btn[SDL_GAMEPAD_BUTTON_SOUTH] = true;
    g_axis[SDL_GAMEPAD_AXIS_RIGHTX] = 32767;
    g_axis[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
    g_axis[SDL_GAMEPAD_AXIS_LEFTX] = 3000;    /* inside the dead zone */
    for (int i = 0; i < 8; i++) {
        sbc_read(NULL, PAD, &s);
    }
    CHECK(s.buttons[0] == 0x02 && s.aim_x == 0x8000 + 32767 && s.rotation == 0 && s.pedal[2] == 0xffff);

    /* Back held: A is the ignition, not fire; Start is nothing */
    g_btn[SDL_GAMEPAD_BUTTON_BACK] = true;
    g_btn[SDL_GAMEPAD_BUTTON_START] = true;
    sbc_read(NULL, PAD, &s);
    CHECK(s.buttons[0] == 0x20);

    printf(fails ? "%d failed\n" : "all ok\n", fails);
    return fails != 0;
}
