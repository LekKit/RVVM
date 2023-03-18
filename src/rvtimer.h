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

#ifndef RVTIMER_H
#define RVTIMER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t begin; // Internal usage only
    uint64_t freq;
    uint64_t timecmp;
} rvtimer_t;

// Convert between frequencies without overflow
static inline uint64_t rvtimer_convert_freq(uint64_t clk, uint64_t src_freq, uint64_t dst_freq)
{
    uint64_t freq_rem = clk % src_freq;
    return (clk / src_freq * dst_freq) + (freq_rem * dst_freq / src_freq);
}

// Get global clocksource with the specified frequency
uint64_t rvtimer_clocksource(uint64_t freq);

// Initialize the timer and the clocksource
void rvtimer_init(rvtimer_t* timer, uint64_t freq);

// Get current timer value
uint64_t rvtimer_get(rvtimer_t* timer);

// Rebase the clocksource by time field
void rvtimer_rebase(rvtimer_t* timer, uint64_t time);

// Check if we have a pending timer interrupt. Updates on it's own
bool rvtimer_pending(rvtimer_t* timer);

void sleep_ms(uint32_t ms);

#endif
