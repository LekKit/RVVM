/*
spinlock.h - Atomic spinlock
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

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "atomics.h"

#define SPINLOCK_DBG_DEF_INFO "[not locked(?)]"

typedef struct {
    uint32_t flag;
#ifdef USE_SPINLOCK_DEBUG
    const char* lock_info;
#endif
} spinlock_t;

NOINLINE void spin_lock_wait(spinlock_t* lock, const char* info, bool infinite);
NOINLINE void spin_lock_wake(spinlock_t* lock);

// For static initialization
#ifdef USE_SPINLOCK_DEBUG
#define SPINLOCK_INIT {0, SPINLOCK_DBG_DEF_INFO}
#else
#define SPINLOCK_INIT {0}
#endif

// Initialize a lock
static inline void spin_init(spinlock_t* lock)
{
    lock->flag = 0;
#ifdef USE_SPINLOCK_DEBUG
    lock->lock_info = SPINLOCK_DBG_DEF_INFO;
#endif
}

// Perform locking on small critical section
// Reports a deadlock upon waiting for too long
static inline void _spin_lock(spinlock_t* lock, const char* info)
{
    if (atomic_add_uint32(&lock->flag, 1)) {
        spin_lock_wait(lock, info, false);
    }
#ifdef USE_SPINLOCK_DEBUG
    lock->lock_info = info;
#endif
}

// Perform locking around heavy operation, wait indefinitely
static inline void _spin_lock_slow(spinlock_t* lock, const char* info)
{
    if (atomic_add_uint32(&lock->flag, 1)) {
        spin_lock_wait(lock, info, true);
    }
#ifdef USE_SPINLOCK_DEBUG
    lock->lock_info = info;
#endif
}

#ifdef USE_SPINLOCK_DEBUG
#define spin_lock(lock) _spin_lock(lock, SOURCE_LINE)
#define spin_lock_slow(lock) _spin_lock_slow(lock, SOURCE_LINE)
#else
#define spin_lock(lock) _spin_lock(lock, "[no debug]")
#define spin_lock_slow(lock) _spin_lock_slow(lock, "[no debug]")
#endif

// Try to claim the lock, returns true on success
static inline bool spin_try_lock(spinlock_t* lock)
{
    return !atomic_add_uint32(&lock->flag, 1);
}

// Release the lock
static inline void spin_unlock(spinlock_t* lock)
{
    if (atomic_swap_uint32(&lock->flag, 0) > 1) {
        spin_lock_wake(lock);
    }
}

#endif
