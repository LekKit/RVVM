/*
chardev_term.c - Terminal backend for UART
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>
Copyright (C) 2023  宋文武 <iyzsong@envs.net>

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

#include "chardev_term.h"
#include "threading.h"
#include "utils.h"

#if (defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)) && !defined(__EMSCRIPTEN__)
#include <sys/types.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


static const uint8_t TERM_CTL_WAIT_READ  = 1;
static const uint8_t TERM_CTL_WAIT_WRITE = 2;
static const uint8_t TERM_CTL_STOP       = 3;
static const uint8_t TERM_CTL_CONT       = 4;
typedef struct {
    thread_ctx_t* thread;
    int ctl[2];
    void (*on_input_available)(chardev_t* dev, void* watcher_data);
    void (*on_output_available)(chardev_t* dev, void* watcher_data);
    void* watcher_data;
} chardev_term_t;

static struct termios orig_term_opts;

static void terminal_origmode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term_opts);
}

static void terminal_rawmode()
{
    tcgetattr(STDIN_FILENO, &orig_term_opts);
    atexit(terminal_origmode);
    struct termios term_opts = orig_term_opts;
    term_opts.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    term_opts.c_iflag &= ~(IXON | ICRNL);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_opts);
}

static void* terminal_thread(void* arg)
{
    chardev_t* dev = (chardev_t*)arg;
    chardev_term_t* term = (chardev_term_t*)dev->data;
    int ctlfd = term->ctl[0];
    bool watch_read = false;
    bool watch_write = false;

    while (true) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(ctlfd, &rfds);
        if (watch_read)
            FD_SET(0, &rfds);
        if (watch_write)
            FD_SET(1, &wfds);
        if (select(ctlfd + 1, &rfds, &wfds, NULL, NULL) == -1) {
            rvvm_error("select() failed: %s", strerror(errno));
            break;
        }
        if (FD_ISSET(ctlfd, &rfds)) {
            uint8_t ctl = 0;
            if (read(ctlfd, &ctl, 1) < 1)
                break;
            if (ctl == TERM_CTL_STOP) {
                // Wait 'terminal_watch' to update callbacks.
                while (ctl != TERM_CTL_CONT) {
                    if (read(ctlfd, &ctl, 1) != 1)
                        return NULL;
                }
                if (term->on_input_available)
                    watch_read = true;
                if (term->on_output_available)
                    watch_write = true;
            }
            if (ctl == TERM_CTL_WAIT_READ && term->on_input_available)
                watch_read = true;
            if (ctl == TERM_CTL_WAIT_WRITE && term->on_output_available)
                watch_write = true;
        }
        if (FD_ISSET(0, &rfds) && term->on_input_available) {
            watch_read = false;
            term->on_input_available(dev, term->watcher_data);
        }
        if (FD_ISSET(1, &wfds) && term->on_output_available) {
            watch_write = false;
            term->on_output_available(dev, term->watcher_data);
        }
    }
    return NULL;
}

static void terminal_watch(chardev_t* dev,
                           void (*on_input_available)(chardev_t* dev, void* watcher_data),
                           void (*on_output_available)(chardev_t* dev, void* watcher_data),
                           void* watcher_data)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    if (write(term->ctl[1], &TERM_CTL_STOP, 1) != 1)
        close(term->ctl[1]);
    term->on_input_available = on_input_available;
    term->on_output_available = on_output_available;
    term->watcher_data = watcher_data;
    if (write(term->ctl[1], &TERM_CTL_CONT, 1) != 1)
        close(term->ctl[1]);
}

static void terminal_destroy(chardev_t* dev)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    if (term->thread == NULL)
        return;
    close(term->ctl[1]);
    thread_join(term->thread);
    close(term->ctl[0]);
    term->thread = NULL;
}

static size_t terminal_read(chardev_t* dev, void* buf, size_t nbytes)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    int n = read(0, buf, nbytes);
    if (write(term->ctl[1], &TERM_CTL_WAIT_READ, 1) != 1)
        close(term->ctl[1]);
    return n > 0 ? n : 0;
}

static size_t terminal_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    int n = write(1, buf, nbytes);
    if (write(term->ctl[1], &TERM_CTL_WAIT_WRITE, 1) != 1)
        close(term->ctl[1]);
    return n > 0 ? n : 0;
}

chardev_t* chardev_term_create()
{
    static chardev_term_t term = {
        .thread = NULL,
        .on_input_available = NULL,
        .on_output_available = NULL,
    };
    static chardev_t dev = {
        .read = terminal_read,
        .write = terminal_write,
        .watch = terminal_watch,
        .destroy = terminal_destroy,
        .data = &term,
    };
    if (term.thread != NULL) {
        rvvm_error("Only one chardev_term may exist at the same time");
        return NULL;
    }
    terminal_rawmode();
    if (pipe(term.ctl) == -1) {
        rvvm_error("pipe() failed: %s", strerror(errno));
        return NULL;
    }
    term.thread = thread_create(terminal_thread, &dev);
    return &dev;
}

#elif defined(_WIN32) && !defined(UNDER_CE)
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include "atomics.h"
#include "rvtimer.h"

#define TERM_CTL_WAIT_READ  0x01
#define TERM_CTL_WAIT_WRITE 0x02
#define TERM_CTL_PAUSE      0x04
#define TERM_CTL_PAUSED     0x08
#define TERM_CTL_SHUTDOWN   0x10

typedef struct {
    thread_ctx_t* thread;
    uint32_t ctl;
    void (*on_input_available)(chardev_t* dev, void* watcher_data);
    void (*on_output_available)(chardev_t* dev, void* watcher_data);
    void* watcher_data;
} chardev_term_t;

static void terminal_rawmode()
{
    //AttachConsole(ATTACH_PARENT_PROCESS);
    SetConsoleOutputCP(CP_UTF8);
    // ENABLE_VIRTUAL_TERMINAL_INPUT
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0x200);
    // ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), 0x5);
}

static size_t terminal_read(chardev_t* dev, void* buf, size_t nbytes)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    static char t_buff[32];
    static size_t t_head = 0, t_tail = 0;
    if (nbytes == 0)
        return 0;
    if (t_head == t_tail) {
        wchar_t w_buff[8];
        DWORD w_chars;
        ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), w_buff, STATIC_ARRAY_SIZE(w_buff), &w_chars, NULL);
        t_head = WideCharToMultiByte(CP_UTF8, 0, w_buff, w_chars, t_buff, sizeof(t_buff), NULL, NULL);
        t_tail = 0;
    }
    if (t_head != t_tail) {
        *(uint8_t*)buf = t_buff[t_tail++];
        atomic_or_uint32(&term->ctl, TERM_CTL_WAIT_READ);
        return 1;
    } else return 0;
}

static size_t terminal_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    if (nbytes == 0)
        return 0;
    if (putc(*(uint8_t*)buf, stdout) == EOF)
        return 0;
    atomic_or_uint32(&term->ctl, TERM_CTL_WAIT_WRITE);
    return 1;
}

static void* terminal_thread(void* arg)
{
    chardev_t* dev = (chardev_t*)arg;
    chardev_term_t* term = (chardev_term_t*)dev->data;
    while (true) {
        uint32_t ctl = atomic_load_uint32(&term->ctl);
        if (ctl & TERM_CTL_SHUTDOWN)
            break;
        // Wait 'terminal_watch' to update callbacks.
        if (ctl & TERM_CTL_PAUSE) {
            atomic_or_uint32(&term->ctl, TERM_CTL_PAUSED);
            while (atomic_load_uint32(&term->ctl) & TERM_CTL_PAUSE);
        }
        if (term->on_input_available && _kbhit() && (ctl & TERM_CTL_WAIT_READ)) {
            atomic_and_uint32(&term->ctl, ~TERM_CTL_WAIT_READ);
            term->on_input_available(dev, term->watcher_data);
        }
        fflush(stdout);
        if (term->on_output_available && (ctl & TERM_CTL_WAIT_WRITE)) {
            atomic_and_uint32(&term->ctl, ~TERM_CTL_WAIT_WRITE);
            term->on_output_available(dev, term->watcher_data);
        }
        sleep_ms(1);
    }
    return NULL;
}

static void terminal_destroy(chardev_t* dev)
{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    if (term->thread == NULL)
        return;
    atomic_or_uint32(&term->ctl, TERM_CTL_SHUTDOWN);
    thread_join(term->thread);
    term->thread = NULL;
}

static void terminal_watch(chardev_t* dev,
                           void (*on_input_available)(chardev_t* dev, void* watcher_data),
                           void (*on_output_available)(chardev_t* dev, void* watcher_data),
                           void* watcher_data)

{
    chardev_term_t* term = (chardev_term_t*)dev->data;
    atomic_or_uint32(&term->ctl, TERM_CTL_PAUSE);
    while (!(atomic_load_uint32(&term->ctl) & TERM_CTL_PAUSED));
    term->on_input_available = on_input_available;
    term->on_output_available = on_output_available;
    term->watcher_data = watcher_data;
    if (on_input_available)
        atomic_or_uint32(&term->ctl, TERM_CTL_WAIT_READ);
    if (on_output_available)
        atomic_or_uint32(&term->ctl, TERM_CTL_WAIT_WRITE);
    atomic_and_uint32(&term->ctl, ~(TERM_CTL_PAUSE | TERM_CTL_PAUSED));
}

chardev_t* chardev_term_create()
{
    static chardev_term_t term = {
        .thread = NULL,
        .on_input_available = NULL,
        .on_output_available = NULL,
        .ctl = 0,
    };
    static chardev_t dev = {
        .read = terminal_read,
        .write = terminal_write,
        .destroy = terminal_destroy,
        .watch = terminal_watch,
        .data = &term,
    };
    if (term.thread != NULL) {
        rvvm_error("Only one chardev_term may exist at the same time");
        return NULL;
    }
    terminal_rawmode();
    term.thread = thread_create(terminal_thread, &dev);
    return &dev;
}

#else
#warning No UART input support!

static size_t terminal_read(chardev_t* dev, void* buf, size_t nbytes)
{
    UNUSED(dev);
    UNUSED(buf);
    UNUSED(nbytes);
    return 0;
}

static size_t terminal_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    UNUSED(dev);
    UNUSED(buf);
    UNUSED(nbytes);
    return 0;
}

static void terminal_watch(chardev_t* dev,
                           void (*on_input_available)(chardev_t* dev, void* watcher_data),
                           void (*on_output_available)(chardev_t* dev, void* watcher_data),
                           void* watcher_data)
{
    UNUSED(dev);
    UNUSED(on_input_available);
    UNUSED(on_output_available);
    UNUSED(watcher_data);
}

static void terminal_destroy(chardev_t* dev)
{
    UNUSED(dev);
}

chardev_t* chardev_term_create()
{
    static chardev_t dev = {
        .read = terminal_read,
        .write = terminal_write,
        .watch = terminal_watch,
        .destroy = terminal_destroy,
    };
    return &dev;
}

#endif
