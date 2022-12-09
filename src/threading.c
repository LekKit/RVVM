/*
threading.c - Threading, Conditional variables, Task offloading
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

#include "threading.h"
#include "atomics.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

typedef HANDLE thread_internal_t;
typedef struct {
    HANDLE event;
    uint32_t flag;
} cond_var_internal_t;

#else

#include <time.h>
#if !defined(CLOCK_MONOTONIC) || defined(__APPLE__)
#include <sys/time.h>
#endif
#include <pthread.h>

typedef pthread_t thread_internal_t;
typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t lock;
    uint32_t flag;
} cond_var_internal_t;

#endif

thread_handle_t thread_create(thread_func_t func_name, void *arg)
{
    thread_handle_t handle = calloc(sizeof(thread_internal_t), 1);
    if (handle) {
#ifdef _WIN32
        *(HANDLE*)handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(const void*)func_name, arg, 0, NULL);
        if (*(HANDLE*)handle) {
            return handle;
        }
#else
        if (pthread_create((pthread_t*)handle, NULL, func_name, arg) == 0) {
            return handle;
        }
#endif
    }
    rvvm_warn("Failed to spawn thread!");
    free(handle);
    return NULL;
}

void* thread_join(thread_handle_t handle)
{
    if (handle == NULL) return NULL;
    void* ret;
#ifdef _WIN32
    DWORD ltmp;
    WaitForSingleObject(*(HANDLE*)handle, INFINITE);
    GetExitCodeThread(*(HANDLE*)handle, &ltmp);
    ret = (void*)(size_t)ltmp;
#else
    pthread_join(*(pthread_t*)handle, &ret);
#endif
    free(handle);
    return ret;
}

bool thread_detach(thread_handle_t handle)
{
    if (handle == NULL) return false;
    bool ret;
#ifdef _WIN32
    ret = CloseHandle(*(HANDLE*)handle);
#else
    ret = pthread_detach(*(pthread_t*)handle) == 0;
#endif
    free(handle);
    return ret;
}

cond_var_t condvar_create()
{
    cond_var_internal_t* cond = calloc(sizeof(cond_var_internal_t), 1);
    if (cond) {
        atomic_store_uint32(&cond->flag, 0);
#ifdef _WIN32
        cond->event = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (cond->event) return cond;
#elif defined(CLOCK_MONOTONIC) && !defined(__APPLE__)
        pthread_condattr_t cond_attr;
        pthread_condattr_init(&cond_attr);
        if (pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) == 0
         && pthread_cond_init(&cond->cond, &cond_attr)  == 0
         && pthread_mutex_init(&cond->lock, NULL) == 0) {
            pthread_condattr_destroy(&cond_attr);
            return cond;
        }
        pthread_condattr_destroy(&cond_attr);
#else
        if (pthread_cond_init(&cond->cond, NULL)  == 0
         && pthread_mutex_init(&cond->lock, NULL) == 0) {
            return cond;
        }
#endif
    }
    rvvm_warn("Failed to create conditional variable!");
    free(cond);
    return NULL;
}

bool condvar_wait(cond_var_t cond, unsigned timeout_ms)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return false;
    if (atomic_swap_uint32(&cond_p->flag, 0)) return true;
#ifdef _WIN32
    DWORD ms = (timeout_ms == CONDVAR_INFINITE) ? INFINITE : timeout_ms;
    return WaitForSingleObject(cond_p->event, ms) == WAIT_OBJECT_0;
#else
    bool ret = true;
    pthread_mutex_lock(&cond_p->lock);
    if (timeout_ms == CONDVAR_INFINITE) {
        ret = pthread_cond_wait(&cond_p->cond, &cond_p->lock) == 0;
    } else {
        struct timespec ts = {0};
#if defined(CLOCK_MONOTONIC) && !defined(__APPLE__)
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += timeout_ms * 1000000;
        ts.tv_sec  += ts.tv_nsec / 1000000000;
#else
        struct timeval tv = {0};
        gettimeofday(&tv, NULL);
        ts.tv_nsec = (tv.tv_usec * 1000) + (timeout_ms * 1000000);
        ts.tv_sec  = tv.tv_sec + (ts.tv_nsec / 1000000000);
#endif
        ts.tv_nsec %= 1000000000;
        ret = pthread_cond_timedwait(&cond_p->cond, &cond_p->lock, &ts) == 0;
    }
    pthread_mutex_unlock(&cond_p->lock);
    return ret;
#endif
}

void condvar_wake(cond_var_t cond)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return;
    atomic_store_uint32(&cond_p->flag, 1);
#ifdef _WIN32
    SetEvent(cond_p->event);
#else
    pthread_mutex_lock(&cond_p->lock);
    pthread_cond_signal(&cond_p->cond);
    pthread_mutex_unlock(&cond_p->lock);
#endif
}

void condvar_wake_all(cond_var_t cond)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return;
    atomic_store_uint32(&cond_p->flag, 1);
#ifdef _WIN32
    SetEvent(cond_p->event);
#else
    pthread_mutex_lock(&cond_p->lock);
    pthread_cond_broadcast(&cond_p->cond);
    pthread_mutex_unlock(&cond_p->lock);
#endif
}

void condvar_free(cond_var_t cond)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return;
    condvar_wake_all(cond);
#ifdef _WIN32
    CloseHandle(cond_p->event);
#else
    pthread_cond_destroy(&cond_p->cond);
    pthread_mutex_destroy(&cond_p->lock);
#endif
    free(cond);
}

// Threadpool task offloading

#define THREAD_MAX_WORKERS 4

#if (defined(_WIN32) && defined(UNDER_CE)) || defined(__EMSCRIPTEN__)
#define THREAD_MAX_WORKER_IDLE CONDVAR_INFINITE
#else
#define THREAD_MAX_WORKER_IDLE 5000
#endif

typedef struct {
    uint32_t busy;
    thread_handle_t thread;
    cond_var_t cond;
    thread_func_t func;
    void* arg[THREAD_MAX_VA_ARGS];
    bool func_va;

} threadpool_entry_t;

static threadpool_entry_t threadpool[THREAD_MAX_WORKERS];

static void* threadpool_worker(void* data)
{
    threadpool_entry_t* thread_ctx = (threadpool_entry_t*)data;
    //rvvm_info("Spawned new threadpool worker %p", data);

    bool busy = true;
    while (condvar_wait(thread_ctx->cond, THREAD_MAX_WORKER_IDLE) || busy) {
        busy = atomic_load_uint32(&thread_ctx->busy);
        if (busy && thread_ctx->func) {
            //rvvm_info("Threadpool worker %p woke up", data);
            if (thread_ctx->func_va) {
                ((thread_func_va_t)(void*)thread_ctx->func)((void**)thread_ctx->arg);
            } else {
                thread_ctx->func(thread_ctx->arg[0]);
            }
            thread_ctx->func = NULL;
            atomic_store_uint32(&thread_ctx->busy, 0);
        }
    }

    condvar_free(thread_ctx->cond);
    thread_detach(thread_ctx->thread);
    thread_ctx->func = NULL;
    thread_ctx->cond = NULL;
    thread_ctx->thread = NULL;
    atomic_store_uint32(&thread_ctx->busy, 0);
    //rvvm_info("Threadpool worker %p exiting upon timeout", data);
    return data;
}

static void thread_workers_terminate()
{
    for (size_t i=0; i<THREAD_MAX_WORKERS; ++i) {
        if (atomic_swap_uint32(&threadpool[i].busy, 0)) {
            condvar_wake(threadpool[i].cond);
        }
    }
}

static bool thread_queue_task(thread_func_t func, void** arg, unsigned arg_count, bool va)
{
    static bool exit_init = false;
    if (!exit_init) {
        exit_init = true;
        atexit(thread_workers_terminate);
    }

    for (size_t i=0; i<THREAD_MAX_WORKERS; ++i) {
        if (!atomic_swap_uint32(&threadpool[i].busy, 1)) {
            //rvvm_info("Threadpool worker %p notified", &threadpool[i]);
            threadpool[i].func = func;
            threadpool[i].func_va = va;
            for (size_t j=0; j<arg_count; ++j) threadpool[i].arg[j] = arg[j];
            if (!threadpool[i].thread) {
                threadpool[i].cond = condvar_create();
                threadpool[i].thread = thread_create(threadpool_worker, &threadpool[i]);
            }
            condvar_wake(threadpool[i].cond);
            return true;
        }
    }

    // Still not queued!
    // Assuming entire threadpool is busy, just do a blocking task
    //rvvm_warn("Blocking on workqueue task %p", func);
    return false;
}

void thread_create_task(thread_func_t func, void* arg)
{
    if (!thread_queue_task(func, &arg, 1, false)) {
        func(arg);
    }
}

void thread_create_task_va(thread_func_va_t func, void** args, unsigned arg_count)
{
    if (arg_count == 0 || arg_count > THREAD_MAX_VA_ARGS) {
        rvvm_warn("Invalid arg count in thread_create_task_va()!");
        return;
    }
    if (!thread_queue_task((thread_func_t)(void*)func, args, arg_count, true)) {
        func(args);
    }
}
