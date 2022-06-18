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

#if defined(__unix__) || defined(__APPLE__)
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
#include <time.h>
#warning No support for platform clocksource!

uint64_t rvtimer_clocksource(uint64_t freq)
{
    return time(0) * freq;
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
    Sleep(ms);
#elif defined(__unix__) || defined(__APPLE__)
    usleep(ms * 1000);
#else
    UNUSED(ms);
#endif
}
