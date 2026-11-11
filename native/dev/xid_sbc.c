/*
 * hakuX native core — the Steel Battalion controller: an XID device (class
 * 0x58, bType 0x80) with a 26-byte input report on interrupt IN 2 and the
 * 22-byte panel report (the lamps) on interrupt OUT 1. Steel Battalion
 * (title 43430002) will not play with a gamepad: it looks for this one.
 *
 * Layout as the Xbox dev wiki (Xbox Input Devices) has it: bytes 2-6 are the
 * buttons and switches bit for bit, 9-18 the aiming lever (unsigned) and the
 * rotation lever and the sight change stick (signed) as 16-bit words, 19-24
 * the three pedals, 24's low nibble the tuner dial, 25 the gear lever.
 * The wiki has the right pedal's word at 23-24 over the tuner's byte; this
 * keeps the pedal's top nibble there, so the title gets the pedal's value to
 * within a sixteenth read either way (a word or its high byte).
 *
 * Input comes from nc_sbc_set (the app's keyboard and gamepad mapping,
 * sbc_map.c); the lamps the title lights come back through nc_sbc_lamps.
 */
#include "native/dev/usb.h"
#include "native/core/irq.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SBC_IN_LEN  26
#define SBC_OUT_LEN 22

typedef struct Sbc {
    NcUsbDev dev;
} Sbc;

static const uint8_t k_dev_desc[18] = {
    18, 1, 0x10, 0x01, /* USB 1.1 */
    0, 0, 0,           /* class per interface */
    8,
    0x7b, 0x0a, 0x00, 0xd0, /* Capcom (0x0a7b), the controller (0xd000) */
    0x00, 0x01, 0, 0, 0, 1,
};

static const uint8_t k_cfg_desc[32] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 250,
    9, 4, 0, 0, 2, 0x58, 0x42, 0, 0,  /* XID interface */
    7, 5, 0x82, 0x03, 0x20, 0, 4,     /* the controls: interrupt IN 2 */
    7, 5, 0x01, 0x03, 0x20, 0, 4,     /* the lamps: interrupt OUT 1 */
};

static const uint8_t k_xid_desc[16] = {
    16, 0x42, 0x00, 0x01, /* XID 1.00 */
    0x80, 1,              /* Steel Battalion */
    SBC_IN_LEN, SBC_OUT_LEN,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
/* At rest: the aiming lever centred. */
static NcSbcState g_sbc = {.aim_x = 0x8000, .aim_y = 0x8000, .gear = -1};
static uint8_t g_lamps[SBC_OUT_LEN];
static unsigned g_lamp_reports;

static void make_report(uint8_t r[SBC_IN_LEN])
{
    NcSbcState s;
    pthread_mutex_lock(&g_lock);
    s = g_sbc;
    pthread_mutex_unlock(&g_lock);

    memset(r, 0, SBC_IN_LEN);
    r[1] = SBC_IN_LEN;
    memcpy(r + 2, s.buttons, 5);
    nc_wr16(r + 9, s.aim_x);
    nc_wr16(r + 11, s.aim_y);
    nc_wr16(r + 13, (uint16_t)s.rotation);
    nc_wr16(r + 15, (uint16_t)s.sight_x);
    nc_wr16(r + 17, (uint16_t)s.sight_y);
    nc_wr16(r + 19, s.pedal[0]);
    nc_wr16(r + 21, s.pedal[1]);
    nc_wr16(r + 23, s.pedal[2]);
    r[24] = (uint8_t)((r[24] & 0xf0) | (s.tuner & 0x0f));
    r[25] = (uint8_t)s.gear;
}

/* The panel report's fields, in order, two to a byte (low nibble first), from byte 2 (the Xbox dev wiki). */
static const char *const k_lamp_names[] = {
    "EmergencyEject", "CockpitHatch", "Ignition", "Start", "OpenClose", "MapZoomInOut", "ModeSelect",
    "SubMonitorModeSelect", "MainMonitorZoomIn", "MainMonitorZoomOut", "ForecastShootingSystem", "Manipulator",
    "LineColorChange", "Washing", "Extinguisher", "Chaff", "TankDetach", "Override", "NightScope", "F1", "F2", "F3",
    "MainWeaponControl", "SubWeaponControl", "MagazineChange", "Comm1", "Comm2", "Comm3", "Comm4", "Comm5",
    "(17 low)", "GearR", "GearN", "Gear1", "Gear2", "Gear3", "Gear4", "Gear5",
};

/* The lamps the title lit, when they change (the first 80 times): what it is waiting for. */
static void log_lamps(const uint8_t *r)
{
    static uint8_t last[SBC_OUT_LEN];
    static unsigned logged;
    if (!memcmp(last, r, SBC_OUT_LEN) || logged >= 80) {
        return;
    }
    memcpy(last, r, SBC_OUT_LEN);
    logged++;
    char line[600];
    int n = snprintf(line, sizeof(line), "steel battalion controller: lamps lit:");
    for (int i = 0; i < 38 && n < (int)sizeof(line) - 40; i++) {
        const unsigned v = (r[2 + i / 2] >> (i & 1 ? 4 : 0)) & 0xf;
        if (v) {
            n += snprintf(line + n, sizeof(line) - (size_t)n, " %s=%u", k_lamp_names[i], v);
        }
    }
    if (n == (int)strlen("steel battalion controller: lamps lit:")) {
        snprintf(line + n, sizeof(line) - (size_t)n, " none");
    }
    nc_log("hakuX-native-usb", "%s", line);
}

static void take_lamps(const uint8_t *r, int len)
{
    if (len < 4) {
        return;
    }
    uint8_t l[SBC_OUT_LEN] = {0};
    memcpy(l, r, MIN(len, SBC_OUT_LEN));
    pthread_mutex_lock(&g_lock);
    memcpy(g_lamps, l, sizeof(g_lamps));
    const unsigned n = ++g_lamp_reports;
    pthread_mutex_unlock(&g_lock);
    if (n == 1) {
        nc_log("hakuX-native-usb", "steel battalion controller: the title sent its panel report (%d bytes)", len);
    }
    log_lamps(l);
}

/* What the title reads changing (the buttons, the switches, the pedals' top nibbles, the tuner, the gear),
 * the first 300 times: whether the title gets what was pressed. */
static void log_changes(const uint8_t r[SBC_IN_LEN])
{
    static uint8_t last[12];
    static unsigned logged;
    uint8_t now[12];
    memcpy(now, r + 2, 5);
    now[5] = (uint8_t)(r[20] >> 4);
    now[6] = (uint8_t)(r[22] >> 4);
    now[7] = (uint8_t)(r[24] >> 4);
    now[8] = (uint8_t)(r[24] & 0xf);
    now[9] = r[25];
    now[10] = now[11] = 0;
    if (!memcmp(last, now, sizeof(now)) || logged >= 300) {
        return;
    }
    memcpy(last, now, sizeof(now));
    logged++;
    nc_log("hakuX-native-usb",
           "steel battalion controller: the title reads buttons %02x %02x %02x %02x %02x, pedals %x %x %x, tuner %u, gear %d",
           now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7], now[8], (int8_t)now[9]);
}

static int sbc_control(NcUsbDev *d, const uint8_t *s, uint8_t *data, int len)
{
    (void)d;
    const uint8_t type = s[0], req = s[1];
    const uint16_t value = nc_rd16(s + 2);
    switch ((type << 8) | req) {
    case 0xA101: /* HID GET_REPORT */
        if (value != 0x0100) {
            return NC_USB_STALL;
        }
        {
            uint8_t r[SBC_IN_LEN];
            make_report(r);
            memcpy(data, r, MIN(len, SBC_IN_LEN));
            return MIN(len, SBC_IN_LEN);
        }
    case 0x2109: /* HID SET_REPORT */
        if (value != 0x0200) {
            return NC_USB_STALL;
        }
        take_lamps(data, len);
        return 0;
    case 0xC106: /* XID GET_DESCRIPTOR */
        if (value != 0x4200) {
            return NC_USB_STALL;
        }
        memcpy(data, k_xid_desc, MIN(len, (int)sizeof(k_xid_desc)));
        return MIN(len, (int)sizeof(k_xid_desc));
    case 0xC101: /* XID GET_CAPABILITIES: every control and lamp is live */
        if (value == 0x0100 || value == 0x0200) {
            const int n = value == 0x0100 ? SBC_IN_LEN : SBC_OUT_LEN;
            memset(data, 0xff, MIN(len, n));
            data[0] = 0;
            if (len > 1) {
                data[1] = (uint8_t)n;
            }
            return MIN(len, n);
        }
        return NC_USB_STALL;
    default:
        nc_log("hakuX-native-usb", "steel battalion controller: unhandled request %02x %02x value %04x", type, req,
               value);
        return NC_USB_STALL;
    }
}

static int sbc_data(NcUsbDev *d, bool in, int ep, uint8_t *buf, int len)
{
    (void)d;
    if (in) {
        if (ep != 2) {
            return NC_USB_STALL;
        }
        static bool polled;
        if (!polled) {
            polled = true;
            nc_log("hakuX-native-usb", "steel battalion controller: polled");
        }
        uint8_t r[SBC_IN_LEN];
        make_report(r);
        log_changes(r);
        const int n = MIN(len, SBC_IN_LEN);
        memcpy(buf, r, n);
        return n;
    }
    if (ep != 1) {
        return NC_USB_STALL;
    }
    take_lamps(buf, len);
    return len;
}

NcUsbDev *nc_xid_sbc_new(void)
{
    Sbc *x = calloc(1, sizeof(Sbc));
    x->dev.name = "steel battalion controller";
    x->dev.dev_desc = k_dev_desc;
    x->dev.dev_desc_len = sizeof(k_dev_desc);
    x->dev.cfg_desc = k_cfg_desc;
    x->dev.cfg_desc_len = sizeof(k_cfg_desc);
    x->dev.strings = NULL;
    x->dev.nstrings = 0;
    x->dev.control = sbc_control;
    x->dev.data = sbc_data;
    x->dev.pending_addr = -1;
    return &x->dev;
}

void nc_sbc_set(const NcSbcState *s)
{
    pthread_mutex_lock(&g_lock);
    g_sbc = *s;
    pthread_mutex_unlock(&g_lock);
}

void nc_sbc_lamps(uint8_t out[22])
{
    pthread_mutex_lock(&g_lock);
    memcpy(out, g_lamps, SBC_OUT_LEN);
    pthread_mutex_unlock(&g_lock);
}

/* The report as the title gets it, for a host test. */
void nc_sbc_report(const NcSbcState *s, uint8_t r[26])
{
    nc_sbc_set(s);
    make_report(r);
}
