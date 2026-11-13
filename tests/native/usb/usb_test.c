/*
 * Host test of the native USB stack (native/dev/ohci.c, usb.c, usb_hub.c,
 * xid.c): a small host driver enumerates the front panel hub, a controller's
 * hub and its gamepad through OHCI EDs and TDs in fake guest RAM, then reads
 * a report and sends rumble.
 *
 *   gcc -O1 -I. tests/native/usb/usb_test.c native/dev/usb.c native/dev/usb_hub.c native/dev/xid.c native/dev/xid_xblc.c -o usb_test
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* The MMIO and IRQ glue, stubbed (their headers need a POSIX host). */
#define NATIVE_CORE_MMIO_H
#define NATIVE_CORE_IRQ_H
#include "native/core/vm.h"
typedef struct NcMmioDev {
    const char *name;
    uint32_t base, size;
    uint64_t (*read)(void *opaque, uint32_t off, unsigned size);
    void (*write)(void *opaque, uint32_t off, uint64_t value, unsigned size);
    void *opaque;
} NcMmioDev;
static void nc_mmio_register(const NcMmioDev *dev)
{
}
static int g_irq_level;
static void nc_irq_set(unsigned line, bool level)
{
    g_irq_level = level;
}
void nc_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("  [%s] ", tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

#include "native/dev/ohci.c"

/* What ohci.c links to beyond the USB stack: savestate records, the pause hooks, the headset's audio, the Steel
 * Battalion device, the frame count. None of it is used here. */
void nc_pause_register(const char *name) {}
void nc_pause_idle(bool idle) {}
int nc_paused(void) { return 0; }
void nc_pause_wait(void) {}
void nc_xblc_audio_install(void) {}
NcUsbDev *nc_xid_sbc_new(void) { return NULL; }
uint64_t hle_back_swap_count(void) { return 0; }
void nc_buf_put(NcBuf *b, const void *p, size_t n) {}
void nc_rec_put(NcBuf *b, const NcRecord *rec, const void *p) {}
void nc_rec_get(NcRd *b, const NcRecord *rec, void *p, unsigned int ver) {}
bool nc_rd_get(NcRd *b, void *p, size_t n) { return true; }


static int g_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                       \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

#define RAM_SIZE (1u << 20)
static uint8_t *g_ram;
static NcVm g_vm;
static Ohci *o = &g_ohci;

#define HCCA   0x10000u
#define ED_CTL 0x11000u
#define ED_INT 0x11100u
#define TDS    0x12000u
#define BUFS   0x20000u

static uint32_t rd(uint32_t pa)
{
    return nc_rd32(g_ram + pa);
}
static void wr(uint32_t pa, uint32_t v)
{
    nc_wr32(g_ram + pa, v);
}
static uint32_t reg(uint32_t off)
{
    return (uint32_t)ohci_read(o, off, 4);
}
static void set_reg(uint32_t off, uint32_t v)
{
    ohci_write(o, off, v, 4);
}

static void td(uint32_t pa, unsigned dp, unsigned toggle, bool round, unsigned di, uint32_t buf, uint32_t len,
               uint32_t next)
{
    wr(pa, (0xfu << 28) | (toggle << 24) | (di << 21) | (dp << 19) | (round ? TD_R : 0));
    wr(pa + 4, len ? buf : 0);
    wr(pa + 8, next);
    wr(pa + 12, len ? buf + len - 1 : 0);
}

/* Runs frames until the done queue arrives; returns it (WDH acknowledged). */
static uint32_t wait_done(void)
{
    for (int i = 0; i < 16; i++) {
        frame(o);
        if (reg(0x0C) & INT_WDH) {
            const uint32_t d = rd(HCCA + 0x84);
            wr(HCCA + 0x84, 0);
            set_reg(0x0C, INT_WDH);
            return d & ~1u;
        }
    }
    return 0;
}

static unsigned td_cc(uint32_t pa)
{
    return rd(pa) >> 28;
}

/* A control transfer to (addr, ep 0). Returns the data stage's byte count, or -cc. */
static int control(uint8_t addr, uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint8_t *data,
                   uint16_t len)
{
    const bool in = type & 0x80;
    uint8_t *setup = g_ram + BUFS;
    setup[0] = type;
    setup[1] = req;
    nc_wr16(setup + 2, value);
    nc_wr16(setup + 4, index);
    nc_wr16(setup + 6, len);
    if (!in && len) {
        memcpy(g_ram + BUFS + 0x100, data, len);
    }
    const uint32_t t_setup = TDS, t_data = TDS + 0x10, t_status = TDS + 0x20, t_tail = TDS + 0x30;
    td(t_setup, PID_SETUP, 2, false, 7, BUFS, 8, len ? t_data : t_status);
    if (len) {
        td(t_data, in ? PID_IN : PID_OUT, 3, true, 7, BUFS + 0x100, len, t_status);
    }
    td(t_status, in ? PID_OUT : PID_IN, 3, false, 0, 0, 0, t_tail);
    td(t_tail, 0, 0, false, 7, 0, 0, 0);
    wr(ED_CTL, addr | (0u << 7) | (64u << 16));
    wr(ED_CTL + 4, t_tail);
    wr(ED_CTL + 8, t_setup);
    wr(ED_CTL + 12, 0);
    set_reg(0x20, ED_CTL);
    set_reg(0x08, CS_CLF);
    const uint32_t done = wait_done();
    if (!done) {
        return -100;
    }
    /* The done queue runs newest first: status, data, setup. */
    for (uint32_t t = done; t; t = rd(t + 8)) {
        if (td_cc(t) != CC_NOERROR) {
            return -(int)td_cc(t);
        }
    }
    if (!len) {
        return 0;
    }
    const uint32_t cbp = rd(t_data + 4);
    const int n = cbp ? (int)(cbp - (BUFS + 0x100)) : len;
    if (in) {
        memcpy(data, g_ram + BUFS + 0x100, n);
    }
    return n;
}

static bool desc_ok(const uint8_t *d, int n)
{
    return n >= 9 && d[1] == 2 && nc_rd16(d + 2) == n;
}

/* GET_DESCRIPTOR(configuration) with the data stage as one 8-byte TD per
 * packet and no buffer rounding, as XAPI sends it: the short last packet
 * halts the ED with DataUnderrun, and its CBP must count what arrived.
 * Returns the bytes the host driver would count. */
static int config_in_packets(uint8_t addr, uint16_t wlen)
{
    uint8_t *setup = g_ram + BUFS;
    const uint8_t s[8] = {0x80, 6, 0x00, 0x02, 0, 0, (uint8_t)wlen, (uint8_t)(wlen >> 8)};
    memcpy(setup, s, 8);
    const unsigned npk = (wlen + 7) / 8;
    const uint32_t t0 = TDS + 0x200;
    td(t0, PID_SETUP, 2, false, 7, BUFS, 8, t0 + 0x10);
    for (unsigned i = 0; i < npk; i++) {
        td(t0 + 0x10 * (i + 1), PID_IN, i & 1 ? 2 : 3, false, 7, BUFS + 0x100 + 8 * i, 8, t0 + 0x10 * (i + 2));
    }
    const uint32_t tail = t0 + 0x10 * (npk + 1);
    td(tail, 0, 0, false, 7, 0, 0, 0);
    wr(ED_CTL, addr | (8u << 16));
    wr(ED_CTL + 4, tail);
    wr(ED_CTL + 8, t0);
    wr(ED_CTL + 12, 0);
    set_reg(0x20, ED_CTL);
    set_reg(0x08, CS_CLF);
    const uint32_t done = wait_done();
    CHECK(rd(ED_CTL + 8) & ED_H); /* halted on the short packet */
    int total = 0;
    for (uint32_t t = done; t; t = rd(t + 8)) {
        if (t == t0) {
            continue;
        }
        const uint32_t start = BUFS + 0x100 + 8 * ((t - t0) / 0x10 - 1);
        const uint32_t cbp = rd(t + 4);
        total += cbp ? (int)(cbp - start) : 8;
        if (td_cc(t) != CC_NOERROR) {
            CHECK(td_cc(t) == CC_DATAUNDERRUN);
        }
    }
    CHECK(desc_ok(g_ram + BUFS + 0x100, total));
    /* The driver drops the rest of the data TDs and runs the status stage. */
    wr(ED_CTL + 8, 0);
    return total;
}

/* One interrupt IN transfer through the periodic list; NAK returns -1. */
static int interrupt_in(uint8_t addr, uint8_t ep, uint8_t *data, uint16_t len)
{
    const uint32_t t = TDS + 0x100, tail = TDS + 0x110;
    td(t, PID_IN, 0, true, 0, BUFS + 0x200, len, tail);
    td(tail, 0, 0, false, 7, 0, 0, 0);
    wr(ED_INT, addr | ((uint32_t)ep << 7) | (2u << 11) | (64u << 16));
    wr(ED_INT + 4, tail);
    wr(ED_INT + 8, t);
    wr(ED_INT + 12, 0);
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, ED_INT);
    }
    const uint32_t done = wait_done();
    for (int i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, 0);
    }
    if (!done) {
        return -1;
    }
    CHECK(done == t);
    if (td_cc(t) != CC_NOERROR) {
        return -(int)td_cc(t);
    }
    const uint32_t cbp = rd(t + 4);
    const int n = cbp ? (int)(cbp - (BUFS + 0x200)) : len;
    memcpy(data, g_ram + BUFS + 0x200, n);
    return n;
}

static void hub_port(uint8_t addr, uint16_t port, uint16_t feature, bool set)
{
    CHECK(control(addr, 0x23, set ? 3 : 1, feature, port, NULL, 0) == 0);
}

static uint32_t hub_port_status(uint8_t addr, uint16_t port)
{
    uint8_t st[4] = {0};
    CHECK(control(addr, 0xA3, 0, 0, port, st, 4) == 4);
    return nc_rd32(st);
}

/* Reset a hub port and give what appears at address 0 the address new_addr. */
static void enumerate_on_hub_port(uint8_t hub, uint16_t port, uint8_t new_addr, uint8_t dev_desc[18])
{
    hub_port(hub, port, 4, true); /* PORT_RESET */
    const uint32_t st = hub_port_status(hub, port);
    CHECK(st & 0x0002);           /* enabled */
    CHECK(st & (0x0010u << 16));  /* C_PORT_RESET */
    hub_port(hub, port, 20, false);
    CHECK(control(0, 0x80, 6, 0x0100, 0, dev_desc, 18) == 18);
    CHECK(control(0, 0x00, 5, new_addr, 0, NULL, 0) == 0);
    CHECK(control(new_addr, 0x00, 9, 1, 0, NULL, 0) == 0);
}


/* ---- the headset (xid_xblc.c) and isochronous transfers ---- */
static uint8_t g_mic_n;
static uint8_t g_spk_log[512];
static int g_spk_len;
static unsigned g_rate_set;
static void hk_rate(unsigned hz)
{
    g_rate_set = hz;
}
static void hk_mic(uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        buf[i] = g_mic_n++;
    }
}
static void hk_spk(const uint8_t *buf, int len)
{
    if (g_spk_len + len <= (int)sizeof(g_spk_log)) {
        memcpy(g_spk_log + g_spk_len, buf, (size_t)len);
        g_spk_len += len;
    }
}

#define ED_ISO  0x11200u
#define ITD     0x13000u
#define ITD_TAIL 0x13040u
#define ISOBUF  0x30000u

/* One isochronous TD of `n` packets of `plen` bytes at the frame `sf`, on an ED for (addr, ep, dir 1 OUT / 2 IN);
 * the frames run until it is on the done queue (or `max` of them); returns the done queue head (0: not done). */
static uint32_t iso_run(uint8_t addr, unsigned ep, unsigned dir, unsigned sf, unsigned n, unsigned plen, int max)
{
    for (unsigned i = 0; i < 32; i++) {
        wr(HCCA + 4 * i, ED_ISO);
    }
    wr(ITD, (sf & 0xffff) | ((n - 1) << 24) | (0xfu << 28));
    wr(ITD + 4, ISOBUF);
    wr(ITD + 8, ITD_TAIL);
    wr(ITD + 12, ISOBUF + n * plen - 1);
    for (unsigned i = 0; i < 8; i += 2) {
        const uint32_t a = 0xE000u | ((i * plen) & 0xfff), b = 0xE000u | (((i + 1) * plen) & 0xfff);
        wr(ITD + 16 + 2 * i, a | (b << 16));
    }
    wr(ITD_TAIL, 0);
    wr(ED_ISO, addr | (ep << 7) | (dir << 11) | (1u << 15) | (48u << 16));
    wr(ED_ISO + 4, ITD_TAIL);
    wr(ED_ISO + 8, ITD);
    wr(ED_ISO + 12, 0);
    for (int i = 0; i < max; i++) {
        frame(o);
        if (reg(0x0C) & INT_WDH) {
            const uint32_t d = rd(HCCA + 0x84);
            wr(HCCA + 0x84, 0);
            set_reg(0x0C, INT_WDH);
            return d & ~1u;
        }
    }
    return 0;
}

static unsigned psw(unsigned i)
{
    return (rd(ITD + 16 + 4 * (i / 2)) >> (16 * (i & 1))) & 0xffff;
}

static void headset_test(uint8_t hub, uint8_t addr)
{
    uint8_t desc[64];
    const NcXblcAudio hooks = {hk_rate, hk_mic, hk_spk};
    nc_xblc_set_audio(&hooks);
    for (uint16_t p = 1; p <= 3; p++) {
        hub_port(hub, p, 8, true);
    }
    enumerate_on_hub_port(hub, 2, addr, desc);
    CHECK(nc_rd16(desc + 8) == 0x045e && nc_rd16(desc + 10) == 0x0283);
    CHECK(control(addr, 0x80, 6, 0x0200, 0, desc, 64) == 45);
    CHECK(desc[4] == 2 && desc[9 + 5] == 0x78 && desc[18 + 2] == 0x04 && desc[36 + 2] == 0x85);
    /* the sample rate: vendor SET_FEATURE, wValue 0x100 | number */
    CHECK(control(addr, 0x41, 3, 0x0102, 0, NULL, 0) == 0);
    CHECK(g_rate_set == 16000 && nc_xblc_rate() == 16000);
    CHECK(control(addr, 0x41, 3, 0x0105, 0, NULL, 0) == 0 && g_rate_set == 16000);   /* no such rate: unchanged */
    CHECK(control(addr, 0x41, 3, 0x0001, 1, NULL, 0) == 0);                          /* gain control: taken */
    CHECK(control(addr, 0xC1, 1, 0, 0, desc, 4) == -CC_STALL);                       /* other vendor requests: stall */

    /* speaker: four packets of 32 bytes (16 kHz) out at the frames after this one */
    for (int i = 0; i < 128; i++) {
        g_ram[ISOBUF + (unsigned)i] = (uint8_t)(0xA0 + i);
    }
    g_spk_len = 0;
    uint32_t done = iso_run(addr, 4, 1, (unsigned)(o->frame_number + 2), 4, 32, 12);
    CHECK(done == ITD);
    CHECK(rd(ITD) >> 28 == CC_NOERROR);
    CHECK(g_spk_len == 128);
    CHECK(!memcmp(g_spk_log, g_ram + ISOBUF, 128));
    for (unsigned i = 0; i < 4; i++) {
        CHECK(psw(i) == 32);                                        /* NoError, 32 bytes */
    }
    CHECK((rd(ED_ISO + 8) & ~0xfu) == ITD_TAIL);                    /* the ED's head: the tail */

    /* microphone: four packets in, the hook's samples land in the guest's buffer, packet by packet */
    memset(g_ram + ISOBUF, 0, 128);
    g_mic_n = 7;
    done = iso_run(addr, 5, 2, (unsigned)(o->frame_number + 1), 4, 32, 12);
    CHECK(done == ITD && rd(ITD) >> 28 == CC_NOERROR);
    for (int i = 0; i < 128; i++) {
        if (g_ram[ISOBUF + (unsigned)i] != (uint8_t)(7 + i)) {
            CHECK(0);
            break;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        CHECK(psw(i) == 32);
    }

    /* one frame a packet: the TD is not done before its last frame */
    g_mic_n = 0;
    const unsigned sf = (unsigned)(o->frame_number + 4);
    done = iso_run(addr, 5, 2, sf, 3, 48, 3);
    CHECK(done == 0);                                              /* three frames: not yet its first */

    /* a TD whose frames have gone by is retired whole with DataOverrun */
    done = iso_run(addr, 5, 2, (unsigned)(o->frame_number - 30), 2, 32, 4);
    CHECK(done == ITD && rd(ITD) >> 28 == CC_DATAOVERRUN);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_ram = calloc(1, RAM_SIZE);
    g_vm.ram_size = RAM_SIZE;
    g_vm.phys = g_ram;
    o->vm = &g_vm;
    pthread_mutex_init(&o->lock, NULL);
    pthread_cond_init(&o->run, NULL);

    /* nc_dev_ohci_init's topology: player 1's controller on front hub port 3. */
    NcUsbDev *front = nc_usb_hub_new(4);
    NcUsbDev *pad_hub = nc_usb_hub_new(3);
    nc_usb_hub_attach(pad_hub, 1, nc_xid_gamepad_new(0));
    nc_usb_hub_attach(pad_hub, 2, nc_xid_xblc_new());
    nc_usb_hub_attach(front, 3, pad_hub);
    o->dev[0] = front;
    soft_reset(o);
    o->ctl = 0;
    roothub_reset(o);

    /* Controller bring-up, as XAPI does it. */
    CHECK(reg(0x00) == 0x10);
    set_reg(0x08, CS_HCR);
    set_reg(0x18, HCCA);
    set_reg(0x10, INT_MIE | INT_WDH | INT_RHSC);
    set_reg(0x04, HCFS_OPERATIONAL | CTL_PLE | CTL_CLE | CTL_BLE);
    CHECK((reg(0x48) & 0xff) == ROOT_PORTS);

    /* Root port 1: the front panel hub is connected; reset enables it. */
    CHECK((reg(0x54) & (PORT_CCS | PORT_CSC)) == (PORT_CCS | PORT_CSC));
    CHECK(!(reg(0x58) & PORT_CCS));
    set_reg(0x54, PORT_CSC);
    set_reg(0x54, PORT_PRS);
    CHECK((reg(0x54) & (PORT_PES | PORT_PRSC)) == (PORT_PES | PORT_PRSC));
    set_reg(0x54, PORT_PRSC);

    uint8_t desc[64];
    CHECK(control(0, 0x80, 6, 0x0100, 0, desc, 18) == 18);
    CHECK(desc[4] == 9); /* hub class */
    CHECK(control(0, 0x00, 5, 1, 0, NULL, 0) == 0);
    CHECK(control(0, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING); /* moved to address 1 */
    CHECK(control(1, 0x80, 6, 0x0200, 0, desc, 64) == 25);
    CHECK(config_in_packets(1, 0x30) == 25);
    CHECK(control(1, 0x00, 9, 1, 0, NULL, 0) == 0);
    CHECK(control(1, 0xA0, 6, 0x2900, 0, desc, 9) == 9);
    CHECK(desc[2] == 4);

    /* Ports are powered from reset: the connection on port 3 is a change. */
    for (uint16_t p = 1; p <= 4; p++) {
        hub_port(1, p, 8, true); /* PORT_POWER */
    }
    CHECK(interrupt_in(1, 1, desc, 1) == 1);
    CHECK(desc[0] == (1u << 3));
    CHECK(hub_port_status(1, 3) == (0x0101u | (1u << 16)));
    CHECK(hub_port_status(1, 1) == 0x0100u);
    hub_port(1, 3, 16, false); /* C_PORT_CONNECTION */
    CHECK(interrupt_in(1, 1, desc, 1) == -1); /* nothing changed since */

    /* The controller's own hub, then the gamepad on its port 1. */
    enumerate_on_hub_port(1, 3, 2, desc);
    CHECK(desc[4] == 9);
    for (uint16_t p = 1; p <= 3; p++) {
        hub_port(2, p, 8, true);
    }
    CHECK(hub_port_status(2, 1) & 1);
    enumerate_on_hub_port(2, 1, 3, desc);
    CHECK(nc_rd16(desc + 8) == 0x045e && nc_rd16(desc + 10) == 0x0202);

    CHECK(control(3, 0x80, 6, 0x0200, 0, desc, 64) == 32);
    CHECK(desc[14] == 0x58); /* XID interface */
    CHECK(control(3, 0xC1, 6, 0x4200, 0, desc, 16) == 16);
    CHECK(desc[1] == 0x42 && desc[4] == 1 && desc[6] == 20 && desc[7] == 6);
    CHECK(control(3, 0xC1, 1, 0x0100, 0, desc, 20) == 20);
    CHECK(desc[1] == 20 && desc[2] == 0xff);

    /* Input: a report on interrupt IN 2. */
    NcPadState pad = {.buttons = NC_PAD_START | NC_PAD_DPAD_UP, .lx = -12345};
    pad.analog[0] = 0xff;
    nc_pad_set(0, &pad);
    CHECK(interrupt_in(3, 2, desc, 20) == 20);
    CHECK(desc[1] == 20 && nc_rd16(desc + 2) == (NC_PAD_START | NC_PAD_DPAD_UP));
    CHECK(desc[4] == 0xff && (int16_t)nc_rd16(desc + 12) == -12345);
    CHECK(control(3, 0xA1, 1, 0x0100, 0, desc, 20) == 20);
    CHECK(nc_rd16(desc + 2) == (NC_PAD_START | NC_PAD_DPAD_UP));

    /* Rumble: HID SET_REPORT. */
    const uint8_t rumble[6] = {0, 6, 0x34, 0x12, 0x78, 0x56};
    CHECK(control(3, 0x21, 9, 0x0200, 0, (uint8_t *)rumble, 6) == 6);
    uint16_t l = 0, r = 0;
    nc_pad_rumble(0, &l, &r);
    CHECK(l == 0x1234 && r == 0x5678);

    headset_test(2, 4);

    /* An unknown address stalls nothing; it doesn't answer. Unknown requests stall. */
    CHECK(control(9, 0x80, 6, 0x0100, 0, desc, 18) == -CC_DEVICENOTRESPONDING);
    CHECK(control(3, 0xC1, 6, 0x4300, 0, desc, 16) == -CC_STALL);

    /* The interrupt line follows status & enable & MIE. */
    set_reg(0x0C, 0xffffffffu);
    CHECK(!g_irq_level);
    set_reg(0x10, INT_SF);
    frame(o);
    CHECK(g_irq_level);
    set_reg(0x14, INT_SF);
    CHECK(!g_irq_level);

    printf(g_fail ? "usb_test: %d FAILED\n" : "usb_test: all passed\n", g_fail);
    return g_fail != 0;
}
