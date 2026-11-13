/*
 * xbased's version: the one place it is written. The dashboard (the update check, the About line) and the
 * window loop (the log's first line, the first-run notice) both read it; two copies of it once disagreed, so
 * the update check compared the release against an older number than the build's own.
 */
#ifndef NC_APP_VERSION_H
#define NC_APP_VERSION_H

#define XBASED_VERSION "1.0.76.40"

#endif
