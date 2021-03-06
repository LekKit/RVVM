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

#ifndef RISCV_SPINLOCK_H
#define RISCV_SPINLOCK_H

#include <stdint.h>

#if __STDC_VERSION__ >= 201112LL
#ifndef __STDC_NO_ATOMICS__
#include <stdatomic.h>
#define C11_ATOMICS
#endif
#endif

#ifndef C11_ATOMICS

typedef int atomic_int;

#ifdef __GNUC__
#define atomic_exchange(A, C) __atomic_exchange_n(A, C, __ATOMIC_SEQ_CST)
#define atomic_store(A, C) __atomic_store_n(A, C, __ATOMIC_SEQ_CST)
#else
#warning No atomics support for current build target!
#define atomic_exchange(A, C) ((*A + C) - (*A = C))
#define atomic_store(A, C) (*A = C)
#endif

#endif

typedef struct {
    atomic_int flag;
} spinlock_t;

inline void spin_init(spinlock_t* lock)
{
    lock->flag = 0;
}

inline void spin_lock(spinlock_t* lock)
{
    while (atomic_exchange(&lock->flag, 1));
}

inline void spin_unlock(spinlock_t* lock)
{
    atomic_store(&lock->flag, 0);
}

#endif
