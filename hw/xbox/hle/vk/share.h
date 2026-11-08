/*
 * hakuX HLE GPU — the window's pictures handed to another process (Windows).
 *
 * The game's frame (native/app/win_frame.c) owns the one top-level window of
 * a game session and presents to it; each run of the emulator draws its
 * final picture (the frame scaled, filtered, the overlay over it) into a
 * ring of images shared with the frame, as it would into a swapchain's.
 * The driver sees the frame's process presenting to a full screen window
 * of its own: independent flip and G-Sync / FreeSync as for any game, and
 * a run started again changes where the pictures come from, not the window.
 *
 * Everything is named (Local\xbased-<token>-...): the frame creates the
 * block and its event, a run its images and two timeline semaphores
 * (opaque Win32 NT handles) and writes their names into its source; the
 * frame opens them by name.
 */
#ifndef HLE_VK_SHARE_H
#define HLE_VK_SHARE_H

#include <stdint.h>

#define HLE_SHARE_MAGIC   0x58424652u  /* "XBFR" */
#define HLE_SHARE_VERSION 1u
#define HLE_SHARE_IMAGES  3u
#define HLE_SHARE_NAME    64           /* wchar_t, with the terminator */
#define HLE_SHARE_MICE    64           /* the mouse's events from the frame, a ring */

/* one run drawing for the frame */
typedef struct HleShareSrc {
    volatile int64_t gen;              /* its images' set (0: none yet); written last */
    volatile int64_t order;            /* the share's count when it came: the newest shows */
    uint32_t pid;
    uint32_t w, h, fmt;                /* its images (VkFormat) */
    uint32_t vsync;                    /* the present mode it asks for: 1 FIFO, 0 immediate */
    uint8_t luid[8];                   /* its device's (VkPhysicalDeviceIDProperties) */
    uint32_t luid_valid;
    uint64_t size[HLE_SHARE_IMAGES];   /* the images' allocations (dedicated) */
    uint16_t mem[HLE_SHARE_IMAGES][HLE_SHARE_NAME];
    uint16_t drawn[HLE_SHARE_NAME];    /* its timeline: picture v done drawing */
    uint16_t freed[HLE_SHARE_NAME];    /* the frame's on it: picture v copied (each before too) */
    volatile int64_t latest;           /* (v << 2) | image: its newest picture */
    volatile int64_t shown;            /* the frame's: the newest it presented (v) */
} HleShareSrc;

typedef struct HleShareMouse {
    uint32_t type;                     /* 1 move, 2 button down, 3 button up, 4 wheel */
    int32_t x, y;                      /* client pixels */
    int32_t button, wheel;             /* SDL_BUTTON_*, wheel steps (x120 -> 1) */
} HleShareMouse;

typedef struct HleShare {
    uint32_t magic, version;
    volatile uint32_t cw, ch;          /* the frame's client area, pixels */
    volatile int64_t orders;           /* sources so far */
    volatile int32_t cursor;           /* the run wants the pointer shown */
    volatile int32_t failed;           /* the frame can't present them: the run's window its own */
    volatile int64_t mouse_head;       /* written by the frame */
    HleShareMouse mouse[HLE_SHARE_MICE];
    HleShareSrc src[2];
} HleShare;

/* the block's and the new-picture event's names for a frame's token */
#define HLE_SHARE_BLOCK_FMT  L"Local\\xbased-%hs-share"
#define HLE_SHARE_EVENT_FMT  L"Local\\xbased-%hs-new"

#endif
