/*
threading.h - Threads
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

#ifndef THREADING_H
#define THREADING_H

typedef void* thread_handle_t;
typedef void* (*thread_func_t)(void*);

thread_handle_t thread_create(thread_func_t func, void *arg);
void* thread_join(thread_handle_t handle);

// Use with care, only for hanged threads
void thread_kill(thread_handle_t handle);

// Deliver a signal with total memory ordering to the thread
// Also immediately interrupts any sleep in a target thread
void thread_signal_membarrier(thread_handle_t handle);

#endif
