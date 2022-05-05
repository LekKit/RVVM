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

// Unsupported pre-win Vista
//#define WINDOWS_SRW_CONDVAR

typedef HANDLE thread_internal_t;

#ifdef WINDOWS_SRW_CONDVAR
typedef struct {
    CONDITION_VARIABLE cond;
    SRWLOCK lock;
    uint32_t flag;
} cond_var_internal_t;
#else
typedef HANDLE cond_var_internal_t;
#endif

static void __stdcall apc_membarrier(ULONG_PTR p)
{
    UNUSED(p);
    atomic_fence();
}

#else

#include <pthread.h>
#include <signal.h>

typedef pthread_t thread_internal_t;
typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t lock;
    uint32_t flag;
} cond_var_internal_t;

static void signal_membarrier(int sig)
{
    UNUSED(sig);
    atomic_fence();
}

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

void thread_signal_membarrier(thread_handle_t handle)
{
    if (handle == NULL) return;
#ifdef _WIN32
    QueueUserAPC(apc_membarrier, *(HANDLE*)handle, 0);
#else
    struct sigaction sa;
    sigaction(SIGUSR2, NULL, &sa);

    if (sa.sa_handler != signal_membarrier) {
        if (sa.sa_handler != SIG_DFL && sa.sa_handler != SIG_IGN) {
            rvvm_warn("thread_signal_membarrier() failed: SIGUSR2 is already in use!");
            return;
        } else {
            sa.sa_handler = signal_membarrier;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;
            // Still a possible race here?..
            sigaction(SIGUSR2, &sa, NULL);
        }
    }

    pthread_kill(*(pthread_t*)handle, SIGUSR2);
#endif
}

cond_var_t condvar_create()
{
    cond_var_internal_t* cond = calloc(sizeof(cond_var_internal_t), 1);
    if (cond) {
#ifdef WINDOWS_SRW_CONDVAR
        InitializeConditionVariable(&cond->cond);
        InitializeSRWLock(&cond->lock);
        cond->flag = 0;
        return cond;
#elif defined(_WIN32)
        *cond = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (*cond) return cond;
#else
        if (pthread_cond_init(&cond->cond, NULL) == 0
         && pthread_mutex_init(&cond->lock, NULL) == 0) {
             cond->flag = 0;
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
#ifdef WINDOWS_SRW_CONDVAR
    bool ret = true;
    DWORD ms = timeout_ms;
    if (timeout_ms == CONDVAR_INFINITE) ms = INFINITE;
    AcquireSRWLockShared(&cond_p->lock);
    if (atomic_load_uint32(&cond_p->flag) == 0) {
        ret = SleepConditionVariableSRW(&cond_p->cond, &cond_p->lock, ms, CONDITION_VARIABLE_LOCKMODE_SHARED);
    }
    atomic_store_uint32(&cond_p->flag, 0);
    ReleaseSRWLockShared(&cond_p->lock);
    return ret;
#elif defined(_WIN32)
    DWORD ms = timeout_ms;
    if (timeout_ms == CONDVAR_INFINITE) ms = INFINITE;
    return WaitForSingleObject(*cond_p, ms) == WAIT_OBJECT_0;
#else
    bool ret = true;
    pthread_mutex_lock(&cond_p->lock);
    if (atomic_load_uint32(&cond_p->flag) == 0) {
        if (timeout_ms == CONDVAR_INFINITE) {
            ret = pthread_cond_wait(&cond_p->cond, &cond_p->lock) == 0;
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += timeout_ms * 1000000;
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
            ret = pthread_cond_timedwait(&cond_p->cond, &cond_p->lock, &ts) == 0;
        }
    }
    atomic_store_uint32(&cond_p->flag, 0);
    pthread_mutex_unlock(&cond_p->lock);
    return ret;
#endif
}

void condvar_wake(cond_var_t cond)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return;
#ifdef WINDOWS_SRW_CONDVAR
    AcquireSRWLockExclusive(&cond_p->lock);
    atomic_store_uint32(&cond_p->flag, 1);
    ReleaseSRWLockExclusive(&cond_p->lock);
    WakeConditionVariable(&cond_p->cond);
#elif defined(_WIN32)
    SetEvent(*cond_p);
#else
    pthread_mutex_lock(&cond_p->lock);
    atomic_store_uint32(&cond_p->flag, 1);
    pthread_cond_signal(&cond_p->cond);
    pthread_mutex_unlock(&cond_p->lock);
#endif
}

void condvar_wake_all(cond_var_t cond)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return;
#ifdef WINDOWS_SRW_CONDVAR
    AcquireSRWLockExclusive(&cond_p->lock);
    atomic_store_uint32(&cond_p->flag, 1);
    ReleaseSRWLockExclusive(&cond_p->lock);
    WakeAllConditionVariable(&cond_p->cond);
#elif defined(_WIN32)
    SetEvent(*cond_p);
#else
    pthread_mutex_lock(&cond_p->lock);
    atomic_store_uint32(&cond_p->flag, 1);
    pthread_cond_broadcast(&cond_p->cond);
    pthread_mutex_unlock(&cond_p->lock);
#endif
}

void condvar_free(cond_var_t cond)
{
    cond_var_internal_t* cond_p = (cond_var_internal_t*)cond;
    if (!cond) return;
    condvar_wake_all(cond);
#ifdef WINDOWS_SRW_CONDVAR
    UNUSED(cond_p);
#elif defined(_WIN32)
    CloseHandle(*cond_p);
#else
    pthread_cond_destroy(&cond_p->cond);
    pthread_mutex_destroy(&cond_p->lock);
#endif
    free(cond);
}

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
    while (condvar_wait(thread_ctx->cond, THREAD_MAX_WORKER_IDLE_MS) || busy) {
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

static bool thread_queue_task(thread_func_t func, void* arg, unsigned arg_count)
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
            if (arg_count) {
                if (arg_count > THREAD_MAX_VA_ARGS) return false;
                memcpy(threadpool[i].arg, arg, sizeof(void*) * arg_count);
            } else {
                threadpool[i].arg[0] = arg;
            }
            threadpool[i].func_va = !!arg_count;
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
    if (!thread_queue_task(func, arg, 0)) {
        func(arg);
    }
}

void thread_create_task_va(thread_func_va_t func, void** args, unsigned arg_count)
{
    if (arg_count == 0 || !thread_queue_task((thread_func_t)(void*)func, args, arg_count)) {
        func(args);
    }
}
