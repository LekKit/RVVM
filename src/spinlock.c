/*
spinlock.c - Hybrid Spinlock
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
#include "rvtimer.h"
#include "utils.h"

// Maximum allowed lock time, warns and recovers the lock upon expiration
#define SPINLOCK_MAX_MS 5000

// Attemts to claim the lock before blocking in the kernel
#define SPINLOCK_RETRIES 60

static cond_var_t* global_cond;

static void spin_atexit()
{
    cond_var_t* cond = global_cond;
    global_cond = NULL;
    // Make sure no use-after-free happens on running threads
    atomic_fence();
    condvar_free(cond);
}

static void spin_cond_init()
{
    DO_ONCE ({
        global_cond = condvar_create();
        atexit(spin_atexit);
    });
}

NOINLINE void spin_lock_wait(spinlock_t* lock, const char* location)
{
    for (size_t i=0; i<SPINLOCK_RETRIES; ++i) {
        // Read lock flag until there's any chance to grab it
        // Improves performance due to cacheline bouncing elimination
        if (atomic_load_uint32_ex(&lock->flag, ATOMIC_ACQUIRE) == 0) {
            if (spin_try_lock_real(lock, location)) return;
        }
    }

    spin_cond_init();

    rvtimer_t timer;
    rvtimer_init(&timer, 1000);
    do {
        uint32_t flag = atomic_load_uint32_ex(&lock->flag, ATOMIC_ACQUIRE);
        if (flag == 0 && spin_try_lock_real(lock, location)) {
            // Succesfully grabbed the lock
            return;
        }
        // Someone else grabbed the lock, indicate that we are still waiting
        if (flag != 2 && !atomic_cas_uint32(&lock->flag, 1, 2)) {
            // Failed to indicate lock as waiting, retry grabbing
            continue;
        }
        // Wait upon wakeup from lock owner
        bool woken = condvar_wait(global_cond, 10);
        if (woken || flag != 2) {
            // Reset deadlock timer upon noticing any forward progress
            rvtimer_init(&timer, 1000);
        }
    } while (location == NULL || rvtimer_get(&timer) < SPINLOCK_MAX_MS);

    rvvm_warn("Possible deadlock at %s", location);
#ifdef USE_SPINLOCK_DEBUG
    rvvm_warn("The lock was previously held at %s", lock->location ? lock->location : "[nowhere?]");
#endif
#ifdef RVVM_VERSION
    rvvm_warn("Version: RVVM v"RVVM_VERSION);
#endif
    rvvm_warn("Attempting to recover execution...\n * * * * * * *\n");
}

NOINLINE void spin_lock_wake(spinlock_t* lock)
{
    UNUSED(lock);
    spin_cond_init();
    condvar_wake_all(global_cond);
}
