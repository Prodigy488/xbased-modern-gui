/*
 * xbased (hakuX for PC) — the front end: an SDL3 window, the controllers and
 * the keyboard, the core on its own thread, the HLE GPU's frames presented
 * into the window once per display refresh (present.c), as the Android
 * app's NativeCoreActivity and app_main.c do.
 *
 *   hakux [disc image] [option...]
 *
 * With no disc image, a file dialog asks for one. Options are the Android
 * app's arguments: the core's (native/core/options.h: scale=, loadstate=,
 * video=, lang=, net=, mods=, uncap, ...), the window's (aspect=auto|4:3|
 * 16:9|stretch, fxaa=0|1, upscale=bilinear|fsr), and
 *   hdd=DIR      the hard disk folder (default: hdd beside the executable)
 *   mod=ID       a mod of the game's on (its folder in mods\<TITLE ID>.<version>
 *                beside the executable; mod=* all of them), cheat=NAME a cheat
 *   fullscreen=0 start in a window (full screen, borderless, otherwise)
 *   console      a console window for the log (it always goes to xbased.log)
 * xbased.ini beside the executable holds options too, one a line ('#'
 * comments): the command line's come after, and win.
 *
 * Keys: F11 or Alt+Enter full screen, F2 pause, F5 save the state, F8 load
 * it (slot 1; Shift: slot 2), Esc leaves full screen. Player 1 plays on the
 * keyboard while no controller is plugged in (win_keys below).
 *
 * Loading a state, a title's relaunch (XLaunchNewImage) and its reboot
 * start the core again: the process runs itself again with the option the
 * Android app passes its activity (loadstate=, relaunch).
 */
#include <math.h>

#include "native/app/app.h"
#include "native/app/ui/autofix.h"
#include "native/app/ui/compat_scan.h"
#include "native/app/ui/config.h"
#include "native/app/ui/console_cfg.h"
#include "native/app/ui/dash.h"
#include "native/app/ui/i18n.h"
#include "native/app/ui/input_map.h"
#include "native/app/ui/sbc_map.h"
#include "native/app/ui/ui.h"
#include "hw/xbox/hle/hitch.h"
#include "hw/xbox/hle/vk/swapchain.h"
#include "native/app/ui/xbox_data.h"
#include "native/app/ui/xemu_import.h"
#include "native/app/win_aslr.h"
#include "native/app/win_auto.h"
#include "native/app/win_mods.h"
#include "native/core/cheats.h"
#include "native/core/core.h"
#include "native/core/mods.h"
#include "native/core/options.h"
#include "native/dev/usb.h"
#include "native/kernel/kernel.h"
#include "native/net/net.h"
#include "native/state/save.h"
#include "native/hle/hle_native.h"
#include "hw/xbox/hle/vk/native_window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "native/app/desk.h"   /* windows.h, or its few calls over POSIX */
#ifdef _WIN32
#include <shellapi.h>
#endif

/* native/dev/apu/out.c */
void nc_audio_set_paused(bool on);
void nc_audio_set_volume(float v);
/* native/dev/ohci.c */
void nc_dev_ohci_set_pads(unsigned mask);
/* hw/xbox/hle/pb/pbmode.h */
bool hle_pb_active(void);

#define MAX_ARGS 512

static NcCore g_core;
static int g_disc_fd = -1;
static SDL_AtomicInt g_core_state; /* 0 booting, 1 running, -1 failed or stopped */
static struct ANativeWindow g_nw;
static char g_exe_dir[MAX_PATH];
static char g_data_dir[MAX_PATH]; /* settings, saves, states, the log (cfg_data_dir) */
static char g_disc[1024];
static char *g_args[MAX_ARGS];     /* the options: xbased.ini's, the game's, the command line's */
static int g_nargs;
/* The command line's own options, which a run started again gets: the
 * settings' and the game's are read again by the new run. Passing all of
 * them on, each run added the settings again: a few relaunches or loaded
 * states filled the list, and what came last (the state to load, the
 * relaunch flag) was dropped, so a state loaded restarted the game, and a
 * title that relaunches (Chicago Enforcer) did so over and over. */
static char *g_cmdline[MAX_ARGS];
static int g_ncmdline;
static int g_ncfg_args;            /* how many of them xbased.ini gave */
static ImMap g_map;     /* the controls: the game's file over xbased.ini (input_map.h) */
static char g_key[24];  /* the game's "<TITLE ID>.<version>" */
static uint32_t g_title_id;  /* ... its title id */
static int g_sbc_opt = -1;   /* sbc=1/0: the Steel Battalion controller on/off; -1: on for Steel Battalion */
static bool g_sbc_on;        /* player 1 has it, not a gamepad */

/* ------------------------------------------------------------ helpers */

static void exe_dir(void)
{
    DWORD n = GetModuleFileNameA(NULL, g_exe_dir, sizeof(g_exe_dir));
    while (n && g_exe_dir[n - 1] != '\\' && g_exe_dir[n - 1] != '/') {
        n--;
    }
    g_exe_dir[n] = 0;
    snprintf(g_data_dir, sizeof(g_data_dir), "%s", cfg_data_dir());
}

static void add_arg(const char *a)
{
    static bool told;
    if (g_nargs < MAX_ARGS && a && a[0]) {
        g_args[g_nargs++] = strdup(a);
    } else if (a && a[0] && !told) {
        told = true;
        nc_log("hakuX-app", "more than %d options: %s and those after it left out", MAX_ARGS, a);
    }
}

/* xbased.ini: one option a line (hakux.ini, an earlier build's, becomes it) */
static void read_ini(void)
{
    char path[MAX_PATH + 16], old[MAX_PATH + 16];
    snprintf(path, sizeof(path), "%sxbased.ini", g_data_dir);
    snprintf(old, sizeof(old), "%shakux.ini", g_data_dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        MoveFileA(old, path);
    }
    {   /* the old shader settings gone first (cfg_shaders_migrate) */
        Config c;
        cfg_load(&c, path);
        if (cfg_shaders_migrate(&c)) {
            cfg_save(&c);
        }
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        size_t n = strlen(p);
        while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) {
            p[--n] = 0;
        }
        if (n && p[0] != '#' && p[0] != ';') {
            add_arg(p);
        }
    }
    fclose(f);
}

static const char *opt_value(const char *name)
{
    const size_t n = strlen(name);
    const char *v = NULL;
    for (int i = 0; i < g_nargs; i++) {
        if (!strncmp(g_args[i], name, n) && g_args[i][n] == '=') {
            v = g_args[i] + n + 1; /* the last one wins */
        }
    }
    return v;
}

static bool opt_flag(const char *name)
{
    for (int i = 0; i < g_nargs; i++) {
        if (!strcmp(g_args[i], name)) {
            return true;
        }
    }
    return false;
}

/* Full screen (borderless, the desktop's mode) unless told otherwise (Settings > Video >
 * "Start in full screen" off, or "fullscreen=0"): a console's picture, not a window's.
 * F11 and Alt+Enter switch at any time. */
static bool want_fullscreen(void)
{
    const char *v = opt_value("fullscreen");
    return v ? strcmp(v, "0") != 0 : true;
}

/* The game's own options (games/<TITLE ID>.<version>.ini) after the global
 * ones and before the command line's (which come last and win); the
 * console's language and video flags as console_cfg.c keeps them; the dashboard's
 * "uncap=1" as the flag the core takes, "fullscreen" (on unless "=0") once. */
static void game_options(const char *disc)
{
    XdGame g;
    if (disc) {
        xd_read_game(disc, &g);
        xd_free_game(&g);
    } else {
        /* the Xbox dashboard (no disc): settings of its own ("dash"), the global ones under them */
        memset(&g, 0, sizeof(g));
        snprintf(g.key, sizeof(g.key), "dash");
    }
    char path[MAX_PATH + 64];
    snprintf(path, sizeof(path), "%sgames/%s.ini", g_data_dir, g.key);
    Config c;
    cfg_load(&c, path);
    if (cfg_shaders_migrate(&c)) {
        cfg_save(&c);
    }
    /* the command line's options after the game's: move them to the end */
    char *cmdline[MAX_ARGS];
    int ncmd = 0;
    for (int i = g_ncfg_args; i < g_nargs; i++) {
        cmdline[ncmd++] = g_args[i];
    }
    g_nargs = g_ncfg_args;
    for (int i = 0; i < c.n; i++) {
        char a[600];
        if (c.flag[i]) {
            snprintf(a, sizeof(a), "%s", c.key[i]);
        } else {
            snprintf(a, sizeof(a), "%s=%s", c.key[i], c.val[i]);
        }
        add_arg(a);
    }
    for (int i = 0; i < ncmd; i++) {
        if (g_nargs < MAX_ARGS) {
            g_args[g_nargs++] = cmdline[i];
        }
    }
    if (c.n) {
        nc_log("hakuX-app", "%d options of the game's (%s)", c.n, path);
    }
    snprintf(g_key, sizeof(g_key), "%s", g.key);
    g_title_id = g.title_id;
    Config global;
    snprintf(path, sizeof(path), "%sxbased.ini", g_data_dir);
    cfg_load(&global, path);
    im_resolve(&g_map, &global, &c);
    /* flags written as values */
    static const char *const k_flags[] = {"uncap", "fullscreen"};
    for (int f = 0; f < 2; f++) {
        const char *v = opt_value(k_flags[f]);
        bool on = opt_flag(k_flags[f]) || f == 1;  /* full screen by default */
        if (v) {
            on = strcmp(v, "0") != 0;
        }
        for (int i = 0; i < g_nargs; i++) {
            const size_t n = strlen(k_flags[f]);
            if (!strncmp(g_args[i], k_flags[f], n) && (g_args[i][n] == '=' || g_args[i][n] == 0)) {
                g_args[i][0] = 0;  /* gone; added back once below */
            }
        }
        if (on) {
            add_arg(k_flags[f]);
        } else if (f == 1) {
            add_arg("fullscreen=0");
        }
    }
    i18n_set(opt_value("ui.lang"));     /* the app's messages */
    /* The console's language and video flags: what changed in the EEPROM
     * taken into the global settings first (console_cfg.c), then the
     * command line's, else the game's own, else the global ones (for "auto"
     * or none, the interface's language). The game's own language, or the
     * command line's, is kept even on a disc that seems to lack it. */
    cc_sync(&global);
    const char *cl_lang = NULL, *cl_video = NULL;
    for (int i = 0; i < ncmd; i++) {
        if (!strncmp(cmdline[i], "lang=", 5)) {
            cl_lang = cmdline[i] + 5;
        } else if (!strncmp(cmdline[i], "video=", 6)) {
            cl_video = cmdline[i] + 6;
        }
    }
    const int asked = cl_lang ? atoi(cl_lang) : cfg_get(&c, "lang") ? atoi(cfg_get(&c, "lang")) : 0;
    const bool fixed = asked >= 1 && asked <= 9;
    char a[32];
    snprintf(a, sizeof(a), "lang=%d", fixed ? asked : cc_lang(&global));
    add_arg(a);
    if (fixed) {
        add_arg("langfixed=1");
    }
    if (!cl_video && !cfg_get(&c, "video")) {
        snprintf(a, sizeof(a), "video=0x%x", cc_video(&global));
        add_arg(a);
    }
}

static void make_hdd(const char *root)
{
    char p[1024];
    mkdir(root, 0775);
    for (int i = 0; i <= 5; i++) {
        snprintf(p, sizeof(p), "%s/Partition%d", root, i);
        mkdir(p, 0775);
    }
}

/* The game's window has the screen: the one shown next (the dashboard back,
 * a relaunched title's) is to have it, with the keyboard and the gamepad.
 * Windows locks the foreground: without help that one stayed behind
 * whatever window came next, its taskbar button flashing, as if minimized.
 * Any process may take it (the grant); and back to the dashboard, the game
 * shows the dashboard's window and gives it the foreground itself (its
 * owner may give it to any window): the dashboard that started it says its
 * window in XBASED_DASH_HWND. The grant alone left the dashboard without
 * the gamepad on an ROG Xbox Ally X. While this process still has the
 * foreground: before its window hides (hidden, another window has it). */
static void give_foreground(bool to_dash)
{
#ifdef _WIN32
    AllowSetForegroundWindow(ASFW_ANY);
    const char *e = to_dash ? getenv("XBASED_DASH_HWND") : NULL;
    HWND h = e ? (HWND)(uintptr_t)strtoull(e, NULL, 16) : NULL;
    if (h && IsWindow(h)) {
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
    }
#else
    (void)to_dash;
#endif
}

/* The process ends at once with code. On Windows ExitProcess would first
 * run every DLL's detach (the GPU driver's, the gamepads', the audio's) with
 * the emulation's threads cut off wherever they were, and one waiting on
 * another there hangs the process: a black full-screen window, and the
 * dashboard waiting behind it. TerminateProcess skips that; what the run
 * wrote is written (stderr isn't buffered, files are closed as they go).
 * A code with bit 30 is a run started again (run_again): no dashboard. */
static _Noreturn void exit_now(int code)
{
    fflush(NULL);
#ifdef _WIN32
    give_foreground(!(code & 0x40000000));
    TerminateProcess(GetCurrentProcess(), (UINT)code);
#endif
    _exit(code);
}

static void fatal_box(const char *title, const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
}

static void hold_picture(char *path, size_t n);
static void wait_next_run(void *ev, void *proc);

/* The process again, with the same disc and options plus extra (minus any
 * earlier option of extra's name). hold: the next run shows this one's
 * picture until its first frame (a state loaded, a title relaunched),
 * otherwise black (what this one shows at the end of a transition). On
 * Windows the next run's window comes up under this one, held on top, and
 * this one goes once that one has drawn: no desktop, nor a window without
 * the keyboard, between the two. */
static void run_again(const char *extra, bool hold)
{
    nc_kernel_set_paused(&g_core.kernel, true);    /* the picture held is the last one */
    nc_audio_set_paused(true);
    af_clear_env();     /* the next run reads its fixes again (the game's file, its own fixtrial=) */
    const bool keep_trial = extra && !strcmp(extra, "relaunch");   /* a title's relaunch goes on with the trial */
    char hold_path[MAX_PATH + 32] = "";
    if (hold) {
        hold_picture(hold_path, sizeof(hold_path));
    }
    char cmd[8192];
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    int n = snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", exe, g_disc[0] ? g_disc : "none");
    const char *eq = extra ? strchr(extra, '=') : NULL;
    const size_t name_len = eq ? (size_t)(eq - extra) : extra ? strlen(extra) : 0;
    for (int i = 0; i < g_ncmdline && n < (int)sizeof(cmd) - 8; i++) {
        const char *a = g_cmdline[i];
        if (extra && !strncmp(a, extra, name_len) && (a[name_len] == '=' || a[name_len] == 0)) {
            continue;
        }
        if (!a[0] || !strcmp(a, "relaunch") || !strncmp(a, "loadstate=", 10) ||
            (!keep_trial && !strncmp(a, "fixtrial=", 9))) {
            continue; /* one run's */
        }
        n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " \"%s\"", a);
    }
    if (extra) {
        n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " \"%s\"", extra);
    }
    nc_log("hakuX-app", "running again: %s", cmd);
#ifdef _WIN32
    SetEnvironmentVariableA("XBASED_HOLD", hold_path[0] ? hold_path : NULL);
    char ev_name[96];
    snprintf(ev_name, sizeof(ev_name), "Local\\xbased-ready-%lu-%llu", (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64());
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, ev_name);
    SetEnvironmentVariableA("XBASED_READY", ev ? ev_name : NULL);
    char rect[64] = "";
    if (g_nw.sdl_window && !(SDL_GetWindowFlags(g_nw.sdl_window) & SDL_WINDOW_FULLSCREEN)) {
        int x = 0, y = 0, w = 0, h = 0;
        SDL_GetWindowPosition(g_nw.sdl_window, &x, &y);
        SDL_GetWindowSize(g_nw.sdl_window, &w, &h);
        snprintf(rect, sizeof(rect), "%d,%d,%d,%d", x, y, w, h);
    }
    SetEnvironmentVariableA("XBASED_RECT", rect[0] ? rect : NULL);
    if (g_nw.hwnd) {
        SetWindowPos((HWND)g_nw.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    PROCESS_INFORMATION pi;
    if (xb_spawn(exe, cmd, g_exe_dir, &pi)) {
        CloseHandle(pi.hThread);
        wait_next_run(ev, pi.hProcess);
        CloseHandle(pi.hProcess);
        /* the dashboard waits for this process: the new one's id in the
         * exit code (bit 30 set) lets it follow the game on (dash.c) */
        exit_now((int)(0x40000000u | (pi.dwProcessId & 0x3FFFFFFFu)));
    }
    if (g_nw.hwnd) {
        SetWindowPos((HWND)g_nw.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#else
    if (hold_path[0]) {
        setenv("XBASED_HOLD", hold_path, 1);
    } else {
        unsetenv("XBASED_HOLD");
    }
    /* this process becomes the new run (the same arguments as above) */
    char *argv[MAX_ARGS + 4];
    int k = 0;
    argv[k++] = exe;
    argv[k++] = g_disc[0] ? g_disc : (char *)"none";
    for (int i = 0; i < g_ncmdline; i++) {
        char *a = g_cmdline[i];
        if (extra && !strncmp(a, extra, name_len) && (a[name_len] == '=' || a[name_len] == 0)) {
            continue;
        }
        if (!a[0] || !strcmp(a, "relaunch") || !strncmp(a, "loadstate=", 10) ||
            (!keep_trial && !strncmp(a, "fixtrial=", 9))) {
            continue;
        }
        argv[k++] = a;
    }
    if (extra) {
        argv[k++] = (char *)extra;
    }
    argv[k] = NULL;
    fflush(stderr);
    execv(exe, argv);
#endif
    exit_now(0);
}

/* ------------------------------------------------------------- the core */

static void *core_thread(void *arg)
{
    nc_win_thread_name("vcpu");
    nc_win_thread_priority(1);
    if (!nc_core_boot(&g_core, g_disc_fd)) {
        nc_log("hakuX-app", "core: boot failed");
        if (!nc_hle_fatal()) {
            nc_hle_set_fatal(NC_FATAL_BOOT, "The game stopped while starting.");
        }
        SDL_SetAtomicInt(&g_core_state, -1);
        return NULL;
    }
    SDL_SetAtomicInt(&g_core_state, 1);
    nc_core_run(&g_core);
    nc_log("hakuX-app", "core: stopped");
    SDL_SetAtomicInt(&g_core_state, -1);
    return NULL;
}

/* ------------------------------------------------------------ the input */

/* Controllers by player: each one plugged in takes the first free port. */
static SDL_Gamepad *g_pads[4];
static uint16_t g_rumble[4][2];


/* Player p's gamepad: its device's (auto: the p-th plugged in, padK: the
 * K-th), NULL for the keyboard, none, or nothing plugged. */
static SDL_Gamepad *player_pad(int p)
{
    const int d = g_map.device[p];
    if (d < 0) {
        return NULL;
    }
    const int k = d == 0 ? p : d - 1;
    return k < 4 ? g_pads[k] : NULL;
}

static void pads_changed(void)
{
    unsigned mask = 0;
    for (int p = 0; p < 4; p++) {
        const int d = g_map.device[p];
        /* player 1 always (the keyboard, unless set to none); the others with a pad */
        if (d != -2 && (player_pad(p) || (p == 0 && d <= 0) || d == -1 || d == -3)) {
            mask |= 1u << p;
        }
    }
    nc_dev_ohci_set_pads(mask ? mask : 1);
}

static void pad_added(SDL_JoystickID id)
{
    for (unsigned i = 0; i < 4; i++) {
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
            return;
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        if (!g_pads[i]) {
            g_pads[i] = SDL_OpenGamepad(id);
            nc_log("hakuX-app", "controller for player %u: %s", i + 1,
                   g_pads[i] ? SDL_GetGamepadName(g_pads[i]) : SDL_GetError());
            pads_changed();
            return;
        }
    }
}

/* A joystick SDL has no gamepad mapping for (the generic DirectInput pads):
 * the usual layout of those (buttons 0-3 the face buttons, 4-5 the
 * shoulders, 6-7 the triggers, 8-9 Back and Start, 10-11 the sticks'
 * clicks, the hat the d-pad, axes 0-1 and 2-3 the sticks), so it becomes a
 * gamepad. gamecontrollerdb.txt beside the executable, if there, has the
 * right one for many more. */
static void joystick_added(SDL_JoystickID id)
{
    if (SDL_IsGamepad(id)) {
        return;
    }
    char guid[64];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    const char *name = SDL_GetJoystickNameForID(id);
    char map[512];
    snprintf(map, sizeof(map),
             "%s,%s,a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,"
             "back:b8,start:b9,leftstick:b10,rightstick:b11,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,",
             guid, name ? name : "Joystick");
    nc_log("hakuX-app", "joystick without a gamepad mapping: \"%s\" (%s); the generic layout given", name ? name : "?",
           guid);
    SDL_AddGamepadMapping(map); /* a gamepad now: SDL_EVENT_GAMEPAD_ADDED follows */
}

static void pad_removed(SDL_JoystickID id)
{
    for (unsigned i = 0; i < 4; i++) {
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
            SDL_CloseGamepad(g_pads[i]);
            g_pads[i] = NULL;
            nc_log("hakuX-app", "player %u's controller unplugged", i + 1);
            pads_changed();
        }
    }
}

static SDL_AtomicInt g_saving;
static void save_state(int slot);
static void load_state(int slot);
static void notice(const char *text, const char *after_file);

/* ---- the mouse as a stick (Settings > Controls): a click on the game captures it, F3 or Esc lets go */
static bool g_mouse_cap;            /* captured (relative mouse mode) */
static bool g_mouse_skip;           /* the capturing click is still down: not the game's */
static float g_mouse_dx, g_mouse_dy;    /* motion since the last poll, in pixels */
static float g_mouse_sx, g_mouse_sy;    /* the stick it gives, smoothed */
static Uint64 g_mouse_t;
static Uint64 g_wheel_until[2];     /* the wheel's "buttons" last a moment */

/* The pointer over the game: hidden, and a fully transparent one set as well, so a driver or a
 * Windows state that ignores the hidden flag still shows nothing. The menus get the usual one back. */
static SDL_Cursor *g_blank_cursor;
static void pointer_hide(void)
{
    if (!g_blank_cursor) {
        SDL_Surface *sf = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888);
        if (sf) {
            SDL_ClearSurface(sf, 0, 0, 0, 0);
            g_blank_cursor = SDL_CreateColorCursor(sf, 0, 0);
            SDL_DestroySurface(sf);
        }
    }
    if (g_blank_cursor) {
        SDL_SetCursor(g_blank_cursor);
    }
    SDL_HideCursor();
}

static void pointer_menu(void)
{
    SDL_SetCursor(SDL_GetDefaultCursor());
}

static void mouse_release(SDL_Window *win)
{
    if (g_mouse_cap) {
        SDL_SetWindowRelativeMouseMode(win, false);
        pointer_hide();
    }
    g_mouse_cap = false;
    g_mouse_dx = g_mouse_dy = g_mouse_sx = g_mouse_sy = 0;
}

static void mouse_capture(SDL_Window *win)
{
    if (SDL_SetWindowRelativeMouseMode(win, true)) {
        g_mouse_cap = true;
        g_mouse_skip = true;
        g_mouse_dx = g_mouse_dy = g_mouse_sx = g_mouse_sy = 0;
        g_mouse_t = SDL_GetTicks();
        nc_log("hakuX-app", "mouse captured (stick: %s, sensitivity %d%%)", g_map.mouse_stick ? "left" : "right", (int)(g_map.mouse_sens * 100));
        notice(T(S_MOUSE_CAPTURED), NULL);
    }
}

/* the mouse's part of player 1's state: its stick, and its buttons as the pad's */
static void mouse_apply(ImPad *m)
{
    const Uint64 now = SDL_GetTicks();
    const float dt = (float)(now - g_mouse_t > 0 ? now - g_mouse_t : 1);
    g_mouse_t = now;
    /* pixels a millisecond: 1.5 of them (1500 a second) is the stick's full travel at 1x */
    const float k = g_map.mouse_sens / 1.5f;
    float tx = g_mouse_dx / dt * k, ty = -g_mouse_dy / dt * k * (g_map.mouse_invert ? -1.0f : 1.0f);
    g_mouse_dx = g_mouse_dy = 0;
    tx = tx < -1 ? -1 : tx > 1 ? 1 : tx;
    ty = ty < -1 ? -1 : ty > 1 ? 1 : ty;
    const float a = 1.0f - expf(-dt / 30.0f);   /* ~30 ms: the counts of a few polls make one move */
    g_mouse_sx += (tx - g_mouse_sx) * a;
    g_mouse_sy += (ty - g_mouse_sy) * a;
    float sx = fabsf(g_mouse_sx) < 0.01f ? 0 : g_mouse_sx, sy = fabsf(g_mouse_sy) < 0.01f ? 0 : g_mouse_sy;
    if (sx != 0 || sy != 0) {
        /* past the game's own dead zone at once, as the sticks' anti dead zone does */
        const float r = sqrtf(sx * sx + sy * sy);
        const float lift = g_map.stick_antidz;
        const float f = r > 0 ? (lift + (r > 1 ? 1 : r) * (1 - lift)) / r : 0;
        sx = sx * f > 1 ? 1 : sx * f < -1 ? -1 : sx * f;
        sy = sy * f > 1 ? 1 : sy * f < -1 ? -1 : sy * f;
        int16_t *px = g_map.mouse_stick ? &m->lx : &m->rx, *py = g_map.mouse_stick ? &m->ly : &m->ry;
        if (fabsf(sx * 32767) > abs(*px)) {
            *px = (int16_t)(sx * 32767);
        }
        if (fabsf(sy * 32767) > abs(*py)) {
            *py = (int16_t)(sy * 32767);
        }
    }
    unsigned down = 0;
    if (!g_mouse_skip) {
        const SDL_MouseButtonFlags b = SDL_GetMouseState(NULL, NULL);
        down |= (b & SDL_BUTTON_LMASK) ? 1u : 0u;
        down |= (b & SDL_BUTTON_RMASK) ? 2u : 0u;
        down |= (b & SDL_BUTTON_MMASK) ? 4u : 0u;
        down |= (b & SDL_BUTTON_X1MASK) ? 8u : 0u;
        down |= (b & SDL_BUTTON_X2MASK) ? 16u : 0u;
    } else if (!(SDL_GetMouseState(NULL, NULL) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK))) {
        g_mouse_skip = false;
    }
    down |= now < g_wheel_until[0] ? 32u : 0u;
    down |= now < g_wheel_until[1] ? 64u : 0u;
    for (int i = 0; i < IM_MOUSE_BTNS; i++) {
        if (!(down & (1u << i))) {
            continue;
        }
        switch (g_map.mouse_btn[i]) {
        case 1: m->analog[0] = 255; break;                  /* A */
        case 2: m->analog[1] = 255; break;                  /* B */
        case 3: m->analog[2] = 255; break;                  /* X */
        case 4: m->analog[3] = 255; break;                  /* Y */
        case 5: m->buttons |= 0x20; break;                  /* Back */
        case 6: m->buttons |= 0x10; break;                  /* Start */
        case 7: m->buttons |= 0x40; break;                  /* left stick click */
        case 8: m->buttons |= 0x80; break;                  /* right stick click */
        case 9: m->analog[5] = 255; break;                  /* White */
        case 10: m->analog[4] = 255; break;                 /* Black */
        case 11: m->buttons |= 1; break;                    /* d-pad up, down, left, right */
        case 12: m->buttons |= 2; break;
        case 13: m->buttons |= 4; break;
        case 14: m->buttons |= 8; break;
        case 15: m->analog[6] = 255; break;                 /* left trigger */
        case 16: m->analog[7] = 255; break;                 /* right trigger */
        default: break;
        }
    }
}

/* The controller input of one port, from several sources: what is pressed on any of them */
static void pad_merge(ImPad *to, const ImPad *from)
{
    to->buttons |= from->buttons;
    for (int i = 0; i < 8; i++) {
        if (from->analog[i] > to->analog[i]) {
            to->analog[i] = from->analog[i];
        }
    }
    if (abs(from->lx) + abs(from->ly) > abs(to->lx) + abs(to->ly)) {
        to->lx = from->lx;
        to->ly = from->ly;
    }
    if (abs(from->rx) + abs(from->ry) > abs(to->rx) + abs(to->ry)) {
        to->rx = from->rx;
        to->ry = from->ry;
    }
}

/* Back held with a bumper (or the combo chosen): a quick save to slot 1, the other side loads it */
static void quick_buttons(void)
{
    static bool was_save, was_load;
    static Uint64 last;
    bool save = false, load = false;
    if (g_map.quick) {
        for (int k = 0; k < 4; k++) {
            SDL_Gamepad *pad = g_pads[k];
            if (!pad || !SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) ||
                SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) {
                continue;
            }
            switch (g_map.quick_combo) {
            case 1:
                save |= SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
                load |= SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
                break;
            case 2:
                save |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
                load |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
                break;
            case 3:
                save |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
                load |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
                break;
            default:
                save |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
                load |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
                break;
            }
        }
    }
    const Uint64 now = SDL_GetTicks();
    const bool ready = now - last > 1500 && SDL_GetAtomicInt(&g_core_state) == 1 && !SDL_GetAtomicInt(&g_saving);
    if (save && !was_save && ready) {
        last = now;
        save_state(1);
    } else if (load && !was_load && ready) {
        last = now;
        load_state(1);
    }
    was_save = save;
    was_load = load;
}

static void update_pads(bool focused)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    for (unsigned i = 0; i < 4; i++) {
        NcPadState s;
        memset(&s, 0, sizeof(s));
        const bool all = g_map.device[i] == -3;         /* the keyboard, the mouse and every controller */
        SDL_Gamepad *pad = player_pad((int)i);
        if (i == 0 && g_sbc_on) {
            /* the Steel Battalion controller: its controls from the keys and the pad (sbc_map.c) */
            NcSbcState sb;
            sbc_read(focused ? keys : NULL, pad, &sb);
            nc_sbc_set(&sb);
        }
        /* the keyboard: player 1 on auto, any player set to it */
        const bool kb = focused && (g_map.device[i] == -1 || all || (i == 0 && g_map.device[i] == 0));
        ImPad m;
        if (all) {
            im_read(&g_map, (int)i, kb ? keys : NULL, NULL, &m);
            for (int k = 0; k < 4; k++) {
                if (g_pads[k]) {
                    ImPad t;
                    im_read(&g_map, (int)i, NULL, g_pads[k], &t);
                    pad_merge(&m, &t);
                }
            }
        } else {
            im_read(&g_map, (int)i, kb ? keys : NULL, pad, &m);
        }
        /* the mouse is the keyboard's: with it in play (player 1 on auto, keyboard or all) */
        if (g_mouse_cap && g_map.mouse && kb && focused) {
            mouse_apply(&m);
        }
        s.buttons = m.buttons;
        memcpy(s.analog, m.analog, sizeof(s.analog));
        s.lx = m.lx;
        s.ly = m.ly;
        s.rx = m.rx;
        s.ry = m.ry;
        if (i == 0) {
            nc_auto_pad(&s);
        }
        nc_pad_set(i, &s);
        uint16_t l = 0, r = 0;
        nc_pad_rumble(i, &l, &r);
        if (!g_map.rumble) {
            l = r = 0;
        } else if (g_map.rumble_scale < 0.999f) {
            l = (uint16_t)(l * g_map.rumble_scale);
            r = (uint16_t)(r * g_map.rumble_scale);
        }
        if ((pad || all) && (l != g_rumble[i][0] || r != g_rumble[i][1])) {
            if (all) {
                for (int k = 0; k < 4; k++) {
                    if (g_pads[k]) {
                        SDL_RumbleGamepad(g_pads[k], l, r, l || r ? 1000 : 0);
                    }
                }
            } else {
                SDL_RumbleGamepad(pad, l, r, l || r ? 1000 : 0);
            }
            g_rumble[i][0] = l;
            g_rumble[i][1] = r;
        }
    }
    if (!g_sbc_on) {
        quick_buttons();                    /* Back is the Steel Battalion panel's */
    }
}

/* ------------------------------------------------------------ states */

/* states\<TITLE ID>.<version>\slot<N>.hxs, its picture slot<N>.bmp beside */
static void state_path(int slot, char *out, size_t n)
{
    char dir[MAX_PATH + 64];
    snprintf(dir, sizeof(dir), "%sstates", g_data_dir);
    mkdir(dir, 0775);
    snprintf(dir, sizeof(dir), "%sstates/%s", g_data_dir, g_key);
    mkdir(dir, 0775);
    snprintf(out, n, "%s/slot%d.hxs", dir, slot);
}

/* The save runs on a thread of its own, as NativeCoreActivity's does: it
 * waits for the GPU to park at a frame, which the window's thread presents
 * meanwhile (from that thread, it waited on itself). */
static SDL_AtomicInt g_saving;
static SDL_AtomicInt g_saved_slot;  /* for the notice: slot + 1, or -1 not now (overlay_tick) */
static void notice(const char *text, const char *after_file);

static void *save_thread(void *arg)
{
    const int slot = (int)(intptr_t)arg;
    char path[MAX_PATH * 2];
    state_path(slot, path, sizeof(path));
    const char *err = nc_state_save(path);
    nc_log("hakuX-app", "savestate %d: %s", slot, err ? err : "saved");
    if (!err) {
        /* its picture: the frame shown, the dashboard's thumbnail */
        char pic[MAX_PATH * 2];
        snprintf(pic, sizeof(pic), "%.*s.bmp", (int)(strlen(path) - 4), path);
        remove(pic);
        hle_present_shot(pic);
        /* the notice after the picture: not in it */
        for (int i = 0; i < 100 && GetFileAttributesA(pic) == INVALID_FILE_ATTRIBUTES; i++) {
            SDL_Delay(10);
        }
    }
    SDL_SetAtomicInt(&g_saved_slot, err ? -1 : slot + 1);
    SDL_SetAtomicInt(&g_saving, 0);
    return NULL;
}

static void save_state(int slot)
{
    if (SDL_GetAtomicInt(&g_core_state) != 1 || !hle_pb_active()) {
        nc_log("hakuX-app", "savestate: not available now");
        SDL_SetAtomicInt(&g_saved_slot, -1);
        return;
    }
    if (!SDL_CompareAndSwapAtomicInt(&g_saving, 0, 1)) {
        return; /* one at a time */
    }
    pthread_t t;
    if (pthread_create(&t, NULL, save_thread, (void *)(intptr_t)slot) == 0) {
        pthread_detach(t);
    } else {
        SDL_SetAtomicInt(&g_saving, 0);
    }
}

static void load_state(int slot)
{
    char path[MAX_PATH * 2], opt[MAX_PATH * 2 + 16];
    state_path(slot, path, sizeof(path));
    const char *err = nc_state_compat(path);
    if (err) {
        nc_log("hakuX-app", "savestate %d: %s", slot, err);
        return;
    }
    snprintf(opt, sizeof(opt), "loadstate=%s", path);
    run_again(opt, true);
}

static bool g_uncap;    /* the run's frame rate setting (fast forward goes back to it) */
static int g_fps_cap;   /* ... its limit, uncapped (0: none) */
static float g_volume = 1.0f;
static bool g_mute_bg = true;   /* silent while the window isn't the active one */
static bool g_pause_bg;        /* hold the game while the window is in the background */
static bool g_autosave = true; /* a save state of slot 0 when the game is left: "Resume" picks it up */
static bool g_is_dash;         /* this process is the Xbox dashboard (no disc) */
static bool g_quit_pending;    /* the exit's save is running; the loop leaves when it ends */
static bool g_quit_close;      /* ... leaving the program, not just the game */
static uint64_t g_quit_deadline;

/* screenshots\<title>-<date>.bmp beside the executable */
static void screenshot(void)
{
    char dir[MAX_PATH + 32], p[MAX_PATH + 128];
    snprintf(dir, sizeof(dir), "%sscreenshots", g_data_dir);
    mkdir(dir, 0775);
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(p, sizeof(p), "%s/%s-%04d%02d%02d-%02d%02d%02d.bmp", dir, g_key, t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    hle_present_shot_full(p);       /* at the render scale's resolution, not the window's */
    notice(T(S_SHOT_SAVED), p);
}

/* ------------------------------------------------------------ the overlay */

/* The interface is the game's overlay (ui_init_overlay): its presenter
 * composites it over the picture in its own window pass, so the window is
 * never handed to anything else. Up only while something is drawn (the
 * in-game menu, a transition, a notice); otherwise the presenter draws as
 * if there were none. */
static bool g_fx = true;        /* ui.fx: the transitions */
static UiTex *g_hold;           /* the picture the run before this one left (XBASED_HOLD) */
static char g_hold_path[MAX_PATH + 32];
static int g_boot;              /* 0 before the first frame, 1 coming in, 2 done */
static double g_boot_t0;
static bool g_ov_up;            /* an overlay of the window loop's is set */
static char g_notice[200];
static double g_notice_t0;
static char g_notice_file[MAX_PATH + 160];  /* shown once this is written (a picture) */
#ifdef _WIN32
static HANDLE g_ready_ev;       /* the run before this one waits on it (run_again) */
#endif

/* The refresh rate of the display the window is on, for the presenter (frame interpolation). */
static void refresh_note(SDL_Window *w)
{
    const SDL_DisplayMode *m = w ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(w)) : NULL;
    nc_present_set_refresh(m ? m->refresh_rate : 0.0f);
}

static void ov_present(void)
{
    if (g_nw.width <= 0 || !nc_present_frame(&g_nw)) {
        SDL_Delay(8);
    }
}

static void ov_size(int *w, int *h)
{
    *w = g_nw.width;
    *h = g_nw.height;
}

static bool ov_ok(void)
{
    return ui_is_overlay() && hle_ov_ready();
}

/* A picture of what the window shows now, for the next run (run_again). */
static void hold_picture(char *path, size_t n)
{
    path[0] = 0;
    if (!ov_ok()) {
        return;
    }
    ui_overlay_off();
    g_ov_up = false;
    snprintf(path, n, "%shandoff.bmp", g_data_dir);
    remove(path);
    hle_present_shot(path);
    for (int i = 0; i < 60 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES; i++) {
        ov_present();
    }
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        path[0] = 0;
    }
}

/* The next run's window under this one's until it has drawn (its event),
 * it ended, or 15 seconds: this window keeps showing what it shows. */
static void wait_next_run(void *ev, void *proc)
{
#ifdef _WIN32
    const uint64_t until = SDL_GetTicks() + 15000;
    while (SDL_GetTicks() < until) {
        if ((ev && WaitForSingleObject((HANDLE)ev, 0) == WAIT_OBJECT_0) ||
            WaitForSingleObject((HANDLE)proc, 0) == WAIT_OBJECT_0) {
            break;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        }
        ov_present();
    }
    if (ev) {
        CloseHandle((HANDLE)ev);
    }
#else
    (void)ev;
    (void)proc;
#endif
}

/* Out of the game: its picture breaks into shards, black behind (what the
 * dashboard's window, or the next run's, comes up on). */
static void fx_out(void)
{
    if (!ov_ok()) {
        return;
    }
    const double t0 = ui_time();
    for (;;) {
        const float k = g_fx ? (float)((ui_time() - t0) / 0.5) : 1.0f;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
        }
        nc_auto_tick();     /* scripted pictures of it too */
        ui_begin();
        ui_cover(true);
        ui_fx_shatter(ui_scene(false), k);
        ui_end();
        if (k >= 1) {
            break;
        }
    }
    g_ov_up = true;     /* black stays up */
}

static void notice(const char *text, const char *after_file)
{
    snprintf(g_notice, sizeof(g_notice), "%s", text);
    snprintf(g_notice_file, sizeof(g_notice_file), "%s", after_file ? after_file : "");
    g_notice_t0 = after_file ? 0 : ui_time();
}

/* Each refresh of the window's loop, before it: the overlay it needs, if
 * any. Before the game's first frame, the picture the run before left (or
 * black); then that picture fading into the game's, or the game's rushing
 * in out of white; a notice over the game a moment. */
static void overlay_tick(void)
{
    if (!ui_is_overlay()) {
        return;
    }
    const int saved = SDL_SetAtomicInt(&g_saved_slot, 0);
    if (saved) {
        char t[200];
        if (saved > 0) {
            snprintf(t, sizeof(t), T(S_STATE_SAVED), saved - 1);
        } else {
            snprintf(t, sizeof(t), "%s", T(S_STATE_NOT_NOW));
        }
        notice(t, NULL);
    }
    if (g_notice_file[0] && GetFileAttributesA(g_notice_file) != INVALID_FILE_ATTRIBUTES) {
        g_notice_file[0] = 0;   /* written: the notice now, not in the picture */
        g_notice_t0 = ui_time();
    }
    const double now = ui_time();
    if (g_boot < 2) {
        const bool shown = nc_present_shown();
        if (!shown && (g_hold || g_fx)) {
            ui_begin();
            if (g_hold) {
                ui_fill(g_hold, UI_WHITE);
                /* a while on: darker, and the console's ring turning */
                const float d = (float)(now - 0.8);
                if (d > 0) {
                    ui_rect(0, 0, ui_w(), ui_h(), (UiColor){0, 0, 0, (uint8_t)(d > 1 ? 110 : 110 * d)});
                    const float a = d > 0.5f ? 1.0f : d * 2;
                    ui_ring(ui_w() - 64, ui_h() - 64, 22, 22, 0, 3, ui_alpha(UI_GREEN_DIM, 0.6f * a));
                    const float r = (float)(now * 5.0);
                    for (int i = 0; i < 3; i++) {
                        ui_circle(ui_w() - 64 + 22 * cosf(r + i * 0.35f), ui_h() - 64 + 22 * sinf(r + i * 0.35f),
                                  3.2f - i * 0.6f, ui_alpha(UI_GREEN_HI, a * (1 - i * 0.25f)));
                    }
                }
            }
            ui_overlay_submit(true);
            g_ov_up = true;
            return;
        }
        if (shown && g_boot == 0) {
            g_boot = 1;
            g_boot_t0 = now;
        }
        const double len = g_hold ? 0.3 : 0.42;
        if (g_boot == 1 && now - g_boot_t0 < len && (g_hold || g_fx)) {
            const float k = (float)((now - g_boot_t0) / len);
            ui_begin();
            if (g_hold) {
                ui_fill(ui_scene(false), UI_WHITE);
                ui_fill(g_hold, ui_alpha(UI_WHITE, 1 - k));
            } else {
                ui_fx_flash_in(ui_scene(false), k, (UiColor){255, 255, 255, 230});
            }
            ui_overlay_submit(true);
            g_ov_up = true;
            return;
        }
        if (shown || !(g_hold || g_fx)) {
            g_boot = 2;
            if (g_hold) {
                ui_texture_free(g_hold);
                g_hold = NULL;
                remove(g_hold_path);
            }
        }
    }
    if (g_notice[0] && g_notice_t0 > 0 && now - g_notice_t0 < 2.4) {
        const float t = (float)(now - g_notice_t0);
        const float a = t < 0.15f ? t / 0.15f : t > 2.0f ? (2.4f - t) / 0.4f : 1.0f;
        ui_begin();
        ui_set_alpha(a);
        const float sz = 22, w = ui_text_width(g_notice, sz, UI_FONT_REGULAR) + 56;
        const float x = (ui_w() - w) / 2, y = ui_h() - 96 + (1 - a) * 12;
        ui_round(x, y, w, 46, 23, ui_th(0x08, 0x16, 0x05, 0xE0));
        ui_round_outline(x, y, w, 46, 23, 1.5f, ui_alpha(UI_GREEN, 0.7f));
        ui_glow(x + 24, y + 23, 14, ui_alpha(UI_GREEN, 0.8f));
        ui_text(x + 40, y + 10, g_notice, sz, UI_FONT_REGULAR, UI_WHITE, UI_ALIGN_LEFT);
        ui_overlay_submit(false);
        g_ov_up = true;
        return;
    }
    if (g_notice[0] && g_notice_t0 > 0) {
        g_notice[0] = 0;
    }
    if (g_ov_up) {
        ui_overlay_off();
        g_ov_up = false;
    }
}

/* What applies at once of the settings (the picture's shape and filters,
 * the volume): the game's over the global ones. */
static void apply_cfg(const Config *global, const Config *game, bool sound)
{
    const Config *pick[2] = {game, global};
    for (int k = 1; k >= 0; k--) {
        const Config *c = pick[k];
        const char *v;
        if ((v = cfg_get(c, "aspect"))) {
            nc_present_set_aspect(!strcmp(v, "4:3") ? NC_ASPECT_4_3 : !strcmp(v, "16:9") ? NC_ASPECT_16_9
                                  : !strcmp(v, "stretch") ? NC_ASPECT_STRETCH : NC_ASPECT_AUTO);
        }
        if ((v = cfg_get(c, "overscan"))) {
            nc_present_set_overscan(atoi(v));
        }
        if ((v = cfg_get(c, "fxaa"))) {
            nc_present_set_fxaa(atoi(v) != 0);
        }
        if ((v = cfg_get(c, "smaa"))) {
            nc_present_set_smaa(atoi(v) != 0);
        }
        if ((v = cfg_get(c, "interp"))) {
            nc_present_set_interp(atoi(v) != 0);
        }
        if ((v = cfg_get(c, "integer"))) {
            nc_present_set_integer(atoi(v) != 0);
        }
        if ((v = cfg_get(c, "upscale"))) {
            nc_present_set_upscale(nc_upscale_from_name(v));
        }
        if ((v = cfg_get(c, "nis.sharp"))) {
            nc_present_set_nis_sharpness(atoi(v));
        }
        if ((v = cfg_get(c, "crt"))) {
            hle_present_set_crt(atoi(v));
        }
        if ((v = cfg_get(c, "vsync"))) {
            hle_present_set_vsync(atoi(v) != 0);
        }
        if ((v = cfg_get(c, "showfps"))) {
            nc_present_set_show_fps(atoi(v) != 0);
        }
        if (!sound) {
            continue;
        }
        if ((v = cfg_get(c, "volume"))) {
            g_volume = atoi(v) / 100.0f;
            nc_audio_set_volume(g_volume);
        }
        if ((v = cfg_get(c, "mutebg"))) {
            g_mute_bg = atoi(v) != 0;
        }
        if ((v = cfg_get(c, "pausebg"))) {
            g_pause_bg = atoi(v) != 0;
        }
        if ((v = cfg_get(c, "autosave"))) {
            g_autosave = atoi(v) != 0;
        }
    }
    g_fx = cfg_bool(global, "ui.fx", true);
}

/* the menu's settings as they are being changed: the picture under it */
static void live_cfg(const struct Config *global, const struct Config *game)
{
    apply_cfg(global, game, false);
}

static void quit_to_dash(SDL_Window *win)
{
    /* straight out: nothing of the run is taken back first (the
     * presenter's swapchain, the paused threads), so nothing can hold the
     * window */
    nc_log("hakuX-app", "quit to the dashboard");
    give_foreground(true);
    SDL_HideWindow(win);
    exit_now(0);
}

/* Leaving a game as xemu's "resume where you left off" does: the state saved to slot 0
 * (it runs on its own thread, the loop keeps presenting) and the exit follows when it
 * is done. False when there is nothing to save (the game just started, the core not
 * running, the setting off): the caller leaves at once. */
static bool start_quit_save(bool close)
{
    if (!g_autosave || g_is_dash || g_quit_pending || SDL_GetTicks() < 8000 ||
        SDL_GetAtomicInt(&g_core_state) != 1 || !hle_pb_active()) {
        return false;
    }
    nc_log("hakuX-app", "leaving: saving the state (slot 0) first");
    g_quit_pending = true;
    g_quit_close = close;
    g_quit_deadline = SDL_GetTicks() + 25000;
    save_state(0);
    if (!SDL_GetAtomicInt(&g_saving)) {
        g_quit_pending = false;     /* it didn't start */
        return false;
    }
    return true;
}

/* The in-game menu: the emulation paused, the menu drawn by the game's own
 * presenter over its picture, blurred (the overlay). Without the overlay:
 * the presenter off the window (its Vulkan surface dropped) while the
 * dashboard's renderer draws the menu over the last frame; then the
 * presenter takes the window back. */
static void ingame_menu(SDL_Window *win, bool *paused, bool minimized)
{
    if (SDL_GetAtomicInt(&g_core_state) != 1) {
        return;
    }
    mouse_release(win);
    nc_kernel_set_paused(&g_core.kernel, true);
    nc_audio_set_paused(true);
    int slot = 0;
    const char *title = strrchr(g_disc, '\\');
    if (ov_ok()) {
        ui_overlay_off();
        g_ov_up = false;
        g_notice[0] = 0;
        pointer_menu();
    const int act = dash_ingame(win, g_key, title ? title + 1 : g_disc, NULL, &slot, live_cfg);
        SDL_HideCursor();
        nc_auto_resync();
        if (act == DASH_INGAME_QUIT && !start_quit_save(false)) {
            fx_out();
            quit_to_dash(win);
        }
        if (act == DASH_INGAME_RESTART) {
            fx_out();
            run_again(NULL, false);
        }
        ui_overlay_off();
        g_ov_up = false;
        nc_kernel_set_paused(&g_core.kernel, *paused || minimized);
        nc_audio_set_paused(*paused || minimized);
        char path[MAX_PATH + 64];
        snprintf(path, sizeof(path), "%sxbased.ini", g_data_dir);
        Config global, game;
        cfg_load(&global, path);
        snprintf(path, sizeof(path), "%sgames/%s.ini", g_data_dir, g_key);
        cfg_load(&game, path);
        im_resolve(&g_map, &global, &game);
        pads_changed();
        apply_cfg(&global, &game, true);
        if (act == DASH_INGAME_SAVE) {
            save_state(slot);
        } else if (act == DASH_INGAME_LOAD) {
            load_state(slot);
        }
        return;
    }
    char pic[MAX_PATH + 32];
    snprintf(pic, sizeof(pic), "%smenu_frame.bmp", g_data_dir);
    remove(pic);
    hle_present_shot(pic);
    for (int i = 0; i < 30 && GetFileAttributesA(pic) == INVALID_FILE_ATTRIBUTES; i++) {
        nc_present_frame(&g_nw);
        SDL_Delay(5);
    }
    nc_present_frame(NULL);     /* the surface goes */
    const int act = dash_ingame(win, g_key, title ? title + 1 : g_disc, pic, &slot, NULL);
    remove(pic);
    if (act == DASH_INGAME_QUIT && !start_quit_save(false)) {
        quit_to_dash(win);
    }
    pointer_hide();     /* the game doesn't use the mouse; the menu may have shown it */
    nc_auto_resync();
    /* the presenter back on the window before anything is shown or saved */
    SDL_Delay(50);
    nc_present_frame(&g_nw);
    nc_kernel_set_paused(&g_core.kernel, *paused || minimized);
    nc_audio_set_paused(*paused || minimized);
    char path[MAX_PATH + 64];
    snprintf(path, sizeof(path), "%sxbased.ini", g_data_dir);
    Config global, game;
    cfg_load(&global, path);
    snprintf(path, sizeof(path), "%sgames/%s.ini", g_data_dir, g_key);
    cfg_load(&game, path);
    im_resolve(&g_map, &global, &game);
    pads_changed();
    apply_cfg(&global, &game, true);
    switch (act) {
    case DASH_INGAME_SAVE:
        save_state(slot);
        break;
    case DASH_INGAME_LOAD:
        load_state(slot);
        break;
    case DASH_INGAME_RESTART:
        run_again(NULL, false);
        break;
    default:
        break;
    }
}

/* debug.hakux.state ("save:<slot>", "load:<slot>"), as the app's menu would */
/* ---- stalled games (ui/autofix.c) ----
 * A game that has stopped for AF_STALL_S seconds (no frame, the monitor's hang) is run again with the next
 * fix that fits what the threads and the render thread are doing; the run that gets 900 frames past the
 * stall, and doesn't stall, keeps its fix in the game's settings file. A notice counts down first: the
 * menu, a pause or the window going away cancels it for the run. */
static AfRun g_af;
static bool g_af_on = true;
static struct {
    bool active;
    Uint64 until_ms, said_ms;
    char arg[240];
    char label[80];
} g_af_pend;

static void af_files(char *game, size_t gn, char *learned, size_t ln)
{
    snprintf(game, gn, "%sgames/%s.ini", g_data_dir, g_key);
    snprintf(learned, ln, "%scompat_learned.ini", g_data_dir);
}

static void af_countdown_notice(Uint64 now_ms)
{
    char t[260];
    const int left = (int)((g_af_pend.until_ms > now_ms ? g_af_pend.until_ms - now_ms : 0) + 999) / 1000;
    snprintf(t, sizeof(t), T(S_AF_TRY), g_af_pend.label, left);
    notice(t, NULL);
    g_af_pend.said_ms = now_ms;
}

static void autofix_tick(bool away)
{
    if (!g_af.enabled) {
        return;
    }
    const Uint64 now_ms = SDL_GetTicks();
    if (g_af_pend.active) {
        if (away) {
            g_af_pend.active = false;
            g_af.enabled = false;
            nc_log("hakuX-app", "autofix: cancelled (the menu, a pause or the window went away)");
            notice(T(S_AF_CANCELLED), NULL);
        } else if (now_ms >= g_af_pend.until_ms) {
            g_af_pend.active = false;
            run_again(g_af_pend.arg, false);
        } else if (now_ms - g_af_pend.said_ms >= 1000) {
            af_countdown_notice(now_ms);
        }
        return;
    }
    const double now = (double)now_ms / 1000.0;
    const double hang = away ? 0.0 : hle_hitch_live_hang_s();
    const uint64_t frames = hle_hitch_frames();
    if (away || !af_due(&g_af, now, hang, frames)) {
        return;
    }
    NcStallFacts f;
    nc_kernel_stall_facts(&g_core.kernel, &f);
    const int act = hle_hitch_render_act();
    const bool gpu = act == HLE_ACT_GPU || act == HLE_ACT_DISPLAY || act == HLE_ACT_FLIP || act == HLE_ACT_TRAP ||
                     act == HLE_ACT_ARENA || act == HLE_ACT_RING;
    char gp[MAX_PATH + 64], lp[MAX_PATH + 64];
    af_files(gp, sizeof(gp), lp, sizeof(lp));
    Config *game = malloc(sizeof(*game)), *learned = malloc(sizeof(*learned));
    if (!game || !learned) {
        free(game);
        free(learned);
        return;
    }
    cfg_load(game, gp);
    cfg_load(learned, lp);
    const uint32_t pub = g_title_id >> 16;
    const AfPoll p = af_poll(&g_af, now, hang, frames, f.running + f.ready, gpu, false, pub, game, learned,
                             nc_kernel_fixups_applied());
    if (p.act == AF_PASS) {
        af_record_pass(game, learned, p.sym, pub, p.fix->id);
        cfg_save(game);
        cfg_save(learned);
        nc_log("hakuX-app", "autofix: \"%s\" got the game %d frames past the stall: kept in %s", p.fix->id,
               AF_PASS_FRAMES, gp);
        char t[260];
        snprintf(t, sizeof(t), T(S_AF_KEPT), p.fix->label);
        notice(t, NULL);
    } else if (p.act == AF_TRY || p.act == AF_GIVEUP) {
        nc_log("hakuX-app",
               "autofix: stalled (%s) at frame %llu after %.0f s: threads %u (%u running, %u ready, %u waiting, %u suspended), "
               "render %d",
               af_sym_name(p.sym), (unsigned long long)p.stall_frame, hang, f.threads, f.running, f.ready, f.waiting,
               f.suspended, act);
        if (p.failed) {
            af_record_fail(game, learned, g_af.trial_sym, pub, p.failed->id);
            cfg_save(game);
            cfg_save(learned);
            nc_log("hakuX-app", "autofix: \"%s\" didn't help", p.failed->id);
        }
        if (p.act == AF_GIVEUP) {
            nc_log("hakuX-app", "autofix: no fix left to try for this game (%d tried)", g_af.trials);
            notice(T(S_AF_GIVEUP), NULL);
        } else {
            char v[200];
            af_run_trial_value(&g_af, p.fix->id, p.stall_frame, p.sym, v, sizeof(v));
            snprintf(g_af_pend.arg, sizeof(g_af_pend.arg), "fixtrial=%s", v);
            snprintf(g_af_pend.label, sizeof(g_af_pend.label), "%s", p.fix->label);
            g_af_pend.until_ms = now_ms + 10000;
            g_af_pend.active = true;
            nc_log("hakuX-app", "autofix: trying \"%s\" (%s) in 10 s", p.fix->id, p.fix->what);
            af_countdown_notice(now_ms);
        }
    }
    free(game);
    free(learned);
}

static void state_request_poll(void)
{
    static char last[PROP_VALUE_MAX];
    static bool primed;
    static unsigned n;
    if (n++ % 30) {
        return;
    }
    char v[PROP_VALUE_MAX] = "";
    __system_property_get("debug.hakux.state", v);
    if (!primed || !strcmp(v, last)) {
        primed = true;
        snprintf(last, sizeof(last), "%s", v);
        return;
    }
    snprintf(last, sizeof(last), "%s", v);
    if (!strncmp(v, "save:", 5)) {
        save_state(atoi(v + 5));
    } else if (!strncmp(v, "load:", 5)) {
        load_state(atoi(v + 5));
    }
}

/* ------------------------------------------------------------- main */

/* xbased.log beside the executable: a run the process started again (a
 * title's relaunch, a state loaded) goes on in the same log. */
static void log_setup(bool console, bool again)
{
    char path[MAX_PATH + 16];
    snprintf(path, sizeof(path), "%sxbased.log", g_data_dir);
#ifdef _WIN32
    if (console && AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    } else {
        freopen(path, again ? "a" : "w", stderr);
    }
#else
    (void)console;
    freopen(path, again ? "a" : "w", stderr);
#endif
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* The file dialog's answer (SDL calls back on a thread of its own). */
static SDL_AtomicInt g_dialog_done;
static char g_dialog_path[1024];

static void SDLCALL dialog_cb(void *ud, const char *const *list, int filter)
{
    if (list && list[0]) {
        snprintf(g_dialog_path, sizeof(g_dialog_path), "%s", list[0]);
    }
    SDL_SetAtomicInt(&g_dialog_done, 1);
}

static const char *pick_disc(void)
{
    static const SDL_DialogFileFilter filters[] = {
        {"Xbox disc images (*.iso, *.xiso)", "iso;xiso"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(dialog_cb, NULL, NULL, filters, 2, NULL, false);
    while (!SDL_GetAtomicInt(&g_dialog_done)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        SDL_Delay(20);
    }
    return g_dialog_path[0] ? g_dialog_path : NULL;
}

#ifndef XBASED_VERSION
#define XBASED_VERSION "1.0.76.22"
#endif

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        remove(path);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        char p[1400];
        snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        struct stat st;
        if (!stat(p, &st) && S_ISDIR(st.st_mode)) {
            rm_tree(p);
        } else {
            remove(p);
        }
    }
    closedir(d);
    rmdir(path);
}

/* The shader caches (hle_spirv*, hle_pipeline_cache, hle_pipelines) from
 * another version go: each update starts them again, so nothing a build
 * before kept is used (they are keyed by their content, and old entries
 * only took space). The version they were made with: hle/version.txt. */
static void shader_cache_by_version(const char *base)
{
    if (!base) {
        return;
    }
    char vf[1200], seen[64] = "";
    snprintf(vf, sizeof(vf), "%s/version.txt", base);
    FILE *f = fopen(vf, "r");
    if (f) {
        if (fgets(seen, sizeof(seen), f)) {
            seen[strcspn(seen, "\r\n")] = 0;
        }
        fclose(f);
    }
    if (!strcmp(seen, XBASED_VERSION)) {
        return;
    }
    DIR *d = opendir(base);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strncmp(e->d_name, "hle_spirv", 9) || !strcmp(e->d_name, "hle_pipeline_cache") ||
                !strcmp(e->d_name, "hle_pipelines")) {
                char p[1300];
                snprintf(p, sizeof(p), "%s/%s", base, e->d_name);
                rm_tree(p);
            }
        }
        closedir(d);
    }
    f = fopen(vf, "w");
    if (f) {
        fprintf(f, "%s\n", XBASED_VERSION);
        fclose(f);
    }
    nc_log("hakuX-app", "shader caches of %s cleared (%s)", seen[0] ? seen : "an earlier version",
           XBASED_VERSION);
}
#ifdef _WIN32
#define NC_PLATFORM_NAME "Windows"
#else
#define NC_PLATFORM_NAME "Linux"
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    xb_aslr_check();    /* Windows' first memory where the guest's goes: run again without (win_aslr.h) */
#endif
    /* The gamepads with the window in the background (the dashboard behind a
     * launcher, a game after a click elsewhere): as the environment's, which
     * SDL takes where the hint set at run time didn't (a virtual gamepad's
     * moves behind another window were dropped with it), inherited by the
     * game's process. */
    if (!getenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS")) {
#ifdef _WIN32
        _putenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1");
#else
        setenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1", 0);
#endif
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1", false);
    }
    exe_dir();
    read_ini();
    g_ncfg_args = g_nargs;
    const char *disc = NULL;
    for (int i = 1; i < argc; i++) {
        if (!disc && !strchr(argv[i], '=') && strcmp(argv[i], "uncap") && strcmp(argv[i], "relaunch") &&
            strcmp(argv[i], "fullscreen") && strcmp(argv[i], "console") && strcmp(argv[i], "dash")) {
            disc = argv[i];
        } else {
            add_arg(argv[i]);
            if (g_ncmdline < MAX_ARGS && argv[i][0]) {
                g_cmdline[g_ncmdline++] = strdup(argv[i]);
            }
        }
    }
    bool again = opt_flag("relaunch");
    for (int i = 0; i < g_nargs; i++) {
        again |= !strncmp(g_args[i], "loadstate=", 10);
    }
    for (int i = 0; i < g_nargs; i++) {
        /* the first run of an autofix chain (fixtrial=<id>@<frame>#1#<sym>): the log of the run that stalled
         * is kept apart, or the new run's would take its place and the evidence of the stall with it */
        const char *h = !strncmp(g_args[i], "fixtrial=", 9) ? strchr(g_args[i], '#') : NULL;
        if (h && atoi(h + 1) == 1) {
            char from[MAX_PATH + 16], to[MAX_PATH + 32];
            snprintf(from, sizeof(from), "%sxbased.log", g_data_dir);
            snprintf(to, sizeof(to), "%sxbased-before-autofix.log", g_data_dir);
            remove(to);
            rename(from, to);
        }
    }
    log_setup(opt_flag("console"), again);
    if (again) {
        nc_log("hakuX-app", "---- started again (%s)", opt_flag("relaunch") ? "the title's relaunch" : "a state loaded");
    }
    nc_log("hakuX-app", "xbased " XBASED_VERSION " (Modern GUI fork of xbased, an original Xbox emulator by crazyriddler, based on hakuX), " NC_PLATFORM_NAME);
    nc_win_mods_cleanup(g_exe_dir);     /* what an update took away (win_mods.h) */

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifndef _WIN32
    /* PulseAudio's API first: PipeWire desktops serve it too, and where
     * PipeWire's own is missing (WSLg) SDL waits half a minute on it */
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "pulseaudio,pipewire,alsa");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fatal_box("xbased", SDL_GetError());
        return 1;
    }
    {
        char db[MAX_PATH + 32];
        snprintf(db, sizeof(db), "%sgamecontrollerdb.txt", g_exe_dir);
        const int n = SDL_AddGamepadMappingsFromFile(db);
        if (n > 0) {
            nc_log("hakuX-app", "%d gamepad mappings from %s", n, db);
        }
    }
    /* "dash" with no disc: the Xbox dashboard from the hard disk's C: drive,
     * the tray empty (its Xbox Live account pages, its settings) */
    const bool xbox_dash = opt_flag("dash") && (!disc || !strcmp(disc, "none"));
    if (!xbox_dash && (!disc || !strcmp(disc, "none"))) {
        if (opt_flag("pick")) {
            disc = pick_disc();
            if (!disc) {
                return 0;
            }
        } else {
            /* the dashboard: the library and settings; each game in a process of its own */
            SDL_Window *dw = SDL_CreateWindow("xbased", 1280, 720,
                                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                  (want_fullscreen() ? SDL_WINDOW_FULLSCREEN : 0));
            if (!dw) {
                fatal_box("xbased", SDL_GetError());
                return 1;
            }
            char exe[MAX_PATH];
            GetModuleFileNameA(NULL, exe, sizeof(exe));
            const int r = dash_run(dw, exe);
            SDL_DestroyWindow(dw);
            SDL_Quit();
            return r;
        }
    }
    g_is_dash = xbox_dash;
    if (xbox_dash) {
        /* the same options, controller map, language and video flags as a game's: the
         * dashboard took no input until the in-game menu had been opened and closed */
        game_options(NULL);
    } else {
        snprintf(g_disc, sizeof(g_disc), "%s", disc);
        game_options(g_disc);
        g_disc_fd = open(g_disc, O_RDONLY | O_BINARY);
        if (g_disc_fd < 0) {
            nc_log("hakuX-app", "can't open the disc %s", g_disc);
        }
    }
    char hdd[1024];
    const char *hv = opt_value("hdd");
    if (hv) {
        snprintf(hdd, sizeof(hdd), "%s", hv);
    } else {
        snprintf(hdd, sizeof(hdd), "%shdd", g_data_dir);
    }
    make_hdd(hdd);
    if (g_title_id) {
        /* downloaded content and title updates copied in by hand: signed for this console */
        const int resigned = xi_resign_downloads(hdd, g_title_id);
        if (resigned) {
            nc_log("hakuX-app", "%d downloaded content file(s) of %08x re-signed for this console", resigned, g_title_id);
        }
    }
    nc_io_set_hdd_root(hdd);
    setenv("HAKUX_HLE_MODE", "replace", 0);
    {
        char base[1100];
        snprintf(base, sizeof(base), "%s/../hle", hdd);
        mkdir(base, 0775);
        setenv("HAKUX_NATIVE_BASE", base, 0);
        shader_cache_by_version(getenv("HAKUX_NATIVE_BASE"));
    }

    NcOptions opt;
    nc_options_init(&opt);
    af_run_init(&g_af, false, 0);
    for (int i = 0; i < g_nargs; i++) {
        const char *a = g_args[i];
        char resolved[2400];
        if (!strncmp(a, "xcheat=", 7) && a[7] && a[7] != '/' && a[7] != '\\' && a[8] != ':') {
            /* a cheat file under cheats/ (the shipped ones beside the
             * executable, the user's in the data folder) */
            char p[1300];
            snprintf(p, sizeof(p), "%s", a + 7);
            char *bar = strrchr(p, '|');
            const char *states = bar ? bar + 1 : "";
            if (bar) {
                *bar = 0;
            }
            char full[1400];
            snprintf(full, sizeof(full), "%s%s", g_data_dir, p);
            if (access(full, 0) != 0) {
                snprintf(full, sizeof(full), "%s%s", g_exe_dir, p);
            }
            snprintf(resolved, sizeof(resolved), "xcheat=%s|%s", full, states);
            a = resolved;
        }
        if (nc_option(&opt, a)) {
            continue; /* the core's */
        }
        if (!strncmp(a, "aspect=", 7)) {
            const char *v = a + 7;
            nc_present_set_aspect(!strcmp(v, "4:3")       ? NC_ASPECT_4_3
                                  : !strcmp(v, "16:9")    ? NC_ASPECT_16_9
                                  : !strcmp(v, "stretch") ? NC_ASPECT_STRETCH
                                                          : NC_ASPECT_AUTO);
        } else if (!strncmp(a, "overscan=", 9)) {
            nc_present_set_overscan(atoi(a + 9));
        } else if (!strncmp(a, "fxaa=", 5)) {
            nc_present_set_fxaa(atoi(a + 5) != 0);
        } else if (!strncmp(a, "smaa=", 5)) {
            nc_present_set_smaa(atoi(a + 5) != 0);
        } else if (!strncmp(a, "interp=", 7)) {
            nc_present_set_interp(atoi(a + 7) != 0);
        } else if (!strncmp(a, "integer=", 8)) {
            nc_present_set_integer(atoi(a + 8) != 0);
        } else if (!strncmp(a, "upscale=", 8)) {
            nc_present_set_upscale(nc_upscale_from_name(a + 8));
        } else if (!strncmp(a, "nis.sharp=", 10)) {
            nc_present_set_nis_sharpness(atoi(a + 10));
        } else if (!strncmp(a, "crt=", 4)) {
            hle_present_set_crt(atoi(a + 4));
        } else if (!strncmp(a, "vsync=", 6)) {
            hle_present_set_vsync(atoi(a + 6) != 0);
        } else if (!strncmp(a, "volume=", 7)) {
            g_volume = atoi(a + 7) / 100.0f;
            nc_audio_set_volume(g_volume);
        } else if (!strncmp(a, "showfps=", 8)) {
            nc_present_set_show_fps(atoi(a + 8) != 0);
        } else if (!strncmp(a, "mutebg=", 7)) {
            g_mute_bg = atoi(a + 7) != 0;
        } else if (!strncmp(a, "pausebg=", 8)) {
            g_pause_bg = atoi(a + 8) != 0;
        } else if (!strncmp(a, "autosave=", 9)) {
            g_autosave = atoi(a + 9) != 0;
        } else if (!strncmp(a, "sbc=", 4)) {
            g_sbc_opt = atoi(a + 4) != 0;
        } else if (!strncmp(a, "autofix=", 8)) {
            g_af_on = atoi(a + 8) != 0;
        } else if (!strncmp(a, "fix=", 4)) {
            /* a fix the game's file keeps (autofix.c) */
            nc_log("hakuX-app", "autofix: %s \"%s\"", af_apply(a + 4, &opt) ? "applied the kept fix" : "unknown fix", a + 4);
        } else if (!strncmp(a, "fixtrial=", 9)) {
            /* the fix this run is a trial of: kept if the game gets past where it stalled */
            if (af_run_set_trial(&g_af, a + 9)) {
                af_apply(g_af.trial_id, &opt);
                nc_log("hakuX-app", "autofix: trial %d: \"%s\" (for \"%s\", the stall at frame %llu)", g_af.trials,
                       g_af.trial_id, af_sym_name(g_af.trial_sym), (unsigned long long)g_af.trial_frame);
            } else {
                nc_log("hakuX-app", "autofix: unreadable trial %s", a + 9);
            }
        }
    }
    g_sbc_on = g_sbc_opt > 0 || (g_sbc_opt < 0 && g_title_id == SBC_TITLE_STEEL_BATTALION && !xbox_dash);
    nc_dev_ohci_use_sbc(g_sbc_on);
    if (g_sbc_on) {
        nc_log("hakuX-app", "the Steel Battalion controller is player 1's: %s", sbc_layout_text());
    }
    g_af.enabled = g_af_on && !xbox_dash && g_key[0] && g_disc_fd >= 0;
    g_af.started_s = (double)SDL_GetTicks() / 1000.0;
    nc_win_mods_prepare(g_exe_dir, g_disc_fd, g_args, g_nargs);
    nc_options_apply(&opt);
    g_uncap = opt.uncap;
    g_fps_cap = opt.fps_cap;
    nc_present_set_widescreen(opt.video >= 0 && (opt.video & NC_VIDEO_WIDESCREEN));
    nc_log("hakuX-app", "native core: disc %s, hdd %s%s", g_disc, hdd, opt.uncap ? ", frame rate uncapped" : "");
    if (!xbox_dash && g_disc_fd >= 0) {
        /* the lead before a run: kernel functions the title imports that this build doesn't have */
        char names[600];
        const int miss = cs_missing_for_disc(g_disc, names, sizeof(names));
        if (miss > 0) {
            nc_log("hakuX-app", "known gaps: the title imports %d kernel function(s) this build lacks (%s); a call to one stops it, "
                   "and no setting helps", miss, names);
        }
    }

    const char *title = strrchr(g_disc, '\\');
    const char *title2 = strrchr(g_disc, '/');
    title = title2 > title ? title2 : title;
    char wtitle[512];
    snprintf(wtitle, sizeof(wtitle), "xbased - %s", xbox_dash ? "Xbox Dashboard" : title ? title + 1 : g_disc);
#ifdef _WIN32
    const SDL_WindowFlags vk_flag = 0;            /* the presenter makes its own Win32 surface */
#else
    const SDL_WindowFlags vk_flag = SDL_WINDOW_VULKAN; /* SDL_Vulkan_CreateSurface's (swapchain.c) */
#endif
    SDL_Window *win = SDL_CreateWindow(wtitle, 1280, 960, vk_flag | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                              (want_fullscreen() ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) {
        fatal_box("xbased", SDL_GetError());
        return 1;
    }
    pointer_hide();     /* the game doesn't use the mouse */
    nc_log("hakuX-app", "pointer: hidden over the game");
    refresh_note(win);
    {
        /* the run before's window's place (run_again), windowed */
        const char *r = getenv("XBASED_RECT");
        int x, y, w, h;
        if (r && sscanf(r, "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0 &&
            !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)) {
            SDL_SetWindowSize(win, w, h);
            SDL_SetWindowPosition(win, x, y);
        }
    }
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    g_nw.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    g_nw.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, NULL);
    g_nw.sdl_window = win;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    g_nw.width = pw;
    g_nw.height = ph;

    {
        /* Settings > Video > Graphics card: the Nth Vulkan device (vkdev.c's HAKUX_GPU) */
        char ip[MAX_PATH + 32];
        snprintf(ip, sizeof(ip), "%sxbased.ini", g_data_dir);
        Config gc;
        cfg_load(&gc, ip);
        const char *gv = cfg_get(&gc, "gpu");
        if (gv && atoi(gv) > 0 && !getenv("HAKUX_GPU")) {
            char b[16];
            snprintf(b, sizeof(b), "%d", atoi(gv) - 1);
#ifdef _WIN32
            _putenv_s("HAKUX_GPU", b);
#else
            setenv("HAKUX_GPU", b, 0);
#endif
        }
        /* Settings > System > disk sizes: what the title is told its drives hold (kernel/io.c) */
        static const struct { const char *key, *env; } disks[] = {{"hdd.mb", "HAKUX_E_MB"}, {"cache.mb", "HAKUX_CACHE_MB"}};
        for (int i = 0; i < 2; i++) {
            const char *dv = cfg_get(&gc, disks[i].key);
            if (dv && atoi(dv) > 0 && !getenv(disks[i].env)) {
#ifdef _WIN32
                _putenv_s(disks[i].env, dv);
#else
                setenv(disks[i].env, dv, 0);
#endif
            }
        }
    }
    if (!nc_present_init()) {
        return 1;
    }
    {
        /* the interface as the game's overlay: the in-game menu, the
         * transitions, the notices (its fonts beside the executable) */
        char fonts[MAX_PATH + 16];
        snprintf(fonts, sizeof(fonts), "%sfonts/", g_exe_dir);
        if (!ui_init_overlay(fonts, ov_present, ov_size)) {
            nc_log("hakuX-app", "no overlay: the in-game menu on a renderer of its own");
        }
        char path[MAX_PATH + 16];
        snprintf(path, sizeof(path), "%sxbased.ini", g_data_dir);
        Config global;
        cfg_load(&global, path);
        g_fx = cfg_bool(&global, "ui.fx", true);
        const char *hp = getenv("XBASED_HOLD");
        if (hp && hp[0] && ui_is_overlay()) {
            SDL_Surface *sf = SDL_LoadBMP(hp);
            if (sf) {
                g_hold = ui_texture_surface(sf);
                SDL_DestroySurface(sf);
                snprintf(g_hold_path, sizeof(g_hold_path), "%s", hp);
            }
        }
#ifdef _WIN32
        const char *rv = getenv("XBASED_READY");
        if (rv && rv[0]) {
            g_ready_ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, rv);
        }
        SetEnvironmentVariableA("XBASED_READY", NULL);
        SetEnvironmentVariableA("XBASED_HOLD", NULL);
        SetEnvironmentVariableA("XBASED_RECT", NULL);
#else
        unsetenv("XBASED_HOLD");
#endif
    }
    nc_vsync_start();
    nc_auto_init(g_data_dir, save_state, load_state);
    nc_core_fault_install(&g_core);
    pads_changed();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t core;
    pthread_create(&core, &attr, core_thread, NULL);
    pthread_attr_destroy(&attr);

    nc_win_thread_name("window");
    bool quit = false, paused = false, minimized = false, focused = true, told = false, bg_paused = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                if (!g_quit_pending && start_quit_save(true)) {
                    break;      /* leaves when the save is done */
                }
                quit = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                /* a click on the game takes the mouse, when it is set to be a stick */
                if (g_map.mouse && !g_mouse_cap && focused && !minimized) {
                    mouse_capture(win);
                } else if (!g_map.mouse) {
                    static bool said;
                    if (!said) {
                        said = true;
                        nc_log("hakuX-app", "a mouse click: the mouse is off (Settings > Controls > Mouse as a stick)");
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (!g_mouse_cap) {
                    pointer_hide();
                }
                if (g_mouse_cap) {
                    g_mouse_dx += ev.motion.xrel;
                    g_mouse_dy += ev.motion.yrel;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (g_mouse_cap && ev.wheel.y != 0) {
                    g_wheel_until[ev.wheel.y > 0 ? 0 : 1] = SDL_GetTicks() + 120;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.repeat) {
                    break;
                }
                if (g_mouse_cap && (ev.key.key == SDLK_F3 || ev.key.key == SDLK_ESCAPE)) {
                    mouse_release(win);     /* F3 or Esc lets go */
                    break;
                }
                /* the shortcuts (Settings > Shortcuts); Shift with quick save/load: slot 2 */
                if (ev.key.key == g_map.hotkey[IM_HK_FULL] ||
                    (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                } else if (ev.key.key == g_map.hotkey[IM_HK_PAUSE]) {
                    paused = !paused;
                    nc_kernel_set_paused(&g_core.kernel, paused || minimized);
                    nc_audio_set_paused(paused || minimized);
                    nc_log("hakuX-app", "%s", paused ? "paused" : "resumed");
                } else if (ev.key.key == g_map.hotkey[IM_HK_SAVE]) {
                    save_state(ev.key.mod & SDL_KMOD_SHIFT ? 2 : 1);
                } else if (ev.key.key == g_map.hotkey[IM_HK_LOAD]) {
                    load_state(ev.key.mod & SDL_KMOD_SHIFT ? 2 : 1);
                } else if (ev.key.key == g_map.hotkey[IM_HK_TURBO]) {
                    nc_win_prop_set("debug.hakux.uncap", "1");  /* frames as fast as they come, while held */
                    nc_win_prop_set("debug.hakux.fpscap", "0");
                } else if (ev.key.key == g_map.hotkey[IM_HK_SHOT]) {
                    screenshot();
                } else if (ev.key.key == g_map.hotkey[IM_HK_MENU]) {
                    ingame_menu(win, &paused, minimized);
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (ev.key.key == g_map.hotkey[IM_HK_TURBO]) {
                    nc_win_prop_set("debug.hakux.uncap", g_uncap ? "1" : "0");
                    char cap[16];
                    snprintf(cap, sizeof(cap), "%d", g_fps_cap);
                    nc_win_prop_set("debug.hakux.fpscap", cap);
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                /* the menu: the Guide button, or Back with Start */
                SDL_Gamepad *gp = SDL_GetGamepadFromID(ev.gbutton.which);
                if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE ||
                    (gp && SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_BACK) &&
                     SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_START))) {
                    ingame_menu(win, &paused, minimized);
                }
                break;
            }
            case SDL_EVENT_JOYSTICK_ADDED:
                joystick_added(ev.jdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                pad_added(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                pad_removed(ev.gdevice.which);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                g_nw.width = ev.window.data1;
                g_nw.height = ev.window.data2;
                refresh_note(win);
                break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                refresh_note(win);
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
                minimized = ev.type == SDL_EVENT_WINDOW_MINIMIZED;
                nc_kernel_set_paused(&g_core.kernel, paused || minimized);
                nc_audio_set_paused(paused || minimized);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                focused = true;
                pointer_hide();
                nc_audio_set_volume(g_volume);
                if (bg_paused) {
                    bg_paused = false;
                    minimized = false;
                    nc_kernel_set_paused(&g_core.kernel, paused);
                    nc_audio_set_paused(paused);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                focused = false;
                mouse_release(win);
                if (g_mute_bg) {
                    nc_audio_set_volume(0);
                }
                if (g_pause_bg && !bg_paused) {
                    /* held like a minimized window (Settings > Video) */
                    bg_paused = true;
                    minimized = true;
                    nc_kernel_set_paused(&g_core.kernel, true);
                    nc_audio_set_paused(true);
                }
                break;
            default:
                break;
            }
        }
        nc_auto_tick();
        if (nc_auto_take_menu()) {
            ingame_menu(win, &paused, minimized);
        }
        if (SDL_CursorVisible()) {
            static bool said;
            if (!said) {
                said = true;
                nc_log("hakuX-app", "pointer: it showed again over the game and was hidden");
            }
            pointer_hide();     /* the game doesn't draw one: a message box, a focus change or a mouse move shows it again */
        }
        {
            /* Windows only changes the pointer when the mouse moves, and SDL only acts on a window it has
             * seen the mouse in: with a controller, or a mouse left alone, the one the dashboard showed can
             * stay on the game. So it is set again a few times a second. */
            static Uint64 last_hide;
            const Uint64 now_ms = SDL_GetTicks();
            if (now_ms - last_hide >= 150) {
                last_hide = now_ms;
                pointer_hide();
#ifdef _WIN32
                if (focused) {
                    SetCursor(NULL);
                }
#endif
            }
        }
        update_pads(focused);
        state_request_poll();
        autofix_tick(paused || minimized || bg_paused);

        /* the GPU can't draw the game (no Vulkan device, a driver lacking
         * what it needs): the game ran on with its sound over a black
         * window, the reason only in the log */
        if (!told && nc_hle_fatal_kind() == NC_FATAL_DRIVER) {
            told = true;
            fatal_box("xbased", nc_hle_fatal());
            break;
        }
        const int state = SDL_GetAtomicInt(&g_core_state);
        if (state < 0) {
            if (g_core.kernel.relaunch) {
                run_again("relaunch", true);
            }
            if (g_core.kernel.quit) {
                if (g_core.kernel.quit_no_dash) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "xbased", T(S_NO_DASHBOARD), win);
                } else {
                    fx_out();
                }
                break;
            }
            if (!told) {
                told = true;
                const char *why = nc_hle_fatal();
                if (why) {
                    fatal_box("xbased", why);
                    break;
                }
            }
        }
        if (g_quit_pending && (!SDL_GetAtomicInt(&g_saving) || SDL_GetTicks() > g_quit_deadline)) {
            g_quit_pending = false;
            if (g_quit_close) {
                break;
            }
            quit_to_dash(win);
        }
        overlay_tick();
        if (minimized || g_nw.width <= 0 || !nc_present_frame(&g_nw)) {
            SDL_Delay(8); /* nothing to wait for the display on */
        }
#ifdef _WIN32
        if (g_ready_ev && hle_present_count() > 0) {
            SetEvent(g_ready_ev);   /* drawn: the run before may go */
            CloseHandle(g_ready_ev);
            g_ready_ev = NULL;
        }
#endif
    }
    nc_log("hakuX-app", "native core: leaving");
    exit_now(0); /* the core thread has no stop yet */
}
