/*
 * hakuX native core — the Xbox Live Communicator (the headset): a USB 1.1 device with two interfaces of one
 * isochronous endpoint each, the speaker (endpoint 4, OUT) and the microphone (endpoint 5, IN), mono signed 16-bit
 * samples at one rate for both (8000, 11025, 16000, 22050 or 24000 Hz, set by a vendor request), 48 bytes at most
 * a frame. It goes in a controller's memory unit slot (port 2 of its hub).
 *
 * Descriptors and requests are those the Xbox dev wiki lists for the real headset (Micronas UAC 35xx, 045e:0283).
 * Here the samples go to and come from the hooks the front end sets (xblc_audio.c: the PC's microphone and the
 * sound output); without them the speaker's samples are dropped and the microphone is silent.
 * Not tried with a title: written from the wiki's description and tested in a host test (tests/native/usb).
 */
#include "native/dev/usb.h"

#include <stdlib.h>

#define EP_SPEAKER 4
#define EP_MIC     5

static const uint8_t k_dev_desc[18] = {
    18, 1, 0x10, 0x01,      /* USB 1.1 */
    0, 0, 0, 8,             /* class and protocol per interface, max packet 0: 8 */
    0x5e, 0x04, 0x83, 0x02, /* 045e:0283 Microsoft, Xbox Communicator */
    0x58, 0x01,             /* 1.58 */
    0, 0, 0, 1,             /* no strings, one configuration */
};

static const uint8_t k_cfg_desc[45] = {
    9, 2, 45, 0, 2, 1, 0, 0x80, 50,             /* configuration: two interfaces, bus powered, 100 mA */
    9, 4, 0, 0, 1, 0x78, 0, 0, 0,               /* interface 0: the speaker */
    9, 5, 0x04, 0x05, 0x30, 0x00, 1, 0, 0,      /* endpoint 4 OUT, isochronous, asynchronous, 48 bytes, every frame */
    9, 4, 1, 0, 1, 0x78, 0, 0, 0,               /* interface 1: the microphone */
    9, 5, 0x85, 0x05, 0x30, 0x00, 1, 0, 0,      /* endpoint 5 IN, the same */
};

static const unsigned k_rates[5] = {8000, 11025, 16000, 22050, 24000};

static NcXblcAudio g_audio;
static unsigned g_rate = 8000;

typedef struct Xblc {
    NcUsbDev dev;
    bool agc;
} Xblc;

void nc_xblc_set_audio(const NcXblcAudio *a)
{
    if (a) {
        g_audio = *a;
    } else {
        memset(&g_audio, 0, sizeof(g_audio));
    }
}

unsigned nc_xblc_rate(void)
{
    return g_rate;
}

static int xblc_control(NcUsbDev *d, const uint8_t *s, uint8_t *data, int len)
{
    Xblc *x = (Xblc *)d;
    (void)data;
    (void)len;
    /* vendor, to the interface: SET_FEATURE. wIndex 0: the sample rate (wValue 0x100 | the rate's number);
     * wIndex 1: wValue 1 or 0, believed to be the microphone's automatic gain control. */
    if (s[0] == 0x41 && s[1] == 0x03) {
        const uint16_t value = nc_rd16(s + 2), index = nc_rd16(s + 4);
        if (index == 0 && (value >> 8) == 1 && (value & 0xff) < 5) {
            g_rate = k_rates[value & 0xff];
            nc_log("hakuX-native-usb", "headset: sample rate %u Hz", g_rate);
            if (g_audio.set_rate) {
                g_audio.set_rate(g_rate);
            }
            return 0;
        }
        if (index == 1) {
            x->agc = value != 0;
            return 0;
        }
        return 0;
    }
    return NC_USB_STALL;
}

static int xblc_data(NcUsbDev *d, bool in, int ep, uint8_t *buf, int len)
{
    (void)d;
    if (in && ep == EP_MIC) {
        /* what was said in this frame: exactly the packet's size (a quiet microphone is zeros, so the stream keeps
         * its time) */
        if (g_audio.mic_read) {
            g_audio.mic_read(buf, len);
        } else {
            memset(buf, 0, (size_t)len);
        }
        return len;
    }
    if (!in && ep == EP_SPEAKER) {
        if (g_audio.speaker_write && len > 0) {
            g_audio.speaker_write(buf, len);
        }
        return len;
    }
    return NC_USB_STALL;
}

NcUsbDev *nc_xid_xblc_new(void)
{
    Xblc *x = calloc(1, sizeof(Xblc));
    if (!x) {
        return NULL;
    }
    x->dev.name = "xbox live communicator";
    x->dev.dev_desc = k_dev_desc;
    x->dev.dev_desc_len = sizeof(k_dev_desc);
    x->dev.cfg_desc = k_cfg_desc;
    x->dev.cfg_desc_len = sizeof(k_cfg_desc);
    x->dev.strings = NULL;
    x->dev.nstrings = 0;
    x->dev.control = xblc_control;
    x->dev.data = xblc_data;
    x->dev.pending_addr = -1;
    return &x->dev;
}
