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
#include "dlib.h"

#include <time.h>

#ifdef __linux__
#include <sys/prctl.h> // For PR_SET_TIMERSLACK
#endif

#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#include <sched.h> // For sched_yield()
#define SCHED_YIELD_IMPL
#endif

#ifdef _WIN32
// Use QueryPerformanceCounter()
#include <windows.h>
#include "atomics.h"

static uint32_t qpc_crit = 0;
static uint64_t qpc_off = 0, qpc_last = 0, qpc_freq = 0;
static uint64_t qpc_last_checked = 0, uit_last_checked = 0;

static BOOL (__stdcall *query_uit)(PULONGLONG) = NULL;

static uint64_t qpc_get_frequency()
{
    LARGE_INTEGER qpc = {0};
    QueryPerformanceFrequency(&qpc);
    if (qpc.QuadPart == 0) {
        rvvm_fatal("QueryPerformanceFrequency() failed!");
    }
    return qpc.QuadPart;
}

static uint64_t qpc_get_clock()
{
    LARGE_INTEGER qpc = {0};
    if (!QueryPerformanceCounter(&qpc)) {
        rvvm_fatal("QueryPerformanceCounter() failed!");
    }
    return qpc.QuadPart;
}

static uint64_t uit_get_clock()
{
    ULONGLONG uit = 0;
    if (query_uit) query_uit(&uit);
    return uit;
}

uint64_t rvtimer_clocksource(uint64_t freq)
{
    // Read the latest cached timer value from userspace
    uint64_t qpc_val = atomic_load_uint64_ex(&qpc_last, ATOMIC_ACQUIRE);

    if (!atomic_swap_uint32_ex(&qpc_crit, 1, ATOMIC_ACQUIRE)) {
        // Claimed the QPC lock, obtain new clock timestamp
        if (qpc_freq == 0) {
            // Initialize the clock frequency once
            qpc_freq = qpc_get_frequency();
            // Initialize unbiased backup clock if present
            query_uit = dlib_get_symbol("kernel32.dll", "QueryUnbiasedInterruptTime");
        }

        uint64_t qpc_new = qpc_get_clock() + qpc_off;
        if (qpc_new < qpc_val) {
            // Sometimes TSC drifts back on obscure hardware, Windows doesn't fix this up
            DO_ONCE(rvvm_warn("Unstable clocksource (backward drift observed)"));
        } else {
            if (query_uit) {
                // Check unbiased backup clock to compensate for suspend & forward jumps
                uint64_t qpc_delta = qpc_new - qpc_last_checked;
                if (qpc_delta > qpc_freq) {
                    uint64_t uit_new = uit_get_clock();
                    uint64_t uit_delta = (uit_new - uit_last_checked) * qpc_freq / 10000000U;
                    if (qpc_delta > uit_delta + qpc_freq && qpc_last_checked) {
                        uint64_t compensate = EVAL_MIN(qpc_delta - uit_delta, qpc_new - qpc_val);
                        qpc_off -= compensate;
                        qpc_new -= compensate;
                    }

                    qpc_last_checked = qpc_new;
                    uit_last_checked = uit_new;
                }
            }

            // Cache the new timer value
            qpc_val = qpc_new;
            atomic_store_uint64_ex(&qpc_last, qpc_val, ATOMIC_RELEASE);
        }

        atomic_store_uint32_ex(&qpc_crit, 0, ATOMIC_RELEASE);
    }

    return rvtimer_convert_freq(qpc_val, qpc_freq, freq);
}

#elif defined(__APPLE__)
// Use mach_absolute_time() on Mac OS
#include <mach/mach_time.h>

static mach_timebase_info_data_t mach_clk_info = {0};
static uint64_t mach_clk_freq = 0;

uint64_t rvtimer_clocksource(uint64_t freq)
{
    DO_ONCE({
        mach_timebase_info(&mach_clk_info);
        if (mach_clk_info.numer == 0 || mach_clk_info.denom == 0) {
            rvvm_fatal("mach_timebase_info() failed!");
        }
        // Calculate Mach timer frequency
        mach_clk_freq = (mach_clk_info.denom * 1000000000ULL) / mach_clk_info.numer;
    });
    return rvtimer_convert_freq(mach_absolute_time(), mach_clk_freq, freq);
}

#elif defined(CLOCK_REALTIME) || defined(CLOCK_MONOTONIC)
// Use POSIX clock_gettime(), with a fast monotonic clock if possible

// Use CLOCK_MONOTONIC_COARSE on Serenity for perf reasons
#if defined(CLOCK_MONOTONIC_COARSE) && defined(__serenity__)
#define CHOSEN_POSIX_CLOCK CLOCK_MONOTONIC_COARSE
// Use CLOCK_UPTIME on OpenBSD to skip suspend time
#elif defined(CLOCK_UPTIME)
#define CHOSEN_POSIX_CLOCK CLOCK_UPTIME
// Use CLOCK_MONOTONIC on Linux, FreeBSD, etc
#elif defined(CLOCK_MONOTONIC)
#define CHOSEN_POSIX_CLOCK CLOCK_MONOTONIC
#else
#define CHOSEN_POSIX_CLOCK CLOCK_REALTIME
#endif

uint64_t rvtimer_clocksource(uint64_t freq)
{
    struct timespec now = {0};
    clock_gettime(CHOSEN_POSIX_CLOCK, &now);
    return (now.tv_sec * freq) + (now.tv_nsec * freq / 1000000000ULL);
}

#else
// Use time() with no sub-second precision
#warning No OS support for precise clocksource!

uint64_t rvtimer_clocksource(uint64_t freq)
{
    return time(0) * freq;
}

#endif

void rvtimer_init(rvtimer_t* timer, uint64_t freq)
{
    timer->freq = freq;
    rvtimer_rebase(timer, 0);
}

uint64_t rvtimer_freq(const rvtimer_t* timer)
{
    return timer->freq;
}

uint64_t rvtimer_get(const rvtimer_t* timer)
{
    return rvtimer_clocksource(timer->freq) - atomic_load_uint64_ex(&timer->begin, ATOMIC_RELAXED);
}

void rvtimer_rebase(rvtimer_t* timer, uint64_t time)
{
    atomic_store_uint64(&timer->begin, rvtimer_clocksource(timer->freq) - time);
}

void rvtimecmp_init(rvtimecmp_t* cmp, rvtimer_t* timer)
{
    cmp->timer = timer;
    rvtimecmp_set(cmp, -1);
}

void rvtimecmp_set(rvtimecmp_t* cmp, uint64_t timecmp)
{
    atomic_store_uint64_ex(&cmp->timecmp, timecmp, ATOMIC_RELAXED);
}

uint64_t rvtimecmp_get(const rvtimecmp_t* cmp)
{
    return atomic_load_uint64_ex(&cmp->timecmp, ATOMIC_RELAXED);
}

bool rvtimecmp_pending(const rvtimecmp_t* cmp)
{
    return rvtimer_get(cmp->timer) >= rvtimecmp_get(cmp);
}

uint64_t rvtimecmp_delay(const rvtimecmp_t* cmp)
{
    uint64_t timer = rvtimer_get(cmp->timer);
    uint64_t timecmp = rvtimecmp_get(cmp);
    return (timer < timecmp) ? (timecmp - timer) : 0;
}

uint64_t rvtimecmp_delay_ns(const rvtimecmp_t* cmp)
{
    uint64_t delay = rvtimecmp_delay(cmp);
    if (delay > 0x400000000ULL) {
        // Approximate overflow for nanosecond conversion
        delay = 0x400000000ULL;
    }
    return delay * 1000000000ULL / rvtimer_freq(cmp->timer);
}

#if defined(_WIN32) && !defined(UNDER_CE)

static NTSTATUS (__stdcall *nt_set_timer_resolution)(ULONG, BOOLEAN, PULONG) = NULL;
static uint32_t low_latency = 0;
static rvtimer_t latency_timer = {0};

#endif

static void sleep_low_latency_once(void)
{
#if defined(_WIN32) && !defined(UNDER_CE)
    rvtimer_init(&latency_timer, 1000);
    nt_set_timer_resolution = dlib_get_symbol("ntdll.dll", "NtSetTimerResolution");
#elif defined(__linux__) && defined(PR_SET_TIMERSLACK)
    // Slacking off
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
}

void sleep_low_latency(bool enable)
{
    DO_ONCE(sleep_low_latency_once());
#if defined(_WIN32) && !defined(UNDER_CE)
    if (nt_set_timer_resolution && (enable || rvtimer_get(&latency_timer) > 100)) {
        bool was_enabled = !!atomic_swap_uint32(&low_latency, enable);
        if (enable != was_enabled) {
            ULONG cur = 0;
            nt_set_timer_resolution(enable ? 5000 : 156250, TRUE, &cur);
            if (enable) {
                rvtimer_rebase(&latency_timer, 0);
            }
        }
    }
#else
    UNUSED(enable);
#endif
}

void sleep_ms(uint32_t ms)
{
#ifdef _WIN32
    sleep_low_latency(ms < 15);
    Sleep(ms);

#elif defined(CHOSEN_POSIX_CLOCK) || defined(__APPLE__)
    if (ms) {
        struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000, };
        while (nanosleep(&ts, &ts) < 0);
        return;
    }
#if defined(SCHED_YIELD_IMPL)
    // Yield this thread time slice, as does Win32 Sleep(0)
    sched_yield();
#endif

#else
    UNUSED(ms);
    DO_ONCE(rvvm_warn("Unimplemented sleep_ms() for current platform!"));
#endif
}
