/*
rvtimer.h - Timers, sleep functions
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

#ifndef RVVM_RVTIMER_H
#define RVVM_RVTIMER_H

#include "compiler.h"
#include "rvvm_types.h"

typedef struct {
    // Those fields are internal use only
    uint64_t begin;
    uint64_t freq;
} rvtimer_t;

typedef struct {
    // Those fields are internal use only
    uint64_t timecmp;
    rvtimer_t* timer;
} rvtimecmp_t;

// Convert between frequencies without overflow
static inline uint64_t rvtimer_convert_freq(uint64_t clk, uint64_t src_freq, uint64_t dst_freq)
{
#ifdef INT128_SUPPORT
    // Fast path when no overflow (Just mul + div on x86_64 with the overflow check)
    if (likely(!((uint128_t)clk * (uint128_t)dst_freq >> 64))) {
        return clk * dst_freq / src_freq;
    }
#endif
    uint64_t freq_rem = clk % src_freq;
    return (clk / src_freq * dst_freq) + (freq_rem * dst_freq / src_freq);
}

// Get global clocksource with the specified frequency
uint64_t rvtimer_clocksource(uint64_t freq);

/*
 * Timer
 */

// Initialize the timer and the clocksource
void rvtimer_init(rvtimer_t* timer, uint64_t freq);

// Get timer frequency
uint64_t rvtimer_freq(const rvtimer_t* timer);

// Get current timer value
uint64_t rvtimer_get(const rvtimer_t* timer);

// Rebase the clocksource by time field
void rvtimer_rebase(rvtimer_t* timer, uint64_t time);

/*
 * Timer comparators
 */

// Init timer comparator
void rvtimecmp_init(rvtimecmp_t* cmp, rvtimer_t* timer);

// Set comparator timestamp
void rvtimecmp_set(rvtimecmp_t* cmp, uint64_t timecmp);

// Get comparator timestamp
uint64_t rvtimecmp_get(const rvtimecmp_t* cmp);

// Check if we have a pending timer interrupt. Updates on it's own
bool rvtimecmp_pending(const rvtimecmp_t* cmp);

// Get delay until the timer interrupt (In timer frequency)
uint64_t rvtimecmp_delay(const rvtimecmp_t* cmp);

// Get delay until the timer interrupt (In nanoseconds)
uint64_t rvtimecmp_delay_ns(const rvtimecmp_t* cmp);

/*
 * Sleep
 */

// Set expected sleep latency (Internal use)
void sleep_low_latency(bool enable);

// Sleep for N ms
void sleep_ms(uint32_t ms);

#endif
