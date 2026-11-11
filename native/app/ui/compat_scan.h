/*
 * hakuX front end — the library scan (compat_scan.c): for each game, the kernel functions its
 * default.xbe imports that this emulator doesn't implement (a call to one stops the game), read
 * from the images without running them, with a report file.
 */
#ifndef NATIVE_APP_UI_COMPAT_SCAN_H
#define NATIVE_APP_UI_COMPAT_SCAN_H

#include <stdbool.h>
#include <stddef.h>

void cs_start(void);                /* on a thread; no-op while one runs */
bool cs_busy(void);
int cs_done(void);                  /* games read so far */
int cs_total(void);
const char *cs_result(void);        /* the last scan's one-line result, "" before one */
const char *cs_report_path(void);   /* <data>/compat_report.txt */
bool cs_report_exists(void);
/* One image's imports this build lacks: how many (-1: unreadable), the first few names in out. */
int cs_missing_for_disc(const char *disc, char *out, size_t n);

#endif
