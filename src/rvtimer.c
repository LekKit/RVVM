/*
timer.c - Timers, sleep functions
Copyright (C) 2021  LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "rvtimer.h"

#if defined(__linux__)
#include <unistd.h>
#include <time.h>
#define HAS_CLOCK_GETTIME
#elif defined(BSD)
#include <unistd.h>
#include <time.h>
#define HAS_CLOCK_GETTIME
#elif defined(__APPLE__)
#include <unistd.h>
#include <time.h>
#define HAS_CLOCK_GETTIME
#elif defined(_WIN32)
#include <windows.h>
struct timespec { long tv_sec; long tv_nsec; };
static void clock_gettime(int t, struct timespec* tp)
{
    ULARGE_INTEGER tmp;
    GetSystemTimeAsFileTime((LPFILETIME)&tmp);
    tmp.QuadPart -= 0x19DB1DED53E8000ULL; // to UNIX time
    tp->tv_sec = tmp.QuadPart / 10000000ULL;
    tp->tv_nsec = (tmp.QuadPart % 10000000ULL) * 100;
}
#define CLOCK_MONOTONIC_RAW 0
#define HAS_CLOCK_GETTIME
#else
#warning No support for platform clocksource!
#endif

#if defined(HAS_CLOCK_GETTIME)
static inline uint64_t timespec_to_rvtimer(struct timespec* tp, uint64_t freq)
{
    return (tp->tv_sec * freq) + (tp->tv_nsec / 1000 * freq / 1000000);
}
#endif

void rvtimer_init(rvtimer_t* timer, uint64_t freq)
{
    timer->freq = freq;
    timer->time = 0;
    timer->timecmp = -1ULL;
    rvtimer_rebase(timer);
}

void rvtimer_update(rvtimer_t* timer)
{
#if defined(HAS_CLOCK_GETTIME)
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    timer->time = timespec_to_rvtimer(&now, timer->freq) - timer->begin;
#else
    timer->time++;
#endif
}

void rvtimer_rebase(rvtimer_t* timer)
{
#if defined(HAS_CLOCK_GETTIME)
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    timer->begin = timespec_to_rvtimer(&now, timer->freq) - timer->time;
#endif
}

bool rvtimer_pending(rvtimer_t* timer)
{
    rvtimer_update(timer);
    return timer->time >= timer->timecmp;
}

void sleep_ms(uint32_t ms)
{
#if defined(_WIN32)
    Sleep(ms);
#elif defined(HAS_CLOCK_GETTIME)
    usleep(ms*1000);
#endif
}
