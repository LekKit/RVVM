/*
spinlock.c - Atomic spinlock
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

#include "spinlock.h"
#include "rvtimer.h"
#include "utils.h"

// Maximum allowed lock time, warns and recovers the lock upon expiration
#define SPINLOCK_MAX_MS 1000

// Attemts to claim the lock before sleep throttle
#define SPINLOCK_RETRIES 10000

NOINLINE void spin_lock_slow(spinlock_t* lock, const char* info)
{
    for (size_t ms=0; ms<SPINLOCK_MAX_MS; ++ms) {
        for (size_t i=0; i<SPINLOCK_RETRIES; ++i) {
            if (atomic_swap_uint32(&lock->flag, 1) == 0) return;
        }
        sleep_ms(1);
    }
    rvvm_warn("Possible deadlock at %s", info);
#ifdef USE_SPINLOCK_DEBUG
    rvvm_warn("The lock was previously held at %s", lock->lock_info);
#endif
    rvvm_warn("Attempting to recover execution...\n * * * * * * *\n");
}
