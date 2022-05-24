/*
rvtimer.c - Timers, sleep functions
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
#include "compiler.h"
#include "utils.h"

#if defined(__unix__)
#include <unistd.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

uint64_t rvtimer_clocksource(uint64_t freq)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return ((now.tv_sec * 1000000000ULL) + now.tv_nsec) / (1000000000ULL / freq);
}

#elif defined(_WIN32)
#include <windows.h>

/*#define CLOCK_MONOTONIC_RAW 0
struct timespec { long tv_sec; long tv_nsec; };
static inline void clock_gettime(int t, struct timespec* tp)
{
    UNUSED(t);
    ULARGE_INTEGER tmp;
    GetSystemTimeAsFileTime((LPFILETIME)&tmp);
    tmp.QuadPart -= 0x19DB1DED53E8000ULL; // to UNIX time
    tp->tv_sec = tmp.QuadPart / 10000000ULL;
    tp->tv_nsec = (tmp.QuadPart % 10000000ULL) * 100;
}*/

uint64_t rvtimer_clocksource(uint64_t freq)
{
    static LARGE_INTEGER perf_freq = {0};
    if (perf_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&perf_freq);
        if (perf_freq.QuadPart == 0) {
            // Should not fail since WinXP
            rvvm_fatal("perf_clocksource not supported!");
        }
    }

    LARGE_INTEGER clk;
    QueryPerformanceCounter(&clk);
    return clk.QuadPart * freq / perf_freq.QuadPart;
}

#else
#warning No support for platform clocksource!

static uint64_t __rvtimer = 0;

uint64_t rvtimer_clocksource(uint64_t freq)
{
    return __rvtimer++ * freq / 1000;
}

#endif

void rvtimer_init(rvtimer_t* timer, uint64_t freq)
{
    timer->freq = freq;
    // Some dumb rv32 OSes may ignore higher timecmp bits
    timer->timecmp = 0xFFFFFFFFU;
    rvtimer_rebase(timer, 0);
}

uint64_t rvtimer_get(rvtimer_t* timer)
{
    return rvtimer_clocksource(timer->freq) - timer->begin;
}

void rvtimer_rebase(rvtimer_t* timer, uint64_t time)
{
    timer->begin = rvtimer_clocksource(timer->freq) - time;
}

bool rvtimer_pending(rvtimer_t* timer)
{
    return rvtimer_get(timer) >= timer->timecmp;
}

void sleep_ms(uint32_t ms)
{
#if defined(_WIN32)
    //Sleep(ms);
    SleepEx(ms, TRUE);
#elif defined(CLOCK_MONOTONIC_RAW)
    usleep(ms*1000);
#endif
}
