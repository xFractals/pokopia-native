/* The time offset: the game's clock is the host's clock shifted, without touching Windows'.
   No game data, no window: the HLE adapters are called the way test_adapters.c calls them.

   What must hold:
     1. now() and the C library real-time clock report host time + offset.
     2. ToCalendarTime of that epoch gives the shifted calendar (the host's zone, as before).
     3. Calendar -> epoch is the identity on the game's own epoch: a calendar the game got from ToCalendarTime
        converts back to the epoch it came from, offset included. Putting the offset in this direction too would
        make the game's own timestamps disagree with its own clock.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "hle_registry.h"
#include "nxrt/runtime.h"

NXRT_EARLY_RESERVE()

static int g_fail;
#define T(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

typedef struct { int16_t year; int8_t month, day, hour, minute, second, pad; } Cal;
typedef struct { uint32_t wday, yday; char tzname[8]; uint32_t dst; int32_t off; } CalInfo;

static void call(const char *name, NxwGuestCpu *c) {
    const nxrt_hle_entry *e = nxrt_hle_find(name, NXRT_HLE_FORCED);
    T(e != NULL);
    if (e) e->adapter(c);
}

static int64_t now_game(void) {
    NxwGuestCpu c; memset(&c, 0, sizeof c);
    call("_ZN2nn4time23StandardUserSystemClock3nowEv", &c);
    return (int64_t)c.x[0];
}

static uint64_t timespec_game(int64_t *ts) {
    NxwGuestCpu c; memset(&c, 0, sizeof c);
    ts[0] = ts[1] = 0;
    c.x[0] = (uint64_t)(uintptr_t)ts;
    call("_ZN2nn4time41GetStandardUserSystemClockCurrentTimespecEP8timespec", &c);
    return c.x[0];
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    _putenv_s("DM_TIME_OFFSET_DAYS", "1");
    if (nxrt_init(&(nxrt_options){.quiet = 1})) { printf("FAIL nxrt_init\n"); return 1; }

    NxwGuestCpu c; memset(&c, 0, sizeof c);
    call("_ZN2nn4time10InitializeEv", &c);
    T((uint32_t)c.x[0] == 0);

    int64_t host = (int64_t)time(NULL);

    /* 1. the clock the game sees */
    int64_t game = now_game();
    int64_t d1 = game - host;
    printf("now: host %lld, game %lld (%+.3f days)\n", (long long)host, (long long)game, d1 / 86400.0);
    T(d1 > 86390 && d1 < 86410);

    int64_t ts[2];
    uint64_t r = timespec_game(ts);
    printf("timespec: rc %llu, %lld (%+.3f days)\n", (unsigned long long)r, (long long)ts[0], (ts[0] - host) / 86400.0);
    T(r == 0 && ts[0] - host > 86390 && ts[0] - host < 86410);

    /* 2. the calendar of that epoch is the next day, in the host's zone */
    Cal ct; CalInfo ai; int64_t when = game;
    memset(&ct, 0, sizeof ct); memset(&ai, 0, sizeof ai);
    memset(&c, 0, sizeof c);
    c.x[0] = (uint64_t)(uintptr_t)&ct; c.x[1] = (uint64_t)(uintptr_t)&ai; c.x[2] = (uint64_t)(uintptr_t)&when;
    call("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE", &c);
    T((uint32_t)c.x[0] == 0);
    struct tm h; time_t g = (time_t)game; localtime_s(&h, &g);
    printf("calendar(game): %04d-%02d-%02d %02d:%02d (host zone %s)\n", ct.year, ct.month, ct.day, ct.hour, ct.minute, ai.tzname);
    T(ct.year == h.tm_year + 1900 && ct.month == h.tm_mon + 1 && ct.day == h.tm_mday && ct.hour == h.tm_hour);

    /* 3. calendar -> epoch is the identity on the game's epoch (offset included) */
    int n = 1; int64_t back = 0;
    memset(&c, 0, sizeof c);
    c.x[0] = (uint64_t)(uintptr_t)&n; c.x[1] = (uint64_t)(uintptr_t)&back; c.x[2] = 1; c.x[3] = (uint64_t)(uintptr_t)&ct;
    call("_ZN2nn4time11ToPosixTimeEPiPNS0_9PosixTimeEiRKNS0_12CalendarTimeE", &c);
    printf("round trip: epoch %lld -> calendar -> epoch %lld (delta %lld s)\n", (long long)game, (long long)back, (long long)(back - game));
    T((uint32_t)c.x[0] == 0 && back - game > -2 && back - game < 2);
    T(back - host > 86390);   /* the offset is still in it: this is the game's epoch, not the host's */

    /* the UTC form agrees on the same epoch */
    struct tm gu; gmtime_s(&gu, &g);
    Cal cu = {(int16_t)(gu.tm_year + 1900), (int8_t)(gu.tm_mon + 1), (int8_t)gu.tm_mday, (int8_t)gu.tm_hour, (int8_t)gu.tm_min, (int8_t)gu.tm_sec, 0};
    memset(&c, 0, sizeof c);
    c.x[0] = (uint64_t)(uintptr_t)&cu;
    call("_ZN2nn4time18ToPosixTimeFromUtcERKNS0_12CalendarTimeE", &c);
    printf("ToPosixTimeFromUtc(gmtime) -> %lld (delta %lld s)\n", (long long)c.x[0], (long long)((int64_t)c.x[0] - game));
    T((int64_t)c.x[0] - game > -2 && (int64_t)c.x[0] - game < 2);

    printf(g_fail ? "nxrt.time_offset: FAILED\n" : "nxrt.time_offset: ok\n");
    return g_fail;
}
