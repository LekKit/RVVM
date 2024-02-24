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

// Needed for pthread_condattr_setclock() when not passing -std=gnu..
#define _POSIX_C_SOURCE 200112L

#include "threading.h"
#include "atomics.h"
#include "rvtimer.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

#define COND_FLAG_SIGNALED 0x1
#define COND_FLAG_LOCKED   0x2

#ifdef _WIN32
#include <windows.h>

#else
#include <time.h>
#if !defined(CLOCK_MONOTONIC) || defined(__APPLE__)
#include <sys/time.h>
#endif
#include <pthread.h>

static void condvar_fill_timespec(struct timespec* ts)
{
#if defined(CLOCK_MONOTONIC) && !defined(__APPLE__)
    clock_gettime(CLOCK_MONOTONIC, ts);
#else
    // Some targets lack clock_gettime(), use gettimeofday()
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    ts->tv_sec  = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
#endif
}

#endif

struct thread_ctx {
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t pthread;
#endif
};

struct cond_var {
    uint32_t flag;
    uint32_t waiters;
#ifdef _WIN32
    HANDLE event;
    HANDLE timer;
#else
    pthread_cond_t cond;
    pthread_mutex_t lock;
#endif
};

#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
// Wrap our function call to hide calling convention details
typedef struct { thread_func_t func; void* arg; } thread_win32_wrap_t;
static __stdcall DWORD thread_win32_wrap(void* arg)
{
    thread_win32_wrap_t wrap = *(thread_win32_wrap_t*)arg;
    free(arg);
    return (DWORD)(size_t)wrap.func(wrap.arg);
}
#endif

thread_ctx_t* thread_create(thread_func_t func, void *arg)
{
    thread_ctx_t* thread = safe_new_obj(thread_ctx_t);
#ifdef _WIN32
#if defined(__i386__) || defined(_M_IX86)
    thread_win32_wrap_t* wrap = safe_new_obj(thread_win32_wrap_t);
    wrap->func = func;
    wrap->arg = arg;
    thread->handle = CreateThread(NULL, 0, thread_win32_wrap, wrap, 0, NULL);
#else
    thread->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void*)func, arg, 0, NULL);
#endif
    if (thread->handle) return thread;
#else
    if (pthread_create(&thread->pthread, NULL, func, arg) == 0) {
        return thread;
    }
#endif
    rvvm_warn("Failed to spawn thread!");
    free(thread);
    return NULL;
}

void* thread_join(thread_ctx_t* thread)
{
    void* ret = NULL;
    if (thread == NULL) return NULL;
#ifdef _WIN32
    DWORD ltmp = 0;
    WaitForSingleObject(thread->handle, INFINITE);
    GetExitCodeThread(thread->handle, &ltmp);
    ret = (void*)(size_t)ltmp;
#else
    pthread_join(thread->pthread, &ret);
#endif
    free(thread);
    return ret;
}

bool thread_detach(thread_ctx_t* thread)
{
    bool ret = false;
    if (thread == NULL) return false;
#ifdef _WIN32
    ret = CloseHandle(thread->handle);
#else
    ret = pthread_detach(thread->pthread) == 0;
#endif
    free(thread);
    return ret;
}

cond_var_t* condvar_create()
{
    cond_var_t* cond = safe_new_obj(cond_var_t);
#ifdef _WIN32
#ifndef UNDER_CE
    static HANDLE (__stdcall *create_WTExW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD) = NULL;
    static NTSTATUS (__stdcall *nt_setTR)(ULONG, BOOLEAN, PULONG) = NULL;
    DO_ONCE ({
        create_WTExW = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW");
        nt_setTR = (void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution");
    });
    if (nt_setTR) {
        // Set system clock resolution to 500us
        ULONG cur;
        nt_setTR(5000, TRUE, &cur);
        nt_setTR = NULL;
    }
    if (create_WTExW) {
        // Create a high resolution, manual reset waitable timer (Win10 1803+)
        cond->timer = create_WTExW(NULL, NULL, 0x3, TIMER_ALL_ACCESS);
        if (cond->timer == NULL) create_WTExW = NULL;
    }
    if (cond->timer == NULL) {
        // Fallback to generic waitable timer
        cond->timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    }
#endif
    cond->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (cond->event) return cond;
#elif defined(CLOCK_MONOTONIC) && !defined(__APPLE__)
    pthread_condattr_t cond_attr;
    if (pthread_condattr_init(&cond_attr) == 0
     && pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) == 0
     && pthread_cond_init(&cond->cond, &cond_attr)  == 0
     && pthread_mutex_init(&cond->lock, NULL) == 0) {
        pthread_condattr_destroy(&cond_attr);
        return cond;
    }
#else
    if (pthread_cond_init(&cond->cond, NULL)  == 0
     && pthread_mutex_init(&cond->lock, NULL) == 0) {
        return cond;
    }
#endif
    rvvm_warn("Failed to create conditional variable!");
    condvar_free(cond);
    return NULL;
}

bool condvar_wait(cond_var_t* cond, uint64_t timeout_ms)
{
    uint64_t timeout_ns = CONDVAR_INFINITE;
    if (timeout_ms != CONDVAR_INFINITE) timeout_ns = timeout_ms * 1000000;
    return condvar_wait_ns(cond, timeout_ns);
}

bool condvar_wait_ns(cond_var_t* cond, uint64_t timeout_ns)
{
    if (!cond) return false;
    bool ret = false;
    uint32_t flag = 0;
    // Mark that a thread is waiting here first of all, otherwise wake may set signal
    // too late be consumed, but not see any waiters and so a wakeup event may be lost.
    uint32_t waiters = atomic_add_uint32(&cond->waiters, 1);
    // Sometimes it's also useful to know there are other waiters
    UNUSED(waiters);
    do {
        // Try consuming a signal in userspace, and spin here if locked
        flag = atomic_and_uint32(&cond->flag, ~COND_FLAG_SIGNALED);
    } while (flag & COND_FLAG_LOCKED);
    // Check if the condition is already signaled or timeout is zero
    if ((flag & COND_FLAG_SIGNALED) || !timeout_ns) {
        atomic_sub_uint32(&cond->waiters, 1);
        return !!(flag & COND_FLAG_SIGNALED);
    }
#ifdef _WIN32
    if (timeout_ns == CONDVAR_INFINITE) {
        ret = WaitForSingleObject(cond->event, INFINITE) == WAIT_OBJECT_0;
#ifndef UNDER_CE
    } else if (timeout_ns < 15000000 && cond->timer && !waiters) {
        // Nanosecond precision timeout using WaitableTimer
        LARGE_INTEGER delay = { .QuadPart = -(timeout_ns / 100ULL), };
        HANDLE handles[2] = { cond->event, cond->timer };
        SetWaitableTimer(handles[1], &delay, 0, NULL, NULL, false);
        ret = WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0;
#endif
    } else {
        // Coarse ms precision timeout
        ret = WaitForSingleObject(cond->event, EVAL_MAX(timeout_ns / 1000000, 1)) == WAIT_OBJECT_0;
    }
#else
    pthread_mutex_lock(&cond->lock);
    if (!(atomic_and_uint32(&cond->flag, ~COND_FLAG_SIGNALED) & COND_FLAG_SIGNALED)) {
        if (timeout_ns == CONDVAR_INFINITE) {
            ret = pthread_cond_wait(&cond->cond, &cond->lock) == 0;
        } else {
#if defined(__APPLE__) || defined(HAVE_PTHREAD_COND_TIMEDWAIT_RELATIVE)
            struct timespec ts = { .tv_sec = timeout_ns / 1000000000, .tv_nsec = timeout_ns % 1000000000, };
            UNUSED(condvar_fill_timespec);
            ret = pthread_cond_timedwait_relative_np(&cond->cond, &cond->lock, &ts) == 0;
#else
            struct timespec ts = {0};
            condvar_fill_timespec(&ts);
            // Properly handle timespec addition without an overflow
            timeout_ns += ts.tv_nsec;
            ts.tv_sec += timeout_ns / 1000000000;
            ts.tv_nsec = timeout_ns % 1000000000;
            ret = pthread_cond_timedwait(&cond->cond, &cond->lock, &ts) == 0;
#endif
        }
    }
    pthread_mutex_unlock(&cond->lock);
#endif
    atomic_and_uint32(&cond->flag, ~COND_FLAG_SIGNALED);
    atomic_sub_uint32(&cond->waiters, 1);
    return ret;
}

bool condvar_wake(cond_var_t* cond)
{
    if (!cond) return false;
    // Signal the condition
    atomic_or_uint32(&cond->flag, COND_FLAG_SIGNALED);
    // Omit syscall if there are no waiters
    if (!atomic_load_uint32(&cond->waiters)) return false;
#ifdef _WIN32
    SetEvent(cond->event);
#else
    pthread_mutex_lock(&cond->lock);
    pthread_mutex_unlock(&cond->lock);
    // We aren't required to signal under the lock, but it should be taken anyways
    // to prevent lost wakeup between cond->flag check and waiting on pthread_cond
    pthread_cond_signal(&cond->cond);
#endif
    return true;
}

bool condvar_wake_all(cond_var_t* cond)
{
    if (!cond) return false;
#ifdef _WIN32
    // Wake old waiters, lock others
    atomic_or_uint32(&cond->flag, COND_FLAG_LOCKED | COND_FLAG_SIGNALED);
    for (uint32_t i=atomic_load_uint32(&cond->waiters); i--;) SetEvent(cond->event);
    atomic_and_uint32(&cond->flag, ~COND_FLAG_LOCKED);
#else
    atomic_or_uint32(&cond->flag, COND_FLAG_SIGNALED);
    if (!atomic_load_uint32(&cond->waiters)) return false;
    pthread_mutex_lock(&cond->lock);
    pthread_mutex_unlock(&cond->lock);
    pthread_cond_broadcast(&cond->cond);
#endif
    return true;
}

uint32_t condvar_waiters(cond_var_t* cond)
{
    if (!cond) return false;
    return atomic_load_uint32(&cond->waiters);
}

void condvar_free(cond_var_t* cond)
{
    if (!cond) return;
    uint32_t waiters = condvar_waiters(cond);
    if (waiters) rvvm_warn("Destroying a condvar with %u waiters!", waiters);
#ifdef _WIN32
    if (cond->event) CloseHandle(cond->event);
    if (cond->timer) CloseHandle(cond->timer);
#else
    pthread_cond_destroy(&cond->cond);
    pthread_mutex_destroy(&cond->lock);
#endif
    free(cond);
}

// Threadpool task offloading

#define WORKER_THREADS 4
#define WORKQUEUE_SIZE 2048
#define WORKQUEUE_MASK (WORKQUEUE_SIZE - 1)

BUILD_ASSERT(!(WORKQUEUE_SIZE & WORKQUEUE_MASK));

typedef struct {
    uint32_t seq;
    uint32_t flags;
    thread_func_t func;
    void* arg[THREAD_MAX_VA_ARGS];
} task_item_t;

typedef struct {
    task_item_t tasks[WORKQUEUE_SIZE];
    char pad0[64];
    uint32_t head;
    char pad1[64];
    uint32_t tail;
    char pad2[64];
} work_queue_t;

static uint32_t      pool_run;
static work_queue_t  pool_wq;
static cond_var_t*   pool_cond;
static thread_ctx_t* pool_threads[WORKER_THREADS];

static void workqueue_init(work_queue_t* wq)
{
    for (size_t seq = 0; seq < WORKQUEUE_SIZE; ++seq) {
        atomic_store_uint32(&wq->tasks[seq].seq, seq);
    }
}

static bool workqueue_try_perform(work_queue_t* wq)
{
    uint32_t tail = atomic_load_uint32_ex(&wq->tail, ATOMIC_RELAXED);
    while (true) {
        task_item_t* task_ptr = &wq->tasks[tail & WORKQUEUE_MASK];
        uint32_t seq = atomic_load_uint32_ex(&task_ptr->seq, ATOMIC_ACQUIRE);
        int32_t diff = (int32_t)seq - (int32_t)(tail + 1);
        if (diff == 0) {
            // This is a filled task slot
            if (atomic_cas_uint32_ex(&wq->tail, tail, tail + 1, true, ATOMIC_RELAXED, ATOMIC_RELAXED)) {
                // We claimed the slot
                task_item_t task = wq->tasks[tail & WORKQUEUE_MASK];

                // Mark task slot as reusable
                atomic_store_uint32_ex(&task_ptr->seq, tail + WORKQUEUE_MASK + 1, ATOMIC_RELEASE);

                // Run the task
                if (task.flags & 2) {
                    ((thread_func_va_t)(void*)task.func)((void**)task.arg);
                } else {
                    task.func(task.arg[0]);
                }
                return true;
            }
        } else if (diff < 0) {
            // Queue is empty
            return false;
        } else {
            // Another consumer stole our task slot, reload the tail pointer
            tail = atomic_load_uint32_ex(&wq->tail, ATOMIC_RELAXED);
        }

        // Yield this thread timeslice
        sleep_ms(0);
    }
}

static bool workqueue_submit(work_queue_t* wq, thread_func_t func, void** arg, unsigned arg_count, bool va)
{
    uint32_t head = atomic_load_uint32_ex(&wq->head, ATOMIC_RELAXED);
    while (true) {
        task_item_t* task_ptr = &wq->tasks[head & WORKQUEUE_MASK];
        uint32_t seq = atomic_load_uint32_ex(&task_ptr->seq, ATOMIC_ACQUIRE);
        int32_t diff = (int32_t)seq - (int32_t)head;
        if (diff == 0) {
            // This is an empty task slot
            if (atomic_cas_uint32_ex(&wq->head, head, head + 1, true, ATOMIC_RELAXED, ATOMIC_RELAXED)) {
                // We claimed the slot, fill it with data
                task_ptr->func = func;
                for (size_t i=0; i<arg_count; ++i) task_ptr->arg[i] = arg[i];
                task_ptr->flags = (va ? 2 : 0);
                // Mark the slot as filled
                atomic_store_uint32_ex(&task_ptr->seq, head + 1, ATOMIC_RELEASE);
                return true;
            }
        } else if (diff < 0) {
            // Queue is full
            return false;
        } else {
            // Another producer stole our task slot, reload the head pointer
            head = atomic_load_uint32_ex(&wq->head, ATOMIC_RELAXED);
        }

        // Yield this thread timeslice
        sleep_ms(0);
    }
    return false;
}

static void thread_workers_terminate()
{
    atomic_store_uint32_ex(&pool_run, 0, ATOMIC_RELAXED);
    condvar_wake_all(pool_cond);
    for (size_t i=0; i<WORKER_THREADS; ++i) {
        thread_join(pool_threads[i]);
    }
    condvar_free(pool_cond);
}

static void* threadpool_worker(void* ptr)
{
    while (atomic_load_uint32_ex(&pool_run, ATOMIC_RELAXED)) {
        while (workqueue_try_perform(&pool_wq));
        condvar_wait(pool_cond, CONDVAR_INFINITE);
    }
    return ptr;
}

static bool thread_queue_task(thread_func_t func, void** arg, unsigned arg_count, bool va)
{
    DO_ONCE ({
        atomic_store_uint32_ex(&pool_run, 1, ATOMIC_RELAXED);
        workqueue_init(&pool_wq);
        pool_cond = condvar_create();
        for (size_t i=0; i<WORKER_THREADS; ++i) {
            pool_threads[i] = thread_create(threadpool_worker, NULL);
        }
        call_at_deinit(thread_workers_terminate);
    });

    if (workqueue_submit(&pool_wq, func, arg, arg_count, va)) {
        //if (condvar_waiters(pool_cond) == WORKER_THREADS)
        condvar_wake(pool_cond);
        return true;
    }

    // Still not queued!
    // Assuming entire threadpool is busy, just do a blocking task
    DO_ONCE(rvvm_warn("Blocking on workqueue task %p", func));
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
