/*
 * hakuX front end — the library scan: see compat_scan.h.
 *
 * What it can tell: a game that imports a kernel function this emulator lacks stops the moment it
 * calls it ("kunimpl <ordinal> <name>" in the log). Many titles import such functions and never
 * call them (the I/O request functions are in most of them), so a name here is a lead, not a
 * verdict. What it can't tell: anything about the graphics (the log's "pixel shader: texture
 * modes ... sampled as PROJECT2D" line is that), or timing.
 */
#include "native/app/ui/compat_scan.h"

#include "native/app/ui/config.h"
#include "native/app/ui/library.h"
#include "native/app/ui/xbox_data.h"
#include "native/kernel/kernel.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_GAMES 4096

typedef struct Row {
    char title[128];
    char key[24];
    unsigned nimports;
    unsigned nmissing;
    uint16_t missing[64];
    bool read_ok, non_ordinal;
    char err[64];
} Row;

static SDL_AtomicInt g_busy, g_done, g_total;
static char g_result[160];
static SDL_Mutex *g_mu;

static void path_of(char *out, size_t n)
{
    snprintf(out, n, "%scompat_report.txt", cfg_data_dir());
}

const char *cs_report_path(void)
{
    static char p[1024];
    path_of(p, sizeof(p));
    return p;
}

bool cs_report_exists(void)
{
    FILE *f = fopen(cs_report_path(), "rb");
    if (f) {
        fclose(f);
    }
    return f != NULL;
}

bool cs_busy(void)
{
    return SDL_GetAtomicInt(&g_busy) != 0;
}

int cs_done(void)
{
    return SDL_GetAtomicInt(&g_done);
}

int cs_total(void)
{
    return SDL_GetAtomicInt(&g_total);
}

const char *cs_result(void)
{
    static char copy[sizeof(g_result)];
    if (!g_mu) {
        return "";
    }
    SDL_LockMutex(g_mu);
    snprintf(copy, sizeof(copy), "%s", g_result);
    SDL_UnlockMutex(g_mu);
    return copy;
}

static int by_missing(const void *a, const void *b)
{
    const Row *x = a, *y = b;
    if (x->nmissing != y->nmissing) {
        return x->nmissing < y->nmissing ? 1 : -1;
    }
    return SDL_strcasecmp(x->title, y->title);
}

static int by_count(const void *a, const void *b)
{
    const unsigned *x = a, *y = b;     /* {count, ordinal} */
    return x[0] != y[0] ? (x[0] < y[0] ? 1 : -1) : (x[1] < y[1] ? -1 : 1);
}

static int scan_thread(void *arg)
{
    (void)arg;
    const bool *impl = nc_kernel_impl_table();
    static LibGame *games[MAX_GAMES];
    const int n = lib_list(games, MAX_GAMES);
    SDL_SetAtomicInt(&g_total, n);
    SDL_SetAtomicInt(&g_done, 0);
    Row *rows = calloc((size_t)(n ? n : 1), sizeof(Row));
    unsigned ntable = 0, nfunc = 0;
    for (unsigned o = 1; o < NC_KORDINAL_MAX; o++) {
        const NcKExport *e = nc_kernel_export(o);
        if (e) {
            ntable++;
            nfunc += e->abi != NC_KABI_DATA;
        }
    }
    unsigned nimpl = 0;
    for (unsigned o = 1; o < NC_KORDINAL_MAX; o++) {
        const NcKExport *e = nc_kernel_export(o);
        nimpl += e && e->abi != NC_KABI_DATA && impl[o];
    }
    unsigned per_fn[NC_KORDINAL_MAX] = {0};
    int bad = 0, flagged = 0;
    for (int i = 0; i < n && rows; i++) {
        Row *r = &rows[i];
        snprintf(r->title, sizeof(r->title), "%s", games[i]->g.title);
        snprintf(r->key, sizeof(r->key), "%s", games[i]->g.key);
        XdImports im;
        r->read_ok = xd_read_imports(games[i]->g.path, &im);
        snprintf(r->err, sizeof(r->err), "%s", im.err);
        r->non_ordinal = im.non_ordinal;
        r->nimports = im.n;
        for (unsigned j = 0; j < im.n; j++) {
            const unsigned o = im.ord[j];
            const NcKExport *e = o < NC_KORDINAL_MAX ? nc_kernel_export(o) : NULL;
            if (!e) {
                continue;       /* an ordinal the Xbox kernel doesn't have: the loader says so */
            }
            if (e->abi != NC_KABI_DATA && !impl[o]) {
                if (r->nmissing < sizeof(r->missing) / sizeof(r->missing[0])) {
                    r->missing[r->nmissing] = (uint16_t)o;
                }
                r->nmissing++;
                per_fn[o]++;
            }
        }
        bad += !r->read_ok;
        flagged += r->read_ok && (r->nmissing || r->non_ordinal);
        SDL_SetAtomicInt(&g_done, i + 1);
    }
    if (rows) {
        qsort(rows, (size_t)n, sizeof(Row), by_missing);
    }
    FILE *f = fopen(cs_report_path(), "wb");
    if (f) {
        const time_t now = time(NULL);
        char when[40];
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M", localtime(&now));
        fprintf(f, "xbased Modern GUI: library scan, %s\r\n\r\n", when);
        fprintf(f, "This reads each game's default.xbe and lists the Xbox kernel functions it imports that\r\n"
                   "this emulator does not have. %u of the kernel's %u functions are implemented.\r\n"
                   "A game that calls a missing one stops at that call (\"kunimpl <number> <name>\" in the\r\n"
                   "log). Many games import such functions and never call them, so a name here is a lead,\r\n"
                   "not a verdict. It says nothing about graphics or timing: for those, send the game's\r\n"
                   "log from a run.\r\n\r\n",
                nimpl, nfunc);
        fprintf(f, "%d games scanned, %d with imports not implemented, %d could not be read.\r\n\r\n", n, flagged, bad);
        fprintf(f, "== Games with imports that are not implemented (most first)\r\n");
        for (int i = 0; i < n && rows; i++) {
            const Row *r = &rows[i];
            if (!r->read_ok || (!r->nmissing && !r->non_ordinal)) {
                continue;
            }
            fprintf(f, "\r\n%s  [%s]  %u imports, %u not implemented%s\r\n", r->title, r->key, r->nimports,
                    r->nmissing, r->non_ordinal ? ", and an import that is not by ordinal (the loader refuses it)" : "");
            fprintf(f, "   ");
            for (unsigned j = 0; j < r->nmissing && j < sizeof(r->missing) / sizeof(r->missing[0]); j++) {
                const NcKExport *e = nc_kernel_export(r->missing[j]);
                fprintf(f, "%s%s", j ? ", " : "", e ? e->name : "?");
            }
            fprintf(f, "\r\n");
        }
        fprintf(f, "\r\n== Not implemented, by how many games import each\r\n");
        unsigned (*cnt)[2] = calloc(NC_KORDINAL_MAX, sizeof(*cnt));
        unsigned nc = 0;
        for (unsigned o = 1; cnt && o < NC_KORDINAL_MAX; o++) {
            if (per_fn[o]) {
                cnt[nc][0] = per_fn[o];
                cnt[nc][1] = o;
                nc++;
            }
        }
        if (cnt) {
            qsort(cnt, nc, sizeof(*cnt), by_count);
            for (unsigned i = 0; i < nc; i++) {
                const NcKExport *e = nc_kernel_export(cnt[i][1]);
                fprintf(f, "%4u  %s (%u)\r\n", cnt[i][0], e ? e->name : "?", cnt[i][1]);
            }
            free(cnt);
        }
        if (bad) {
            fprintf(f, "\r\n== Could not be read\r\n");
            for (int i = 0; i < n && rows; i++) {
                if (!rows[i].read_ok) {
                    fprintf(f, "%s: %s\r\n", rows[i].title, rows[i].err);
                }
            }
        }
        fclose(f);
    }
    if (g_mu) {
        SDL_LockMutex(g_mu);
        snprintf(g_result, sizeof(g_result), "%d games: %d import functions that aren't here, %d unreadable", n,
                 flagged, bad);
        SDL_UnlockMutex(g_mu);
    }
    free(rows);
    SDL_SetAtomicInt(&g_busy, 0);
    return 0;
}

void cs_start(void)
{
    if (!g_mu) {
        g_mu = SDL_CreateMutex();
    }
    if (SDL_CompareAndSwapAtomicInt(&g_busy, 0, 1)) {
        SDL_Thread *t = SDL_CreateThread(scan_thread, "compat-scan", NULL);
        if (t) {
            SDL_DetachThread(t);
        } else {
            SDL_SetAtomicInt(&g_busy, 0);
        }
    }
}
