/*
 * Host test of the Steel Battalion controller (native/dev/xid_sbc.c): the XID
 * descriptor and capabilities it answers, the 26-byte report's layout, the
 * panel report it takes. Build (from the source root):
 *   cc -std=gnu11 -I. -o /tmp/sbc_test tests/sbc/sbc_test.c native/dev/xid_sbc.c -lpthread && /tmp/sbc_test
 */
#include "native/dev/usb.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void nc_log(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    NcUsbDev *d = nc_xid_sbc_new();
    uint8_t buf[64];

    /* XID GET_DESCRIPTOR: type 0x80, subtype 1, 26 in / 22 out */
    uint8_t setup[8] = {0xC1, 0x06, 0x00, 0x42, 0, 0, 16, 0};
    int n = d->control(d, setup, buf, 16);
    CHECK(n == 16 && buf[0] == 16 && buf[1] == 0x42 && buf[4] == 0x80 && buf[5] == 1 && buf[6] == 26 && buf[7] == 22);
    setup[3] = 0x41;
    CHECK(d->control(d, setup, buf, 16) == NC_USB_STALL);

    /* GET_CAPABILITIES: input 26, output 22 */
    uint8_t cap[8] = {0xC1, 0x01, 0x00, 0x01, 0, 0, 26, 0};
    n = d->control(d, cap, buf, 26);
    CHECK(n == 26 && buf[0] == 0 && buf[1] == 26 && buf[2] == 0xff);
    cap[3] = 0x02;
    n = d->control(d, cap, buf, 22);
    CHECK(n == 22 && buf[1] == 22);

    /* at rest: aiming lever centred, neutral gear (set by the app's first poll) */
    NcSbcState s;
    memset(&s, 0, sizeof(s));
    s.aim_x = s.aim_y = 0x8000;
    s.gear = -1;
    uint8_t r[26];
    nc_sbc_report(&s, r);
    CHECK(r[0] == 0 && r[1] == 26);
    CHECK(r[9] == 0x00 && r[10] == 0x80 && r[11] == 0x00 && r[12] == 0x80);
    CHECK(r[25] == 0xff);

    /* every button lands on its bit: Fire is byte 2 0x02, Comm5 byte 6 0x01, VT toggle byte 6 0x40 */
    memset(&s, 0, sizeof(s));
    s.buttons[0] = 0x02;
    s.buttons[4] = 0x41;
    nc_sbc_report(&s, r);
    CHECK(r[2] == 0x02 && r[6] == 0x41 && r[3] == 0 && r[4] == 0 && r[5] == 0);

    /* levers, pedals, tuner, gear */
    memset(&s, 0, sizeof(s));
    s.aim_x = 0x1234;
    s.aim_y = 0xfedc;
    s.rotation = -0x7fff;
    s.sight_x = 0x7fff;
    s.sight_y = -2;
    s.pedal[0] = 0xff00;
    s.pedal[1] = 0x1000;
    s.pedal[2] = 0xf000;
    s.tuner = 12;
    s.gear = 5;
    nc_sbc_report(&s, r);
    CHECK(r[9] == 0x34 && r[10] == 0x12 && r[11] == 0xdc && r[12] == 0xfe);
    CHECK(r[13] == 0x01 && r[14] == 0x80);
    CHECK(r[15] == 0xff && r[16] == 0x7f && r[17] == 0xfe && r[18] == 0xff);
    CHECK(r[20] == 0xff && r[22] == 0x10);
    CHECK(r[24] == 0xfc);            /* the right pedal's top nibble 0xf, the tuner 12 */
    CHECK(r[25] == 5);
    s.gear = -2;
    nc_sbc_report(&s, r);
    CHECK(r[25] == 0xfe);

    /* the interrupt endpoint: IN 2 returns the report, IN 1 is not there; OUT 1 takes the lamps */
    n = d->data(d, true, 2, buf, 32);
    CHECK(n == 26 && buf[1] == 26);
    CHECK(d->data(d, true, 1, buf, 32) == NC_USB_STALL);
    uint8_t out[22] = {0, 22, 0x0f, 0xf0};
    CHECK(d->data(d, false, 1, out, 22) == 22);
    uint8_t lamps[22];
    nc_sbc_lamps(lamps);
    CHECK(lamps[1] == 22 && lamps[2] == 0x0f && lamps[3] == 0xf0);

    /* descriptors: vendor 0x0a7b, product 0xd000, class 0x58/0x42, endpoints 0x82 and 0x01 */
    CHECK(d->dev_desc[8] == 0x7b && d->dev_desc[9] == 0x0a && d->dev_desc[10] == 0x00 && d->dev_desc[11] == 0xd0);
    CHECK(d->cfg_desc[14] == 0x58 && d->cfg_desc[15] == 0x42);
    CHECK(d->cfg_desc[20] == 0x82 && d->cfg_desc[27] == 0x01);

    printf(fails ? "%d failed\n" : "all ok\n", fails);
    return fails != 0;
}
