/*
 * xbased front end — play time (playtime.h).
 */
#include "native/app/ui/playtime.h"
#include "native/app/ui/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_KEYS 4096

static struct {
    char key[24];
    int64_t secs;
} g_pt[MAX_KEYS];
static int g_n;
static bool g_loaded;

static void path(char *out, size_t n)
{
    snprintf(out, n, "%splaytime.txt", cfg_data_dir());
}

static void load(void)
{
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    char p[1400];
    path(p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (!f) {
        return;
    }
    char line[128];
    while (g_n < MAX_KEYS && fgets(line, sizeof(line), f)) {
        char key[24];
        long long s = 0;
        if (sscanf(line, "%23s %lld", key, &s) == 2 && s > 0) {
            snprintf(g_pt[g_n].key, sizeof(g_pt[g_n].key), "%s", key);
            g_pt[g_n++].secs = s;
        }
    }
    fclose(f);
}

static void save(void)
{
    char p[1400], tmp[1410];
    path(p, sizeof(p));
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < g_n; i++) {
        fprintf(f, "%s %lld\n", g_pt[i].key, (long long)g_pt[i].secs);
    }
    fclose(f);
    remove(p);
    rename(tmp, p);
}

int64_t pt_get(const char *key)
{
    load();
    for (int i = 0; key && i < g_n; i++) {
        if (!strcmp(g_pt[i].key, key)) {
            return g_pt[i].secs;
        }
    }
    return 0;
}

void pt_add(const char *key, int64_t seconds)
{
    load();
    if (!key || !key[0] || seconds <= 0 || strlen(key) >= sizeof(g_pt[0].key)) {
        return;
    }
    for (int i = 0; i < g_n; i++) {
        if (!strcmp(g_pt[i].key, key)) {
            g_pt[i].secs += seconds;
            save();
            return;
        }
    }
    if (g_n < MAX_KEYS) {
        snprintf(g_pt[g_n].key, sizeof(g_pt[g_n].key), "%s", key);
        g_pt[g_n++].secs = seconds;
        save();
    }
}

const char *pt_text(int64_t s)
{
    static char b[4][32];
    static int k;
    k = (k + 1) & 3;
    if (s < 60) {
        b[k][0] = 0;
    } else if (s < 3600) {
        snprintf(b[k], sizeof(b[k]), "%dm", (int)(s / 60));
    } else {
        snprintf(b[k], sizeof(b[k]), "%dh %dm", (int)(s / 3600), (int)(s % 3600 / 60));
    }
    return b[k];
}
