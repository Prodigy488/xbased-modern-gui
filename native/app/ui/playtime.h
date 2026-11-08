/*
 * xbased front end — play time (playtime.c): the seconds each game has been
 * run, kept in <data>/playtime.txt ("<key> <seconds>" a line, the library's
 * game key).
 */
#ifndef NATIVE_APP_UI_PLAYTIME_H
#define NATIVE_APP_UI_PLAYTIME_H

#include <stddef.h>
#include <stdint.h>

int64_t pt_get(const char *key);
void pt_add(const char *key, int64_t seconds);
/* "42h 5m", "5m", "" for none. */
const char *pt_text(int64_t seconds);

#endif
