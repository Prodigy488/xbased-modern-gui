/*
 * xbased — the game's frame (win_frame.h): Windows, then Linux (X11).
 */
#include "native/app/win_frame.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

#define WM_FRAME_TOGGLE (WM_APP + 1)
#define WM_FRAME_CLOSE  (WM_APP + 2)
#define FRAME_TICK_MS   50
#define FRAME_LINGER_MS 3000    /* no run's window in it this long: the session's over */
#define FRAME_WAIT_MS   10000   /* the first run's window this long at most */

/* ------------------------------------------------------------ the frame's process */

static HWND g_fw;
static bool g_fs, g_fs60, g_mode_set, g_had_child;
static RECT g_wrect;                /* windowed, the place to go back to */
static char g_dev[CCHDEVICENAME];   /* the monitor whose mode was set */
static ULONGLONG g_t0, g_gone_at;

static HWND top_child(void)
{
    return GetWindow(g_fw, GW_CHILD);
}

static void fit_children(void)
{
    RECT rc;
    GetClientRect(g_fw, &rc);
    for (HWND c = top_child(); c; c = GetWindow(c, GW_HWNDNEXT)) {
        SetWindowPos(c, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* fsrate=60: the monitor's highest mode at its size that's a whole number
 * of 60 Hz (59 too: 59.94), no faster than now; the current one if it's
 * one already (win_main.c fs_mode's, in Win32 terms) */
static void mode_set(const char *dev)
{
    DEVMODEA cur;
    memset(&cur, 0, sizeof(cur));
    cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsA(dev, ENUM_CURRENT_SETTINGS, &cur)) {
        return;
    }
    const DWORD f0 = cur.dmDisplayFrequency;
    if (f0 % 60 == 0 || (f0 + 1) % 60 == 0) {
        return;
    }
    DEVMODEA best, dm;
    bool have = false;
    for (DWORD i = 0;; i++) {
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsA(dev, i, &dm)) {
            break;
        }
        const DWORD f = dm.dmDisplayFrequency;
        if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight &&
            dm.dmBitsPerPel == cur.dmBitsPerPel && !(dm.dmDisplayFlags & DM_INTERLACED) && f <= f0 && f >= 59 &&
            f <= 241 && (f % 60 == 0 || (f + 1) % 60 == 0) && (!have || f > best.dmDisplayFrequency)) {
            best = dm;
            have = true;
        }
    }
    if (have) {
        best.dmFields &= DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
        if (ChangeDisplaySettingsExA(dev, &best, NULL, CDS_FULLSCREEN, NULL) == DISP_CHANGE_SUCCESSFUL) {
            g_mode_set = true;
            snprintf(g_dev, sizeof(g_dev), "%s", dev);
        }
    }
}

static void mode_restore(void)
{
    if (g_mode_set) {
        ChangeDisplaySettingsExA(g_dev, NULL, NULL, 0, NULL);
        g_mode_set = false;
    }
}

static void set_fullscreen(bool on)
{
    if (on == g_fs) {
        return;
    }
    if (on) {
        GetWindowRect(g_fw, &g_wrect);
        MONITORINFOEXA mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(g_fw, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi);
        if (g_fs60) {
            mode_set(mi.szDevice);
            GetMonitorInfoA(MonitorFromWindow(g_fw, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi);
        }
        const LONG_PTR vis = IsWindowVisible(g_fw) ? WS_VISIBLE : 0;   /* not shown by this, if it wasn't yet */
        SetWindowLongPtrA(g_fw, GWL_STYLE, WS_POPUP | vis | WS_CLIPCHILDREN);
        const RECT r = mi.rcMonitor;
        SetWindowPos(g_fw, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        mode_restore();
        SetWindowLongPtrA(g_fw, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        const RECT r = g_wrect;
        SetWindowPos(g_fw, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
    }
    g_fs = on;
    fit_children();
}

static LRESULT CALLBACK frame_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_SIZE:
        fit_children();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && top_child()) {
            SetFocus(top_child());     /* the keyboard to the run's window */
            return 0;
        }
        break;
    case WM_SETFOCUS:
        if (top_child()) {
            SetFocus(top_child());
            return 0;
        }
        break;
    case WM_CLOSE:
        /* the window's close button: the run's own quit (its window's) */
        if (top_child()) {
            for (HWND c = top_child(); c; c = GetWindow(c, GW_HWNDNEXT)) {
                PostMessageA(c, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        DestroyWindow(h);
        return 0;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)l;
        if (!g_fs) {
            SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_FRAME_TOGGLE:
        set_fullscreen(!g_fs);
        return 0;
    case WM_FRAME_CLOSE:
        ShowWindow(h, SW_HIDE);
        DestroyWindow(h);
        return 0;
    case WM_TIMER: {
        /* a run's window in it, or none: gone a moment between two runs
         * never (the next one is in before this one goes), so none for a
         * while is the session's end */
        const ULONGLONG now = GetTickCount64();
        if (top_child()) {
            g_had_child = true;
            g_gone_at = 0;
        } else if (g_had_child) {
            if (!g_gone_at) {
                g_gone_at = now;
                mode_restore();
                ShowWindow(h, SW_HIDE);     /* what's behind (the dashboard) at once */
            } else if (now - g_gone_at > FRAME_LINGER_MS) {
                DestroyWindow(h);
            }
        } else if (now - g_t0 > FRAME_WAIT_MS) {
            DestroyWindow(h);   /* the first run never came */
        }
        return 0;
    }
    case WM_DESTROY:
        mode_restore();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

int nc_frame_main(int argc, char **argv)
{
    if (argc < 6) {
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const char *token = argv[2];
    const bool fs = atoi(argv[3]) != 0;
    g_fs60 = atoi(argv[4]) != 0;
    wchar_t title[512];
    if (!MultiByteToWideChar(CP_UTF8, 0, argv[5], -1, title, 512)) {
        wcscpy(title, L"xbased");
    }
    char cls[96];
    snprintf(cls, sizeof(cls), "xbased_frame_%s", token);
    HINSTANCE hi = GetModuleHandleA(NULL);
    wchar_t wcls[96];
    MultiByteToWideChar(CP_UTF8, 0, cls, -1, wcls, 96);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = hi;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = wcls;
    if (!RegisterClassExW(&wc)) {
        return 3;
    }
    /* a window's size as the run's own was (1280x960 at the desktop's
     * scale), centered */
    const UINT dpi = GetDpiForSystem();
    RECT r = {0, 0, MulDiv(1280, (int)dpi, 96), MulDiv(960, (int)dpi, 96)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int ww = r.right - r.left, wh = r.bottom - r.top;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoA(MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
    const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;
    g_fw = CreateWindowExW(0, wcls, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, ww, wh, NULL, NULL, hi, NULL);
    if (!g_fw) {
        return 4;
    }
    g_t0 = GetTickCount64();
    if (fs) {
        set_fullscreen(true);   /* before it's shown: up as a window first, the desktop showed around it */
    }
    ShowWindow(g_fw, SW_SHOW);
    SetForegroundWindow(g_fw);
    SetTimer(g_fw, 1, FRAME_TICK_MS, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    mode_restore();
    return 0;
}

/* ------------------------------------------------------------ the game's side */

void *nc_frame_get(const char *title, bool fullscreen, bool fs60)
{
    const char *had = getenv("XBASED_FRAME_HWND");
    if (had && had[0]) {
        HWND h = (HWND)(uintptr_t)strtoull(had, NULL, 16);
        if (IsWindow(h)) {
            return h;     /* the session's, a run started again */
        }
    }
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    char token[48], cmd[MAX_PATH * 2 + 600], t[400];
    snprintf(token, sizeof(token), "%lu_%llu", (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64());
    snprintf(t, sizeof(t), "%s", title && title[0] ? title : "xbased");
    for (char *p = t; *p; p++) {
        if (*p == '"') {
            *p = '\'';
        }
    }
    snprintf(cmd, sizeof(cmd), "\"%s\" frame %s %d %d \"%s\"", exe, token, fullscreen ? 1 : 0, fs60 ? 1 : 0, t);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return NULL;
    }
    AllowSetForegroundWindow(pi.dwProcessId);   /* it takes the foreground this process was given */
    CloseHandle(pi.hThread);
    char cls[96];
    snprintf(cls, sizeof(cls), "xbased_frame_%s", token);
    HWND h = NULL;
    for (int i = 0; i < 500 && !h; i++) {
        h = FindWindowA(cls, NULL);
        if (!h) {
            if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
                break;  /* it went */
            }
        }
    }
    CloseHandle(pi.hProcess);
    if (h) {
        char v[32];
        snprintf(v, sizeof(v), "%llx", (unsigned long long)(uintptr_t)h);
        SetEnvironmentVariableA("XBASED_FRAME_HWND", v);   /* the runs this one starts: the same frame */
        _putenv_s("XBASED_FRAME_HWND", v);
    }
    return h;
}

static HWND hwnd_of(SDL_Window *w)
{
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

void nc_frame_adopt(void *frame, SDL_Window *child, const char *title)
{
    HWND f = (HWND)frame, c = hwnd_of(child);
    wchar_t wt[512];
    if (title && MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 512)) {
        SendMessageW(f, WM_SETTEXT, 0, (LPARAM)wt);  /* this run's title (another executable's) */
    }
    SetWindowLongPtrA(c, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
    SetWindowLongPtrA(c, GWL_EXSTYLE, 0);
    SetParent(c, f);
    RECT rc;
    GetClientRect(f, &rc);
    SetWindowPos(c, HWND_BOTTOM, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void nc_frame_raise(void *frame, SDL_Window *child)
{
    HWND f = (HWND)frame, c = hwnd_of(child);
    SetWindowPos(c, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (GetForegroundWindow() == f || GetForegroundWindow() == c) {
        SetFocus(c);
    } else {
        SetForegroundWindow(f);     /* the frame activated: its WM_ACTIVATE hands the keyboard down */
    }
}

void nc_frame_toggle_fullscreen(void *frame)
{
    PostMessageA((HWND)frame, WM_FRAME_TOGGLE, 0, 0);
}

bool nc_frame_minimized(void *frame)
{
    return IsIconic((HWND)frame) != 0;
}

void nc_frame_close(void *frame)
{
    if (frame) {
        PostMessageA((HWND)frame, WM_FRAME_CLOSE, 0, 0);
    }
}

#else /* ------------------------------------------------------------ Linux: X11 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME_TICK_MS   50
#define FRAME_LINGER_MS 3000
#define FRAME_WAIT_MS   10000
#define FRAME_MSG_TOGGLE 1
#define FRAME_MSG_CLOSE  2

/* Xlib's calls by name (SDL loads the library too; no link of our own: a
 * desktop without X11 still starts) */
static struct {
    bool ok;
    int (*ReparentWindow)(Display *, Window, Window, int, int);
    int (*MoveResizeWindow)(Display *, Window, int, int, unsigned, unsigned);
    int (*MapWindow)(Display *, Window);
    int (*RaiseWindow)(Display *, Window);
    int (*LowerWindow)(Display *, Window);
    int (*SetInputFocus)(Display *, Window, int, Time);
    Status (*QueryTree)(Display *, Window, Window *, Window *, Window **, unsigned *);
    int (*Free)(void *);
    Status (*GetWindowAttributes)(Display *, Window, XWindowAttributes *);
    int (*Flush)(Display *);
    Status (*SendEvent)(Display *, Window, Bool, long, XEvent *);
    Atom (*InternAtom)(Display *, const char *, Bool);
    int (*ChangeProperty)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int);
    int (*GetWindowProperty)(Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *, unsigned long *,
                             unsigned long *, unsigned char **);
    int (*StoreName)(Display *, Window, const char *);
    int (*Sync)(Display *, Bool);
    XErrorHandler (*SetErrorHandler)(XErrorHandler);
} X;

static bool x_load(void)
{
    static int tried;
    if (tried) {
        return X.ok;
    }
    tried = 1;
    void *h = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        return false;
    }
#define L(n) (*(void **)&X.n = dlsym(h, "X" #n)) != NULL
    X.ok = L(ReparentWindow) && L(MoveResizeWindow) && L(MapWindow) && L(RaiseWindow) && L(LowerWindow) &&
           L(SetInputFocus) && L(QueryTree) && L(Free) && L(GetWindowAttributes) && L(Flush) && L(SendEvent) &&
           L(InternAtom) && L(ChangeProperty) && L(GetWindowProperty) && L(StoreName) && L(Sync) &&
           L(SetErrorHandler);
#undef L
    return X.ok;
}

/* a window gone meanwhile (the run before's, as it ends): its BadWindow
 * isn't the end of this process, as Xlib's own handler makes it */
static int x_errors(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;
    return 0;
}

static uint64_t ms_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static Display *x_display(SDL_Window *w)
{
    return (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
}

static Window x_window(SDL_Window *w)
{
    return (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
}

/* the frame's children, topmost last (QueryTree's order); n returned */
static unsigned children(Display *d, Window f, Window *top)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0;
    if (!X.QueryTree(d, f, &root, &parent, &kids, &n)) {
        n = 0;
    }
    if (top) {
        *top = n ? kids[n - 1] : 0;
    }
    if (kids) {
        X.Free(kids);
    }
    return n;
}

static Atom g_msg_atom, g_min_atom;
static volatile int g_toggle, g_quit;

static bool frame_hook(void *ud, XEvent *e)
{
    (void)ud;
    if (e->type == ClientMessage && e->xclient.message_type == g_msg_atom) {
        if (e->xclient.data.l[0] == FRAME_MSG_TOGGLE) {
            g_toggle = 1;
        } else if (e->xclient.data.l[0] == FRAME_MSG_CLOSE) {
            g_quit = 1;
        }
        return false;
    }
    return true;
}

/* the frame's process: an SDL window of its own on X11, the runs' windows
 * inside it */
static int frame_run(int wfd, pid_t run, bool fs, bool fs60)
{
    signal(SIGPIPE, SIG_IGN);
    if (!SDL_Init(SDL_INIT_VIDEO) || strcmp(SDL_GetCurrentVideoDriver(), "x11") || !x_load()) {
        /* not X11 (Wayland): no frame; the run on its own, this the
         * process the dashboard waits on until it's done */
        if (write(wfd, "0\n", 2) < 0) {
        }
        close(wfd);
        SDL_Quit();
        int st = 0;
        while (waitpid(run, &st, 0) < 0) {
        }
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    SDL_Window *w = SDL_CreateWindow("xbased", 1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    Display *d = w ? x_display(w) : NULL;
    const Window fw = w ? x_window(w) : 0;
    if (!d || !fw) {
        if (write(wfd, "0\n", 2) < 0) {
        }
        close(wfd);
        int st = 0;
        while (waitpid(run, &st, 0) < 0) {
        }
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    X.SetErrorHandler(x_errors);
    g_msg_atom = X.InternAtom(d, "_XBASED_FRAME", False);
    g_min_atom = X.InternAtom(d, "_XBASED_MINIMIZED", False);
    SDL_SetX11EventHook(frame_hook, NULL);
    if (fs) {
        if (fs60) {
            /* the panel's highest mode a multiple of 60 Hz (win_main.c fs_mode's choice) */
            const SDL_DisplayID dd = SDL_GetDisplayForWindow(w);
            const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(dd);
            int n = 0;
            SDL_DisplayMode **ms = SDL_GetFullscreenDisplayModes(dd, &n);
            const SDL_DisplayMode *best = NULL;
            for (int i = 0; dm && ms && i < n; i++) {
                const int k = (int)(ms[i]->refresh_rate / 60.0f + 0.5f);
                if (ms[i]->w == dm->w && ms[i]->h == dm->h && k >= 1 && k <= 4 &&
                    ms[i]->refresh_rate <= dm->refresh_rate + 0.5f &&
                    SDL_fabsf(ms[i]->refresh_rate - 60.0f * (float)k) <= 0.6f * (float)k &&
                    (!best || ms[i]->refresh_rate > best->refresh_rate)) {
                    best = ms[i];
                }
            }
            const int k0 = dm ? (int)(dm->refresh_rate / 60.0f + 0.5f) : 0;
            const bool already = dm && k0 >= 1 && SDL_fabsf(dm->refresh_rate - 60.0f * (float)k0) <= 0.6f * (float)k0;
            SDL_SetWindowFullscreenMode(w, already ? NULL : best);
            SDL_free(ms);
        }
        SDL_SetWindowFullscreen(w, true);
    }
    char v[32];
    const int vn = snprintf(v, sizeof(v), "%lx\n", (unsigned long)fw);
    if (write(wfd, v, (size_t)vn) < 0) {
    }
    close(wfd);
    const uint64_t t0 = ms_now();
    uint64_t gone = 0;
    bool had = false, shown = false;
    int pw = 0, ph = 0;
    while (!g_quit) {
        SDL_Event e;
        int fitw = 0;
        if (SDL_WaitEventTimeout(&e, FRAME_TICK_MS)) {
            do {
                switch (e.type) {
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    /* the window's close: the run's own quit (its window's) */
                    Window root, parent, *kids = NULL;
                    unsigned n = 0;
                    if (X.QueryTree(d, fw, &root, &parent, &kids, &n) && n) {
                        const Atom proto = X.InternAtom(d, "WM_PROTOCOLS", False);
                        const Atom del = X.InternAtom(d, "WM_DELETE_WINDOW", False);
                        for (unsigned i = 0; i < n; i++) {
                            XEvent m;
                            memset(&m, 0, sizeof(m));
                            m.xclient.type = ClientMessage;
                            m.xclient.window = kids[i];
                            m.xclient.message_type = proto;
                            m.xclient.format = 32;
                            m.xclient.data.l[0] = (long)del;
                            m.xclient.data.l[1] = CurrentTime;
                            X.SendEvent(d, kids[i], False, NoEventMask, &m);
                        }
                    } else {
                        g_quit = 1;
                    }
                    if (kids) {
                        X.Free(kids);
                    }
                    break;
                }
                case SDL_EVENT_WINDOW_FOCUS_GAINED: {
                    Window top = 0;
                    XWindowAttributes a;
                    if (children(d, fw, &top) && X.GetWindowAttributes(d, top, &a) && a.map_state == IsViewable) {
                        X.SetInputFocus(d, top, RevertToParent, CurrentTime);    /* the keyboard to the run */
                    }
                    break;
                }
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_RESTORED: {
                    const long m = e.type == SDL_EVENT_WINDOW_MINIMIZED;
                    X.ChangeProperty(d, fw, g_min_atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)&m, 1);
                    break;
                }
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_RESIZED:
                    fitw = 1;
                    break;
                default:
                    break;
                }
            } while (SDL_PollEvent(&e));
        }
        if (g_toggle) {
            g_toggle = 0;
            SDL_SetWindowFullscreen(w, !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN));
        }
        int cw = 0, ch = 0;
        SDL_GetWindowSizeInPixels(w, &cw, &ch);
        if (fitw || cw != pw || ch != ph) {
            pw = cw;
            ph = ch;
            Window root, parent, *kids = NULL;
            unsigned n = 0;
            if (X.QueryTree(d, fw, &root, &parent, &kids, &n)) {
                for (unsigned i = 0; i < n; i++) {
                    X.MoveResizeWindow(d, kids[i], 0, 0, (unsigned)(cw > 0 ? cw : 1), (unsigned)(ch > 0 ? ch : 1));
                }
            }
            if (kids) {
                X.Free(kids);
            }
            X.Flush(d);
        }
        const uint64_t now = ms_now();
        if (children(d, fw, NULL)) {
            if (!shown) {
                shown = true;
                SDL_ShowWindow(w);
                SDL_RaiseWindow(w);
            }
            had = true;
            gone = 0;
        } else if (had) {
            if (!gone) {
                gone = now;
                SDL_HideWindow(w);      /* what's behind (the dashboard) at once */
            } else if (now - gone > FRAME_LINGER_MS) {
                break;
            }
        } else if (now - t0 > FRAME_WAIT_MS) {
            break;  /* the first run never came */
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            /* the runs that ended (the first is this process's child) */
        }
    }
    SDL_DestroyWindow(w);
    SDL_Quit();
    return 0;
}

void nc_frame_fork(bool fullscreen, bool fs60)
{
    int p[2];
    if (getenv("XBASED_FRAME_XID") || !getenv("DISPLAY") || pipe(p) < 0) {
        return;
    }
    const pid_t run = fork();
    if (run < 0) {
        close(p[0]);
        close(p[1]);
        return;
    }
    if (run > 0) {
        close(p[0]);
        _exit(frame_run(p[1], run, fullscreen, fs60));     /* the parent: the frame */
    }
    close(p[1]);
    /* the run: the frame's window, or "0" for none */
    char v[32] = "";
    struct pollfd pf = {p[0], POLLIN, 0};
    if (poll(&pf, 1, 5000) > 0) {
        const ssize_t n = read(p[0], v, sizeof(v) - 1);
        v[n > 0 ? n : 0] = 0;
    }
    close(p[0]);
    /* "0" too: no frame (Wayland), and the runs started again (exec'd,
     * the same process) don't fork another */
    char s[32];
    snprintf(s, sizeof(s), "%lx", strtoul(v, NULL, 16));
    setenv("XBASED_FRAME_XID", s, 1);
}

void *nc_frame_get(const char *title, bool fullscreen, bool fs60)
{
    (void)title;
    (void)fullscreen;
    (void)fs60;
    const char *v = getenv("XBASED_FRAME_XID");
    const unsigned long xid = v ? strtoul(v, NULL, 16) : 0;
    if (!xid || !x_load() || strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
        return NULL;
    }
    return (void *)(uintptr_t)xid;
}

static SDL_Window *g_child;     /* the run's window, for the calls without it */

void nc_frame_adopt(void *frame, SDL_Window *child, const char *title)
{
    Display *d = x_display(child);
    const Window f = (Window)(uintptr_t)frame, c = x_window(child);
    if (!d || !c) {
        return;
    }
    g_child = child;
    X.SetErrorHandler(x_errors);
    if (title && title[0]) {
        /* this run's title (another executable's) */
        X.ChangeProperty(d, f, X.InternAtom(d, "_NET_WM_NAME", False), X.InternAtom(d, "UTF8_STRING", False), 8,
                         PropModeReplace, (const unsigned char *)title, (int)strlen(title));
        X.StoreName(d, f, title);
    }
    XWindowAttributes a;
    memset(&a, 0, sizeof(a));
    X.GetWindowAttributes(d, f, &a);
    X.ReparentWindow(d, c, f, 0, 0);
    X.MoveResizeWindow(d, c, 0, 0, (unsigned)(a.width > 0 ? a.width : 1280), (unsigned)(a.height > 0 ? a.height : 960));
    X.MapWindow(d, c);
    X.LowerWindow(d, c);    /* under the run before's, until this one has drawn */
    X.Flush(d);
}

void nc_frame_raise(void *frame, SDL_Window *child)
{
    Display *d = x_display(child);
    const Window f = (Window)(uintptr_t)frame, c = x_window(child);
    if (!d || !c) {
        return;
    }
    X.RaiseWindow(d, c);
    XWindowAttributes a;
    if (X.GetWindowAttributes(d, f, &a) && a.map_state == IsViewable && X.GetWindowAttributes(d, c, &a) &&
        a.map_state == IsViewable) {
        X.SetInputFocus(d, c, RevertToParent, CurrentTime);
    }
    X.Flush(d);
}

static void send_msg(void *frame, long what)
{
    Display *d = g_child ? x_display(g_child) : NULL;
    if (!d) {
        return;
    }
    XEvent m;
    memset(&m, 0, sizeof(m));
    m.xclient.type = ClientMessage;
    m.xclient.window = (Window)(uintptr_t)frame;
    m.xclient.message_type = X.InternAtom(d, "_XBASED_FRAME", False);
    m.xclient.format = 32;
    m.xclient.data.l[0] = what;
    X.SendEvent(d, (Window)(uintptr_t)frame, False, NoEventMask, &m);
    X.Flush(d);
}

void nc_frame_toggle_fullscreen(void *frame)
{
    send_msg(frame, FRAME_MSG_TOGGLE);
}

bool nc_frame_minimized(void *frame)
{
    Display *d = g_child ? x_display(g_child) : NULL;
    if (!d) {
        return false;
    }
    Atom type = 0;
    int fmt = 0;
    unsigned long n = 0, left = 0;
    unsigned char *data = NULL;
    bool m = false;
    if (X.GetWindowProperty(d, (Window)(uintptr_t)frame, X.InternAtom(d, "_XBASED_MINIMIZED", False), 0, 1, False,
                            XA_CARDINAL, &type, &fmt, &n, &left, &data) == Success &&
        data && n == 1 && fmt == 32) {
        m = *(long *)data != 0;
    }
    if (data) {
        X.Free(data);
    }
    return m;
}

void nc_frame_close(void *frame)
{
    if (frame) {
        send_msg(frame, FRAME_MSG_CLOSE);
    }
}

#endif
