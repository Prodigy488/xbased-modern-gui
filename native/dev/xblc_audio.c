/*
 * hakuX native core — the headset's (xid_xblc.c) sound: the PC's default microphone in, the game's sound device
 * out, as SDL3 audio streams (mono signed 16-bit at the headset's rate). Opened by a thread of their own when the
 * title first uses the headset (a microphone can take a moment to open, or ask the system's permission), so the
 * USB frames never wait for it; until then the microphone is silent and the speaker's samples are dropped.
 */
#include "native/dev/usb.h"

#ifdef NC_DESKTOP
#include <SDL3/SDL.h>

SDL_AudioDeviceID nc_audio_output_device(void);

static SDL_AudioStream *g_mic, *g_spk;
static SDL_AtomicInt g_state;               /* 0 not started, 1 opening, 2 ready, 3 failed */
static SDL_AtomicInt g_rate_hz;
static bool g_primed;                       /* the microphone's stream holds enough to serve from */

#define BYTES_PER_MS(hz) ((int)((hz) * 2 / 1000))

static int SDLCALL open_thread(void *arg)
{
    (void)arg;
    const unsigned hz = (unsigned)SDL_GetAtomicInt(&g_rate_hz);
    const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 1, (int)hz};
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        g_spk = SDL_OpenAudioDeviceStream(nc_audio_output_device(), &spec, NULL, NULL);
        g_mic = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL);
    }
    nc_log("hakuX-native-usb", "headset: speaker %s, microphone %s%s%s", g_spk ? "open" : "not available",
           g_mic ? "open" : "not available", g_mic ? "" : " (SDL: ", g_mic ? "" : SDL_GetError());
    if (!g_mic) {
        nc_log("hakuX-native-usb", "headset: no microphone: the system may be asking for permission, or has none");
    }
    if (g_spk) {
        SDL_ResumeAudioStreamDevice(g_spk);
    }
    if (g_mic) {
        SDL_ResumeAudioStreamDevice(g_mic);
    }
    SDL_SetAtomicInt(&g_state, g_spk || g_mic ? 2 : 3);
    return 0;
}

static void ensure_open(void)
{
    if (SDL_CompareAndSwapAtomicInt(&g_state, 0, 1)) {
        SDL_Thread *t = SDL_CreateThread(open_thread, "xblc.open", NULL);
        if (t) {
            SDL_DetachThread(t);
        } else {
            SDL_SetAtomicInt(&g_state, 3);
        }
    }
}

static void a_set_rate(unsigned hz)
{
    SDL_SetAtomicInt(&g_rate_hz, (int)hz);
    if (SDL_GetAtomicInt(&g_state) == 2) {
        const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 1, (int)hz};
        if (g_spk) {
            SDL_SetAudioStreamFormat(g_spk, &spec, NULL);
        }
        if (g_mic) {
            SDL_SetAudioStreamFormat(g_mic, NULL, &spec);
            SDL_ClearAudioStream(g_mic);
        }
        g_primed = false;
    }
}

static void a_mic_read(uint8_t *buf, int len)
{
    memset(buf, 0, (size_t)len);
    ensure_open();
    if (SDL_GetAtomicInt(&g_state) != 2 || !g_mic) {
        return;
    }
    const int bpms = BYTES_PER_MS(SDL_GetAtomicInt(&g_rate_hz));
    const int avail = SDL_GetAudioStreamAvailable(g_mic);
    /* a stream of 30 ms before it is served from (the device hands over its samples in chunks of several ms, the
     * USB frames take them 1 ms at a time), and no more than 100 ms behind: the oldest goes */
    if (!g_primed) {
        if (avail < 30 * bpms) {
            return;
        }
        g_primed = true;
    }
    if (avail > 100 * bpms) {
        static uint8_t drop[4096];
        int extra = avail - 40 * bpms;
        while (extra > 0) {
            const int n = SDL_GetAudioStreamData(g_mic, drop, extra < (int)sizeof(drop) ? extra : (int)sizeof(drop));
            if (n <= 0) {
                break;
            }
            extra -= n;
        }
    }
    if (SDL_GetAudioStreamAvailable(g_mic) < len) {
        g_primed = false;           /* ran dry: fill up again before more */
        return;
    }
    SDL_GetAudioStreamData(g_mic, buf, len);
}

static void a_speaker_write(const uint8_t *buf, int len)
{
    ensure_open();
    if (SDL_GetAtomicInt(&g_state) != 2 || !g_spk) {
        return;
    }
    if (SDL_GetAudioStreamQueued(g_spk) > 200 * BYTES_PER_MS(SDL_GetAtomicInt(&g_rate_hz))) {
        SDL_ClearAudioStream(g_spk);        /* 200 ms behind: start over rather than lag */
    }
    SDL_PutAudioStreamData(g_spk, buf, len);
}

void nc_xblc_audio_install(void)
{
    SDL_SetAtomicInt(&g_rate_hz, (int)nc_xblc_rate());
    static const NcXblcAudio a = {a_set_rate, a_mic_read, a_speaker_write};
    nc_xblc_set_audio(&a);
}
#else
void nc_xblc_audio_install(void)
{
}
#endif
