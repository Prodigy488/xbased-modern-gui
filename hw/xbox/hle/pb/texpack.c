/*
 * hakuX HLE GPU — texture packs (texpack.h).
 */
#include "hw/xbox/hle/base.h"
#include "hw/xbox/hle/hle.h"
#include "hw/xbox/hle/host.h"
#include "hw/xbox/hle/pb/texpack.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "native/third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "native/third_party/stb/stb_image_write.h"

/* a set of hashes (open addressing; 0 stands for itself in `zero`) */
typedef struct Set {
    uint64_t *v;
    size_t cap, n;
    bool zero;
} Set;

static bool set_has(const Set *s, uint64_t k)
{
    if (!k) {
        return s->zero;
    }
    if (!s->cap) {
        return false;
    }
    for (size_t i = (size_t)(k * 0x9E3779B97F4A7C15ull >> 32) & (s->cap - 1);; i = (i + 1) & (s->cap - 1)) {
        if (s->v[i] == k) {
            return true;
        }
        if (!s->v[i]) {
            return false;
        }
    }
}

static void set_add(Set *s, uint64_t k)
{
    if (!k) {
        s->zero = true;
        return;
    }
    if (set_has(s, k)) {
        return;
    }
    if ((s->n + 1) * 2 > s->cap) {
        Set o = *s;
        s->cap = o.cap ? o.cap * 2 : 1024;
        s->v = g_malloc0(s->cap * sizeof(uint64_t));
        s->n = 0;
        for (size_t i = 0; i < o.cap; i++) {
            if (o.v[i]) {
                set_add(s, o.v[i]);
            }
        }
        g_free(o.v);
    }
    size_t i = (size_t)(k * 0x9E3779B97F4A7C15ull >> 32) & (s->cap - 1);
    while (s->v[i]) {
        i = (i + 1) & (s->cap - 1);
    }
    s->v[i] = k;
    s->n++;
}

static Set g_absent, g_present, g_dumped;
static unsigned g_nloaded, g_ndumped, g_nrejected, g_nused;

/* <base>textures/<title id>/ (and its dump/), made at first use */
static char *dir;

const char *hle_texpack_dir(void)
{
    return dir;
}

static const char *pack_dir(void)
{
    if (!dir) {
        dir = g_strdup_printf("%stextures/%08X/", hle_host_base_path(), g_hle.title.xbe.title_id);
        g_autofree char *dump = g_strdup_printf("%sdump", dir);
        g_autofree char *aut = g_strdup_printf("%sauto", dir);
        g_mkdir_with_parents(dump, 0775);
        g_mkdir_with_parents(aut, 0775);
        hle_log("hakuX-hle-front", "texture packs: %s (PNG files named by the textures' hashes; dump/ holds the "
                "game's own, written out when that is on; auto/ those an upscaler made of them)", dir);
    }
    return dir;
}

uint64_t hle_texpack_hash(const void *data, size_t len, uint32_t fmt, uint32_t w, uint32_t h, const uint32_t *pal,
                          uint32_t npal)
{
    uint64_t x = XXH3_64bits(data, len);
    if (pal && npal) {
        x ^= XXH3_64bits(pal, (size_t)npal * 4) * 0x9E3779B97F4A7C15ull;
    }
    const uint32_t tail[4] = { fmt, w, h, (uint32_t)len };
    return x ^ (XXH3_64bits(tail, sizeof(tail)) * 0xC2B2AE3D27D4EB4Full);
}

static bool file_there(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
    }
    return f != NULL;
}

bool hle_texpack_has(uint64_t hash)
{
    if (set_has(&g_absent, hash)) {
        return false;
    }
    if (set_has(&g_present, hash)) {
        return true;
    }
    g_autofree char *own = g_strdup_printf("%s%016llx.png", pack_dir(), (unsigned long long)hash);
    g_autofree char *aut = g_strdup_printf("%sauto/%016llx.png", pack_dir(), (unsigned long long)hash);
    const bool there = file_there(own) || file_there(aut);
    set_add(there ? &g_present : &g_absent, hash);
    return there;
}

void hle_texpack_reject(uint64_t hash, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh, uint32_t fmt, bool decoded,
                        float colour, float alpha)
{
    set_add(&g_absent, hash);
    if (g_nrejected++ < 24) {
        /* the numbers say why: sizes that don't divide, a format this can't decode, or how far (of 255) the
         * picture averaged down is from the texture in colour and in alpha (28 is the most allowed) */
        hle_log("hakuX-hle-front", "texture packs: auto/%016llx.png (%ux%u) not used: the game's texture is %ux%u "
                "format 0x%02X%s, colour off by %.1f, alpha off by %.1f (#%u of 24 logged)",
                (unsigned long long)hash, w, h, ow, oh, fmt, decoded ? "" : " (not decoded)", (double)colour,
                (double)alpha, g_nrejected);
    }
}

void hle_texpack_used(uint64_t hash, uint32_t w, uint32_t h, bool from_auto)
{
    g_nused++;
    if (g_nloaded++ < 16) {
        hle_log("hakuX-hle-front", "texture packs: %s%016llx.png %ux%u in use (#%u of 16 logged)",
                from_auto ? "auto/" : "", (unsigned long long)hash, w, h, g_nloaded);
    }
}

void hle_texpack_counts(unsigned *used, unsigned *rejected)
{
    *used = g_nused;
    *rejected = g_nrejected;
}

uint8_t *hle_texpack_load(uint64_t hash, uint32_t *w, uint32_t *h, bool *from_auto)
{
    *from_auto = false;
    if (set_has(&g_absent, hash)) {
        return NULL;
    }
    /* your own file first, then the upscaler's */
    g_autofree char *path = g_strdup_printf("%s%016llx.png", pack_dir(), (unsigned long long)hash);
    gchar *file = NULL;
    gsize len = 0;
    bool made = false;
    if (!g_file_get_contents(path, &file, &len, NULL) || !file) {
        g_free(path);
        path = g_strdup_printf("%sauto/%016llx.png", pack_dir(), (unsigned long long)hash);
        made = true;
        if (!g_file_get_contents(path, &file, &len, NULL) || !file) {
            set_add(&g_absent, hash);
            return NULL;
        }
    }
    int iw = 0, ih = 0, comp = 0;
    uint8_t *px = len <= INT32_MAX ? stbi_load_from_memory((const stbi_uc *)file, (int)len, &iw, &ih, &comp, 4) : NULL;
    g_free(file);
    if (!px || iw < 1 || ih < 1 || iw > 8192 || ih > 8192) {
        hle_log("hakuX-hle-front", "texture packs: %s not used (%s)", path,
                px ? "larger than 8192 pixels a side" : "not a PNG this reads");
        if (px) {
            stbi_image_free(px);
        } else if (made) {
            remove(path);               /* an upscale cut short: made again after the next run */
        }
        set_add(&g_absent, hash);
        return NULL;
    }
    *w = (uint32_t)iw;
    *h = (uint32_t)ih;
    *from_auto = made;
    return px;
}

void hle_texpack_free(uint8_t *rgba)
{
    stbi_image_free(rgba);
}

bool hle_texpack_dump_wanted(uint64_t hash)
{
    if (set_has(&g_dumped, hash)) {
        return false;
    }
    set_add(&g_dumped, hash);
    return true;
}

typedef struct Out {
    uint8_t *p;
    size_t n, cap;
} Out;

static void out_write(void *ctx, void *data, int size)
{
    Out *o = ctx;
    if (size <= 0) {
        return;
    }
    if (o->n + (size_t)size > o->cap) {
        o->cap = MAX(o->cap * 2, o->n + (size_t)size + 4096);
        o->p = g_realloc(o->p, o->cap);
    }
    memcpy(o->p + o->n, data, (size_t)size);
    o->n += (size_t)size;
}

void hle_texpack_dump(uint64_t hash, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    g_autofree char *path = g_strdup_printf("%sdump/%016llx.png", pack_dir(), (unsigned long long)hash);
    FILE *have = fopen(path, "rb");
    if (have) {
        fclose(have);                   /* written by an earlier run */
        return;
    }
    Out o = { 0 };
    if (stbi_write_png_to_func(out_write, &o, (int)w, (int)h, 4, rgba, (int)w * 4) && o.n) {
        g_file_set_contents(path, (const gchar *)o.p, (gssize)o.n, NULL);
        if (g_ndumped++ < 8 || !(g_ndumped & 255)) {
            hle_log("hakuX-hle-front", "texture packs: wrote dump/%016llx.png (%ux%u; %u so far)",
                    (unsigned long long)hash, w, h, g_ndumped);
        }
    }
    g_free(o.p);
}
