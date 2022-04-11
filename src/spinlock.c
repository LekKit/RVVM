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
#include "threading.h"
#include "utils.h"
#include "rvtimer.h"

// Maximum allowed lock time, warns and recovers the lock upon expiration
#define SPINLOCK_MAX_MS 1000
#define SPINLOCK_MAX_SLEEP 1

// Attemts to claim the lock before sleep throttle
#define SPINLOCK_RETRIES 100

// It might use Linux futex() at some point,
// but let's just use a condvar for now
static cond_var_t global_cond;
static uint32_t global_cond_init = 0;

static void spin_atexit()
{
    cond_var_t cond = global_cond;
    global_cond = NULL;
    condvar_free(cond);
}

NOINLINE void spin_lock_wait(spinlock_t* lock, const char* info, bool infinite)
{
    for (size_t i=0; i<SPINLOCK_RETRIES; ++i) {
        if (atomic_swap_uint32(&lock->flag, 2) == 0) {
            atomic_store_uint32(&lock->flag, 1);
            return;
        }
    }

    if (!atomic_swap_uint32(&global_cond_init, 1)) {
        global_cond = condvar_create();
        atexit(spin_atexit);
    }

    rvtimer_t timer;
    rvtimer_init(&timer, 1000);
    do {
        condvar_wait(global_cond, SPINLOCK_MAX_SLEEP);
        if (atomic_swap_uint32(&lock->flag, 2) == 0) {
            atomic_store_uint32(&lock->flag, 1);
            return;
        }
    } while (infinite || rvtimer_get(&timer) < SPINLOCK_MAX_MS);

    rvvm_warn("Possible deadlock at %s", info);
#ifdef USE_SPINLOCK_DEBUG
    rvvm_warn("The lock was previously held at %s", lock->lock_info);
#endif
    rvvm_warn("Attempting to recover execution...\n * * * * * * *\n");
}

NOINLINE void spin_lock_wake(spinlock_t* lock)
{
    UNUSED(lock);
    condvar_wake_all(global_cond);
}
