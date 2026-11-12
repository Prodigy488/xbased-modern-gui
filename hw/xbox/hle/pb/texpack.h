/*
 * hakuX HLE GPU — texture packs: a game's textures written out as PNG files, and PNG files of the same names
 * drawn in their place (any size). The files are in <base>textures/<title id>/ (dumps in its dump/ folder), named
 * by a hash of the texture as the game holds it (XXH3-64 of its largest level's bytes, then of its palette, its
 * format and size). hle_host_texpack(): bit 0 use packs, bit 1 write the textures out, bit 2 both, for an
 * upscaler run after the game (native/app/texauto.c) whose pictures, in auto/, are used like a pack's.
 */
#ifndef HW_XBOX_HLE_PB_TEXPACK_H
#define HW_XBOX_HLE_PB_TEXPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The title's folder, once anything used it this run (else NULL). */
const char *hle_texpack_dir(void);
uint64_t hle_texpack_hash(const void *data, size_t len, uint32_t fmt, uint32_t w, uint32_t h, const uint32_t *pal,
                          uint32_t npal);
/* Whether there is a picture for hash, your own or the upscaler's (the file looked for once a run). */
bool hle_texpack_has(uint64_t hash);
/* The pack's picture for hash: bytes R, G, B, A, *w x *h (hle_texpack_free); NULL without one. *from_auto: it is
 * the upscaler's (auto/), which the caller checks against the game's texture (hle_texpack_reject if unlike). */
uint8_t *hle_texpack_load(uint64_t hash, uint32_t *w, uint32_t *h, bool *from_auto);
void hle_texpack_reject(uint64_t hash, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh, uint32_t fmt, bool decoded,
                        float colour);
/* A picture went up in the texture's place (the log, the counts). */
void hle_texpack_used(uint64_t hash, uint32_t w, uint32_t h, bool from_auto);
/* How many pictures were used and how many of the upscaler's refused, this run. */
void hle_texpack_counts(unsigned *used, unsigned *rejected);
void hle_texpack_free(uint8_t *rgba);
/* True the first time for a hash this run: the caller decodes the texture and hands it to hle_texpack_dump. */
bool hle_texpack_dump_wanted(uint64_t hash);
void hle_texpack_dump(uint64_t hash, const uint8_t *rgba, uint32_t w, uint32_t h);

#endif
