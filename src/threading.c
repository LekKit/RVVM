/*
threading.c - Threads
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

#ifdef _WIN32
#include <windows.h>

static void __stdcall apc_membarrier(ULONG_PTR p)
{
    UNUSED(p);
    atomic_fence();
}

#else

#include <pthread.h>
#include <signal.h>

static void signal_membarrier(int sig)
{
    UNUSED(sig);
    atomic_fence();
}

#endif


thread_handle_t thread_create(thread_func_t func_name, void *arg)
{
    thread_handle_t handle;
#ifdef _WIN32
    handle = malloc(sizeof(HANDLE));
    if (handle) *(HANDLE*)handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(const void*)func_name, arg, 0, NULL);
#else
    handle = malloc(sizeof(pthread_t));
    if (handle) pthread_create((pthread_t*)handle, NULL, func_name, arg);
#endif
    return handle;
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
