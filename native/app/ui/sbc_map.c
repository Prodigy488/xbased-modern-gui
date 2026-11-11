/*
 * hakuX front end — the Steel Battalion controller from a keyboard and a
 * gamepad. See sbc_map.h and README.md ("Steel Battalion") for the layout.
 */
#include "native/app/ui/sbc_map.h"

#include <stdlib.h>
#include <string.h>

/* A button or switch: the report's byte (2-6, held as 0-4) and its bit. */
#define BTN(byte, mask) (((byte) - 2) << 8 | (mask))
enum {
    B_MAIN_WEAPON = BTN(2, 0x01), B_FIRE = BTN(2, 0x02), B_LOCK_ON = BTN(2, 0x04), B_EJECT = BTN(2, 0x08),
    B_HATCH = BTN(2, 0x10), B_IGNITION = BTN(2, 0x20), B_START = BTN(2, 0x40), B_MON_OPEN = BTN(2, 0x80),
    B_MAP_ZOOM = BTN(3, 0x01), B_MODE = BTN(3, 0x02), B_SUB_MON = BTN(3, 0x04), B_ZOOM_IN = BTN(3, 0x08),
    B_ZOOM_OUT = BTN(3, 0x10), B_FSS = BTN(3, 0x20), B_MANIP = BTN(3, 0x40), B_LINE_COLOR = BTN(3, 0x80),
    B_WASHING = BTN(4, 0x01), B_EXTING = BTN(4, 0x02), B_CHAFF = BTN(4, 0x04), B_DETACH = BTN(4, 0x08),
    B_OVERRIDE = BTN(4, 0x10), B_NIGHT = BTN(4, 0x20), B_F1 = BTN(4, 0x40), B_F2 = BTN(4, 0x80),
    B_F3 = BTN(5, 0x01), B_WEAPON_MAIN = BTN(5, 0x02), B_WEAPON_SUB = BTN(5, 0x04), B_WEAPON_MAG = BTN(5, 0x08),
    B_COMM1 = BTN(5, 0x10), B_COMM2 = BTN(5, 0x20), B_COMM3 = BTN(5, 0x40), B_COMM4 = BTN(5, 0x80),
    B_COMM5 = BTN(6, 0x01), B_SIGHT = BTN(6, 0x02), T_FILTER = BTN(6, 0x04), T_OXYGEN = BTN(6, 0x08),
    T_FUEL = BTN(6, 0x10), T_BUFFRE = BTN(6, 0x20), T_VT = BTN(6, 0x40),
};

static const struct {
    SDL_Scancode key;
    int btn;
} k_keys[] = {
    {SDL_SCANCODE_LSHIFT, B_MAIN_WEAPON}, {SDL_SCANCODE_SPACE, B_FIRE}, {SDL_SCANCODE_LCTRL, B_LOCK_ON},
    {SDL_SCANCODE_DELETE, B_EJECT}, {SDL_SCANCODE_R, B_HATCH}, {SDL_SCANCODE_T, B_IGNITION},
    {SDL_SCANCODE_RETURN, B_START}, {SDL_SCANCODE_1, B_MON_OPEN}, {SDL_SCANCODE_2, B_MAP_ZOOM},
    {SDL_SCANCODE_3, B_MODE}, {SDL_SCANCODE_4, B_SUB_MON}, {SDL_SCANCODE_EQUALS, B_ZOOM_IN},
    {SDL_SCANCODE_MINUS, B_ZOOM_OUT}, {SDL_SCANCODE_5, B_FSS}, {SDL_SCANCODE_6, B_MANIP},
    {SDL_SCANCODE_7, B_LINE_COLOR}, {SDL_SCANCODE_8, B_WASHING}, {SDL_SCANCODE_9, B_EXTING},
    {SDL_SCANCODE_0, B_CHAFF}, {SDL_SCANCODE_INSERT, B_DETACH}, {SDL_SCANCODE_HOME, B_OVERRIDE},
    {SDL_SCANCODE_PAGEUP, B_NIGHT}, {SDL_SCANCODE_END, B_F1}, {SDL_SCANCODE_PAGEDOWN, B_F2},
    {SDL_SCANCODE_APOSTROPHE, B_F3}, {SDL_SCANCODE_Y, B_WEAPON_MAIN}, {SDL_SCANCODE_U, B_WEAPON_SUB},
    {SDL_SCANCODE_O, B_WEAPON_MAG}, {SDL_SCANCODE_V, B_COMM1}, {SDL_SCANCODE_B, B_COMM2},
    {SDL_SCANCODE_N, B_COMM3}, {SDL_SCANCODE_M, B_COMM4}, {SDL_SCANCODE_SEMICOLON, B_COMM5},
    {SDL_SCANCODE_F, B_SIGHT},
};

/* The toggle switches stay where they were put: a press flips one. */
static const struct {
    SDL_Scancode key;
    int btn;
} k_toggles[] = {
    {SDL_SCANCODE_P, T_FILTER}, {SDL_SCANCODE_LEFTBRACKET, T_OXYGEN}, {SDL_SCANCODE_RIGHTBRACKET, T_FUEL},
    {SDL_SCANCODE_BACKSLASH, T_BUFFRE}, {SDL_SCANCODE_SLASH, T_VT},
};
#define NKEYS (int)(sizeof(k_keys) / sizeof(k_keys[0]))
#define NTOGGLES (int)(sizeof(k_toggles) / sizeof(k_toggles[0]))

static void press(NcSbcState *s, int btn)
{
    s->buttons[btn >> 8] |= (uint8_t)(btn & 0xff);
}

static int16_t dead(int v)
{
    const int dz = 7000, a = abs(v);
    if (a <= dz) {
        return 0;
    }
    const int out = (a - dz) * 32767 / (32767 - dz);
    return (int16_t)(v < 0 ? -out : out);
}

static int axis_key(const bool *keys, SDL_Scancode neg, SDL_Scancode pos)
{
    return keys ? (keys[pos] ? 32767 : 0) - (keys[neg] ? 32767 : 0) : 0;
}

/* The larger of two deflections. */
static int big(int a, int b)
{
    return abs(a) >= abs(b) ? a : b;
}

/* A pedal goes down and comes up over a few polls, not at once. */
static void pedal(uint16_t *p, int target)
{
    int v = *p;
    if (target > v) {
        v += 0x3000;
        v = v > target ? target : v;
    } else {
        v -= 0x5000;
        v = v < target ? target : v;
    }
    *p = (uint16_t)v;
}

void sbc_read(const bool *keys, SDL_Gamepad *pad, NcSbcState *out)
{
    static int gear = 1;            /* index: R N 1 2 3 4 5 */
    static int tuner;
    static uint8_t toggles[5];      /* the switches' positions */
    static bool prev_keys[NTOGGLES], prev_pad[4], prev_ptog[NTOGGLES];
    static uint16_t pedals[3];

    NcSbcState s;
    memset(&s, 0, sizeof(s));

    int aim_x = axis_key(keys, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    int aim_y = axis_key(keys, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    int rot = axis_key(keys, SDL_SCANCODE_A, SDL_SCANCODE_D);
    int sx = axis_key(keys, SDL_SCANCODE_J, SDL_SCANCODE_L);
    int sy = axis_key(keys, SDL_SCANCODE_I, SDL_SCANCODE_K);
    int ped[3] = {0, 0, 0};
    bool gear_up = false, gear_down = false, tuner_up = false, tuner_down = false;
    bool ptog[NTOGGLES] = {false};     /* the pad's presses of the toggle switches */

    if (keys) {
        for (int i = 0; i < NKEYS; i++) {
            if (keys[k_keys[i].key]) {
                press(&s, k_keys[i].btn);
            }
        }
        ped[0] = keys[SDL_SCANCODE_Z] ? 0xffff : 0;
        ped[1] = keys[SDL_SCANCODE_X] ? 0xffff : 0;
        ped[2] = keys[SDL_SCANCODE_C] ? 0xffff : 0;
        gear_up = keys[SDL_SCANCODE_Q];
        gear_down = keys[SDL_SCANCODE_E];
        tuner_down = keys[SDL_SCANCODE_COMMA];
        tuner_up = keys[SDL_SCANCODE_PERIOD];
    }

    if (pad) {
#define PB(b) SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_##b)
        const bool panel = PB(BACK);        /* held: the pad's buttons are the cockpit panel's */
        aim_x = big(aim_x, dead(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)));
        aim_y = big(aim_y, dead(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)));
        rot = big(rot, dead(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)));
        const int ly = dead(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        if (ly > 0) {
            ped[1] = ped[1] > ly * 0xffff / 32767 ? ped[1] : ly * 0xffff / 32767;   /* pushed down: the middle pedal */
        }
        const int lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        const int rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        ped[0] = ped[0] > lt * 0xffff / 32767 ? ped[0] : lt * 0xffff / 32767;
        ped[2] = ped[2] > rt * 0xffff / 32767 ? ped[2] : rt * 0xffff / 32767;
        if (!panel) {
            if (PB(SOUTH)) press(&s, B_FIRE);
            if (PB(WEST)) press(&s, B_MAIN_WEAPON);
            if (PB(NORTH)) press(&s, B_LOCK_ON);
            if (PB(EAST)) press(&s, B_SIGHT);
            if (PB(START)) press(&s, B_START);
            if (PB(LEFT_STICK)) press(&s, B_CHAFF);
            if (PB(RIGHT_STICK)) press(&s, B_NIGHT);
            sx = big(sx, (PB(DPAD_RIGHT) ? 32767 : 0) - (PB(DPAD_LEFT) ? 32767 : 0));
            sy = big(sy, (PB(DPAD_DOWN) ? 32767 : 0) - (PB(DPAD_UP) ? 32767 : 0));
            gear_down |= PB(LEFT_SHOULDER);
            gear_up |= PB(RIGHT_SHOULDER);
        } else {
            if (PB(SOUTH)) press(&s, B_IGNITION);
            if (PB(EAST)) press(&s, B_HATCH);
            if (PB(WEST)) press(&s, B_MODE);
            if (PB(NORTH)) press(&s, B_MON_OPEN);
            /* the five toggle switches, in the order the start-up has them flipped */
            ptog[0] = PB(DPAD_UP);
            ptog[1] = PB(DPAD_RIGHT);
            ptog[2] = PB(DPAD_DOWN);
            ptog[3] = PB(DPAD_LEFT);
            ptog[4] = PB(LEFT_STICK);
            if (PB(RIGHT_STICK)) press(&s, B_EXTING);
            tuner_down |= PB(LEFT_SHOULDER);
            tuner_up |= PB(RIGHT_SHOULDER);
        }
#undef PB
    }

    /* the gear lever, the tuner dial: a step a press */
    const bool now_keys[4] = {gear_up, gear_down, tuner_up, tuner_down};
    if (now_keys[0] && !prev_pad[0] && gear < 6) gear++;
    if (now_keys[1] && !prev_pad[1] && gear > 0) gear--;
    if (now_keys[2] && !prev_pad[2] && tuner < 12) tuner++;
    if (now_keys[3] && !prev_pad[3] && tuner > 0) tuner--;
    memcpy(prev_pad, now_keys, sizeof(prev_pad));
    for (int i = 0; i < NTOGGLES; i++) {
        const bool k = keys && keys[k_toggles[i].key];
        if ((k && !prev_keys[i]) || (ptog[i] && !prev_ptog[i])) {
            toggles[i] ^= 1;
        }
        prev_keys[i] = k;
        prev_ptog[i] = ptog[i];
        if (toggles[i]) {
            press(&s, k_toggles[i].btn);
        }
    }

    s.aim_x = (uint16_t)(0x8000 + aim_x);
    s.aim_y = (uint16_t)(0x8000 + aim_y);
    s.rotation = (int16_t)rot;
    s.sight_x = (int16_t)sx;
    s.sight_y = (int16_t)sy;
    for (int i = 0; i < 3; i++) {
        pedal(&pedals[i], ped[i] > 0xffff ? 0xffff : ped[i]);
        s.pedal[i] = pedals[i];
    }
    s.tuner = (uint8_t)tuner;
    s.gear = (int8_t)(gear == 0 ? -2 : gear == 1 ? -1 : gear - 1);
    *out = s;
}

const char *sbc_layout_text(void)
{
    return "Steel Battalion controller (keyboard): arrows aim, A/D rotation lever, I/J/K/L sight change, "
           "Z/X/C pedals, Q/E gear up/down, , . tuner, Space fire, Shift main weapon, Ctrl lock on, "
           "R hatch, T ignition, Enter start, Delete eject, 1-4 monitors, = - zoom, 5-0 functions, "
           "Y/U/O weapon control, V B N M ; comms, F sight change button, P [ ] \\ / toggle switches. "
           "Gamepad: right stick aim, left stick X rotation, D-pad sight change, LT/RT left/right pedal, "
           "left stick down middle pedal, LB/RB gear, A fire, X main weapon, Y lock on, B sight change, "
           "Start start; hold Back: A ignition, B hatch, X mode, Y monitor, D-pad up/right/down/left and left stick click the five toggle switches, LB/RB tuner.";
}
