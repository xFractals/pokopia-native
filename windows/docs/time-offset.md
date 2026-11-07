# The game's clock: a time offset from the session

Status: in the working tree for review. `hle_time.c` serves `nn::time` from the host clock (`time(NULL)`,
`clock_gettime(CLOCK_REALTIME)`), so the game's date is Windows' date. This adds a shift to what the game believes "now" is, so a build that
takes days finishes in a session, without the player touching Windows' clock (which the rest of the PC lives by: certificates, file times,
saves synced to other devices).

| Switch | What it does |
|---|---|
| `DM_TIME_OFFSET=<seconds>` | the shift at start, in seconds. Signed: `DM_TIME_OFFSET=-172800` is two days back. `0x…` is read as hex |
| `DM_TIME_OFFSET_DAYS=<days>` | the same in days, fractions allowed (`2.5`). Read only when `DM_TIME_OFFSET` is unset |

`DM_FIXED_TIME` still wins over both: with it set, the clock is the fixed value and no shift is added (the deterministic runs stay
deterministic). Without either, the shift comes from the file below.

## The keys

Only while the game's own window is the foreground window (`GetAsyncKeyState`, read on a thread that starts at `time::Initialize`):

| Key | |
|---|---|
| F9 | +1 day |
| F8 | −1 day |
| F7 | +1 hour |
| F6 | −1 hour |
| F10 | back to the host's clock (0) |

The state shows twice: in the game window's title (`Pokopia    game date +1.0 days    (F8 -1, F9 +1, F10 = 0)`) and, so that it is
readable in full screen, as a box over the game for 2.6 seconds — a topmost, layered, no-activate window of the runtime's own, so the game
is never clicked away from and keeps the focus and its input. A shift already in place when the game window comes forward is shown once,
at the moment it takes the focus.

## Where the value lives

`%LOCALAPPDATA%\Pokopia Native\time-offset.txt` (`%APPDATA%` when the former is unset), one decimal number of seconds. Written on every
key press and read once at start, so a shift survives a restart until F10 or the file is deleted. A file rather than a switch only, so a
building that takes a week does not need a restart each day; it is a product decision, not a constraint.

## Why the offset is only on the read side

Three entries report "now", and the shift is added in those three only:

| Entry | |
|---|---|
| `StandardUserSystemClock::GetCurrentTime` (and the adjustable clock, which answers through it) | `time(NULL) + offset` |
| `StandardUserSystemClock::now` | the same |
| the C library's real-time clock (`…CurrentTimespec`, both clocks) | `clock_gettime(CLOCK_REALTIME) + offset` |

The conversions keep running on the host's libc over the epoch they are handed: `ToCalendarTime` is `localtime_r` of that epoch, so the
calendar follows the shift by itself; `ToPosixTime` is `mktime` and `ToPosixTimeFromUtc` is `timegm`, so calendar → epoch stays the
identity on the game's own epoch. That is the whole point: a game that stamps something with its clock and later converts that stamp back
must land on the same second, or its own timestamps disagree with its own clock (events due, timers, "last played"). What moves is the
clock, as on a console when its date is changed by hand — not the arithmetic under it. The user clock context and the clock snapshots
are built from `GetCurrentTime` over the steady clock, so they carry the shift with it and stay consistent with it; the steady clock and
the spans between snapshots are untouched (they are monotonic, and a shift has nothing to do with a duration).

## How to check it

- `ctest -R time_offset` (`nxrt-tests-time`, one of the runtime's tests): with `DM_TIME_OFFSET_DAYS=1` it checks that `now()` and the
  timespec entries report the host's time plus 86400 ± 10 s, that `ToCalendarTime` of that epoch is the following day in the host's zone,
  that `ToPosixTime` of that calendar returns the same epoch (±2 s, still a day ahead of the host), and that `ToPosixTimeFromUtc` of its
  `gmtime` agrees. It prints the four numbers, so the arithmetic is visible rather than asserted.
- The runtime's log, at start: `time offset: +86400 s (+1.00 days)`, and `(host clock)` when it is 0. Every key press logs the new value
  and which key (`time offset: +172800 s (+2.00 days) [+1 day]`). The first conversion logs the date it produced
  (`ToCalendarTime: first conversion 1794014696 → 2026-11-07 02:24 …`), which is the shifted one.

## Limits

- **Co-op / Link Play:** the shift is per process, so every device in a session needs the same one; a host and a visitor a day apart see
  two different dates. Not tried with Link Play.
- **The notice:** a topmost layered window draws over a windowed or borderless game and, in practice, over full screen too, but an
  exclusive full-screen mode that takes the display can hide it. The window title always has the state.
- **What the game writes:** anything the game stamps — its saves, its "last played" — carries the shifted date, because it is the game's
  own clock. Moving the shift back before loading a save made while it was forward is the game's business, exactly as setting a console's
  date back would be.
- The runtime's other time users (the save layer's file times, the log) read the host clock directly and are not shifted.
