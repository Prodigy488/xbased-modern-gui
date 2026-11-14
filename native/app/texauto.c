/*
 * xbased — automatic texture upscaling: after a game, the textures it wrote out (hle/textures/<title id>/dump/,
 * hw/xbox/hle/pb/texpack.h) and that have no picture yet in the pack or in auto/ are handed to Real-ESRGAN
 * (realesrgan-ncnn-vulkan, the portable program, not included), which writes them four times the size into
 * auto/. It runs on its own after xbased has gone on; the game uses what is there the next time it starts.
 *
 * The program is looked for as $XBASED_REALESRGAN, then realesrgan/realesrgan-ncnn-vulkan[.exe] beside xbased,
 * then in the data folder. Each run gets a folder of its own for its input (dump/queue-<time>), so two runs
 * never take each other's files; those of runs a day old are removed.
 */
#ifdef _WIN32
#include <windows.h>
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "native/app/app.h"

#ifdef _WIN32
#define TOOL "realesrgan-ncnn-vulkan.exe"
#else
#define TOOL "realesrgan-ncnn-vulkan"
#endif

static bool there(const char *path)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(path, &pi) && pi.type != SDL_PATHTYPE_NONE;
}

/* The program's path into tool (and its folder's length into *dirlen); false without one. */
static bool find_tool(const char *exe_dir, const char *data_dir, char *tool, size_t n, size_t *dirlen)
{
    const char *env = SDL_getenv("XBASED_REALESRGAN");
    if (env && *env && there(env)) {
        SDL_snprintf(tool, n, "%s", env);
    } else {
        SDL_snprintf(tool, n, "%srealesrgan/" TOOL, exe_dir);
        if (!there(tool)) {
            SDL_snprintf(tool, n, "%srealesrgan/" TOOL, data_dir);
            if (!there(tool)) {
                return false;
            }
        }
    }
    size_t l = SDL_strlen(tool);
    while (l && tool[l - 1] != '/' && tool[l - 1] != '\\') {
        l--;
    }
    *dirlen = l;
    return true;
}

/* The upscaler started last, while it runs: one at a time (it takes the whole GPU), and a run is asked for
 * every few seconds during a game (win_main.c) as well as at its end. */
static SDL_Process *g_job;
static SDL_AtomicInt g_in;
/* How much one run takes: input pixels (0: all that is waiting). A run beside a game is kept small, so that it
 * holds the GPU for a second or so and then lets go; the run when a game ends takes everything. */
static int g_budget;
static char g_prev[80];        /* what the batch before ended with, if badly, for the next note */

void nc_texauto_set_budget(int pixels)
{
    g_budget = pixels;
}

/* The picture's size from its PNG header (8 bytes of signature, then IHDR: width and height, big-endian). */
static bool png_size(const char *path, int *w, int *h)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) {
        return false;
    }
    unsigned char b[24];
    const bool ok = SDL_ReadIO(io, b, sizeof(b)) == sizeof(b) && !SDL_memcmp(b + 12, "IHDR", 4);
    SDL_CloseIO(io);
    if (!ok) {
        return false;
    }
    *w = b[16] << 24 | b[17] << 16 | b[18] << 8 | b[19];
    *h = b[20] << 24 | b[21] << 16 | b[22] << 8 | b[23];
    return *w > 0 && *h > 0;
}

typedef struct Cand {
    char name[96];
    SDL_Time mtime;
    int px;
} Cand;

static int newer_first(const void *x, const void *y)
{
    const Cand *a = x, *b = y;
    return a->mtime < b->mtime ? 1 : a->mtime > b->mtime ? -1 : 0;
}

static int run_one(const char *dir, const char *exe_dir, const char *data_dir, char *note, size_t note_n);

int nc_texauto_run(const char *dir, const char *exe_dir, const char *data_dir, char *note, size_t note_n)
{
    note[0] = 0;
    if (!SDL_CompareAndSwapAtomicInt(&g_in, 0, 1)) {
        return 0;               /* another thread is at it */
    }
    int n = 0;
    if (g_job) {
        int code = 0;
        if (SDL_WaitProcess(g_job, false, &code)) {
            SDL_DestroyProcess(g_job);
            g_job = NULL;
            /* a run that failed (no Vulkan device, a crash) is said in the next run's note, not lost */
            g_prev[0] = 0;
            if (code) {
                SDL_snprintf(g_prev, sizeof(g_prev), "the batch before ended with code %d; ", code);
            }
        }
    }
    if (!g_job) {
        n = run_one(dir, exe_dir, data_dir, note, note_n);
    }
    SDL_SetAtomicInt(&g_in, 0);
    return n;
}

static int run_one(const char *dir, const char *exe_dir, const char *data_dir, char *note, size_t note_n)
{
    char tool[1200], dump[1200], aut[1200], queue[1300], a[1400], b[1400], models[1300];
    size_t tdir = 0;
    note[0] = 0;
    if (!dir || !*dir) {
        return 0;
    }
    if (!find_tool(exe_dir, data_dir, tool, sizeof(tool), &tdir)) {
        SDL_snprintf(note, note_n, "no %s (looked in %srealesrgan/ and %srealesrgan/)", TOOL, exe_dir, data_dir);
        return -1;
    }
    SDL_snprintf(dump, sizeof(dump), "%sdump", dir);
    SDL_snprintf(aut, sizeof(aut), "%sauto", dir);
    SDL_CreateDirectory(aut);
    /* earlier runs' input folders, a day old: gone */
    int nq = 0;
    char **old = SDL_GlobDirectory(dump, "queue-*", 0, &nq);
    const SDL_Time now = (SDL_Time)time(NULL) * 1000000000ll;
    for (int i = 0; old && i < nq; i++) {
        SDL_PathInfo pi;
        SDL_snprintf(a, sizeof(a), "%s/%s", dump, old[i]);
        if (SDL_GetPathInfo(a, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY && now - pi.modify_time > 86400ll * 1000000000ll) {
            int nf = 0;
            char **f = SDL_GlobDirectory(a, "*", 0, &nf);
            for (int k = 0; f && k < nf; k++) {
                SDL_snprintf(b, sizeof(b), "%s/%s", a, f[k]);
                SDL_RemovePath(b);
            }
            SDL_free(f);
            SDL_RemovePath(a);
        }
    }
    SDL_free(old);
    /* this run's input: the dumped textures with no picture of the same name in the pack or in auto/, and with
     * something to gain: pictures are used at 1024 a side at most (pbres.c texpack_cap), so a texture that is
     * 1024 or more already has no use for a 4x picture (it would only be made smaller again). The newest dumps
     * first (what the game is showing now), and up to the budget. */
    int nf = 0, todo = 0, ncand = 0;
    char **f = SDL_GlobDirectory(dump, "*.png", 0, &nf);
    Cand *cand = nf > 0 ? SDL_calloc((size_t)nf, sizeof(Cand)) : NULL;
    SDL_snprintf(queue, sizeof(queue), "%s/queue-%lld", dump, (long long)time(NULL));
    for (int i = 0; cand && f && i < nf; i++) {
        SDL_snprintf(a, sizeof(a), "%s/%s", aut, f[i]);
        SDL_snprintf(b, sizeof(b), "%s%s", dir, f[i]);
        if (there(a) || there(b)) {
            continue;
        }
        /* refused twice (auto/<name>.bad, hle_texpack_reject): not again */
        const size_t stem = SDL_strlen(f[i]) > 4 ? SDL_strlen(f[i]) - 4 : 0;
        SDL_snprintf(a, sizeof(a), "%s/%.*s.bad", aut, (int)stem, f[i]);
        if (there(a)) {
            continue;
        }
        SDL_snprintf(a, sizeof(a), "%s/%s", dump, f[i]);
        int w = 0, h = 0;
        if (!png_size(a, &w, &h) || w >= 1024 || h >= 1024) {
            continue;
        }
        SDL_PathInfo pi;
        if (SDL_strlen(f[i]) >= sizeof(cand[0].name) || !SDL_GetPathInfo(a, &pi)) {
            continue;
        }
        SDL_strlcpy(cand[ncand].name, f[i], sizeof(cand[ncand].name));
        cand[ncand].mtime = pi.modify_time;
        cand[ncand].px = w * h;
        ncand++;
    }
    if (g_budget > 0) {
        SDL_qsort(cand, (size_t)ncand, sizeof(Cand), newer_first);
    }
    long long used_px = 0;
    for (int i = 0; i < ncand; i++) {
        if (g_budget > 0 && todo && used_px + cand[i].px > g_budget) {
            continue;           /* too much for this run: a later one (a smaller one may still fit) */
        }
        if (g_budget > 0 && todo >= 150) {
            break;              /* beside a game: not thousands of small files in one run (each is read, */
        }                       /* written and compressed on the CPU the game runs on) */
        if (!todo && !SDL_CreateDirectory(queue)) {
            break;
        }
        SDL_snprintf(a, sizeof(a), "%s/%s", dump, cand[i].name);
        SDL_snprintf(b, sizeof(b), "%s/%s", queue, cand[i].name);
        if (SDL_CopyFile(a, b)) {
            todo++;
            used_px += cand[i].px;
        }
    }
    SDL_free(cand);
    SDL_free(f);
    if (!todo) {
        return 0;
    }
    SDL_snprintf(models, sizeof(models), "%.*smodels", (int)tdir, tool);
    /* "-t 128": small tiles, so that each GPU dispatch of the upscaler is short. With its own choice (large
     * tiles) a batch can hold the GPU for seconds beside a game drawn at 4x, and Windows resets a GPU that
     * does not answer in about 2 s (VK_ERROR_DEVICE_LOST in the game's vkQueueSubmit2). */
    /* Beside a game ("-j 1:1:1", and the lowest priority below): one thread each to read, upscale and write,
     * where its own choice (1:2:2) and normal priority took CPU time from the game and the window's thread:
     * a Half-Life 2 log had some 30 hitches of 100 to 320 ms ("UI missed N front frames") over 35 batches. */
    const char *args[] = { tool, "-i", queue, "-o", aut, "-n", "realesrgan-x4plus", "-s", "4", "-f", "png",
                           "-m", models, "-t", "128", g_budget > 0 ? "-j" : NULL, "1:1:1", NULL };
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    /* nothing of its output wanted (it writes its progress); not a "background" process, whose end can't be
     * asked for: this one's is (g_job), and it runs on all the same when xbased has gone */
#ifdef _WIN32
    /* Windows: a console program started from a windowed one gets a console window of its own unless it is
     * started as a "background" process (SDL: CREATE_NO_WINDOW), which there can still be asked for its end.
     * (1.0.76.30 started it without: a black window came up over the game at every batch.) */
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process *pr = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    if (!pr) {
        SDL_snprintf(note, note_n, "%s did not start: %s", tool, SDL_GetError());
        return -1;
    }
#ifdef _WIN32
    if (g_budget > 0) {
        HANDLE hp = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                (DWORD)SDL_GetNumberProperty(SDL_GetProcessProperties(pr), SDL_PROP_PROCESS_PID_NUMBER, 0));
        if (hp) {
            SetPriorityClass(hp, IDLE_PRIORITY_CLASS);
            CloseHandle(hp);
        }
    }
#endif
    g_job = pr;                         /* it runs on; its end is asked for at the next call */
    SDL_snprintf(note, note_n, "%s%d textures to %s (into %s)", g_prev, todo, tool, aut);
    g_prev[0] = 0;
    return todo;
}
