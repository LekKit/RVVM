/*
threading.h - Threading, Conditional variables, Task offloading
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

#include <stdbool.h>

#define THREAD_MAX_WORKERS 4
#define THREAD_MAX_VA_ARGS 8
#define THREAD_MAX_WORKER_IDLE_MS 5000

typedef void* thread_handle_t;
typedef void* (*thread_func_t)(void*);
typedef void* (*thread_func_va_t)(void**);
typedef void* cond_var_t;

thread_handle_t thread_create(thread_func_t func, void* arg);
void* thread_join(thread_handle_t handle);
bool thread_detach(thread_handle_t handle);

// Deliver a signal with total memory ordering to the thread
// Also immediately interrupts any sleep in a target thread
void thread_signal_membarrier(thread_handle_t handle);

#define CONDVAR_INFINITE ((unsigned)-1)

cond_var_t condvar_create();
bool condvar_wait(cond_var_t cond, unsigned timeout_ms);
void condvar_wake(cond_var_t cond);
void condvar_wake_all(cond_var_t cond);
void condvar_free(cond_var_t cond);

// Execute task in threadpool
void thread_create_task(thread_func_t func, void* arg);
void thread_create_task_va(thread_func_va_t func, void** args, unsigned arg_count);

#endif
