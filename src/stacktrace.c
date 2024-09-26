/*
stacktrace.c - Stacktrace (Using dynamically loaded libbacktrace)
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "stacktrace.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if (defined(__unix__) || defined(__APPLE__) || defined(__MINGW32__)) \
 && !defined(NO_STACKTRACE) && !defined(UNDER_CE) && !defined(__SANITIZE_ADDRESS__)
#define SIGNAL_IMPL
#include <stdlib.h>
#include <signal.h>
#ifndef SIGBUS
#define SIGBUS 10
#endif
#endif

// RVVM internal headers come after system headers because of safe_free()
#include "compiler.h"
#include "utils.h"
#include "dlib.h"

/*
 * libbacktrace boilerplace
 */

struct backtrace_state;

typedef void (*backtrace_error_callback)(void* data, const char* msg, int errnum);
typedef int (*backtrace_full_callback)(void* data, uintptr_t pc, const char* filename, int lineno, const char* function);

static struct backtrace_state* (*backtrace_create_state)(const char* filename, int threaded,
                                                         backtrace_error_callback error_callback, void *data) = NULL;
static int (*backtrace_full)(struct backtrace_state *state, int skip, backtrace_full_callback callback,
                             backtrace_error_callback error_callback, void *data) = NULL;
static void (*backtrace_print)(struct backtrace_state *state, int skip, FILE* file) = NULL;

static struct backtrace_state* bt_state = NULL;

static void backtrace_dummy_error(void* data, const char* msg, int errnum)
{
    UNUSED(data); UNUSED(msg); UNUSED(errnum);
}

static int backtrace_dummy_callback(void* data, uintptr_t pc, const char* filename, int lineno, const char* function)
{
    UNUSED(data); UNUSED(pc); UNUSED(filename); UNUSED(lineno); UNUSED(function);
    return 0;
}

/*
 * Fatal signal stacktraces
 */

#ifdef SIGNAL_IMPL

static void signal_handler(int sig)
{
    switch (sig) {
        case SIGSEGV:
            rvvm_warn("Fatal signal: Segmentation fault!");
            break;
        case SIGBUS:
            rvvm_warn("Fatal signal: Bus fault - Address is non-canonic or misaligned!");
            break;
        case SIGILL:
            rvvm_warn("Fatal signal: Illegal instruction!");
            break;
        case SIGFPE:
            rvvm_warn("Fatal signal: Division by zero!");
            break;
        default:
            rvvm_warn("Fatal signal %d", sig);
            break;
    }
    rvvm_warn("Stacktrace:");
    stacktrace_print();
    _Exit(-sig);
}

static void set_signal_handler(int sig)
{
#ifdef SA_SIGINFO
    struct sigaction sa_old = {0};
    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = SA_RESTART,
    };
    sigaction(sig, NULL, &sa_old);
    if (!(sa_old.sa_flags & SA_SIGINFO) && (sa_old.sa_handler == NULL || sa_old.sa_handler == (void*)SIG_IGN || sa_old.sa_handler != (void*)SIG_DFL)) {
        // Signal not used
        sigaction(sig, &sa, NULL);
    }
#else
    void* prev = signal(sig, signal_handler);
    if (prev != NULL && prev != (void*)SIG_IGN && prev != (void*)SIG_DFL) {
        // Signal already used
        signal(sig, prev);
    }
#endif
}

#endif

static void backtrace_init_once(void)
{
    if (rvvm_has_arg("no_stacktrace")) {
        return;
    }

    dlib_ctx_t* libbt = dlib_open("backtrace", DLIB_NAME_PROBE);

    backtrace_create_state = dlib_resolve(libbt, "backtrace_create_state");
    backtrace_full = dlib_resolve(libbt, "backtrace_full");
    backtrace_print = dlib_resolve(libbt, "backtrace_print");

    dlib_close(libbt);

    if (backtrace_create_state) {
        bt_state = backtrace_create_state(NULL, true, backtrace_dummy_error, NULL);
    }
    if (backtrace_full && bt_state) {
        // Preload backtracing data, isolation is enabled later on
        backtrace_full(bt_state, 0, backtrace_dummy_callback, backtrace_dummy_error, NULL);

#ifdef SIGNAL_IMPL
        set_signal_handler(SIGSEGV);
        set_signal_handler(SIGBUS);
        set_signal_handler(SIGILL);
        set_signal_handler(SIGFPE);
#endif
    }
}

void stacktrace_init(void)
{
    DO_ONCE(backtrace_init_once());
}

void stacktrace_print(void)
{
    stacktrace_init();
    if (backtrace_print && bt_state) {
        backtrace_print(bt_state, 0, stderr);

    }
}
