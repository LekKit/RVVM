/*
chardev_term.c - Terminal backend for UART
Copyright (C) 2023  LekKit <github.com/LekKit>
                    宋文武 <iyzsong@envs.net>

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

#include "chardev.h"
#include "spinlock.h"
#include "rvtimer.h"
#include "ringbuf.h"
#include "utils.h"
#include "mem_ops.h"

#if (defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)) && !defined(__EMSCRIPTEN__)
#include <sys/types.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define POSIX_TERM_IMPL

static struct termios orig_term_opts;

static void term_origmode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term_opts);
}

static void term_rawmode()
{
    tcgetattr(STDIN_FILENO, &orig_term_opts);
    atexit(term_origmode);
    struct termios term_opts = orig_term_opts;
    term_opts.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    term_opts.c_iflag &= ~(IXON | ICRNL);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_opts);
}

#elif defined(_WIN32) && !defined(UNDER_CE)
#include <windows.h>
#include <conio.h>

#define WIN32_TERM_IMPL

static void term_rawmode()
{
    //AttachConsole(ATTACH_PARENT_PROCESS);
    SetConsoleOutputCP(CP_UTF8);
    // ENABLE_VIRTUAL_TERMINAL_INPUT
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0x200);
    // ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), 0x5);
}

#else
#include <stdio.h>
#warning No UART input support!

static void term_rawmode() {}

#endif

typedef struct {
    chardev_t chardev;
    spinlock_t lock;
    spinlock_t io_lock;
    uint32_t flags;
    int rfd, wfd;
    ringbuf_t rx, tx;
} chardev_term_t;

static uint32_t term_update_flags(chardev_term_t* term)
{
    uint32_t flags = 0;
    if (ringbuf_avail(&term->rx)) flags |= CHARDEV_RX;
    if (ringbuf_space(&term->tx)) flags |= CHARDEV_TX;

    return flags & ~atomic_swap_uint32(&term->flags, flags);
}

static void term_push_io(chardev_term_t* term, char* buffer, size_t* rx_size, size_t* tx_size)
{
    size_t to_read = rx_size ? *rx_size : 0;
    size_t to_write = tx_size ? *tx_size : 0;
    if (rx_size) *rx_size = 0;
    if (tx_size) *tx_size = 0;
    UNUSED(term);
#if defined(POSIX_TERM_IMPL)
    fd_set rfds, wfds;
    struct timeval timeout = {0};
    int nfds = EVAL_MAX(term->rfd, term->wfd) + 1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (to_read) FD_SET(term->rfd, &rfds);
    if (to_write) FD_SET(term->wfd, &wfds);
    if ((to_read || to_write) && select(nfds, to_read ? &rfds : NULL, to_write ? &wfds : NULL, NULL, &timeout) > 0) {
        if (to_write && FD_ISSET(term->wfd, &wfds)) {
            int tmp = write(term->wfd, buffer, to_write);
            *tx_size = tmp > 0 ? tmp : 0;
        }
        if (to_read && FD_ISSET(term->rfd, &rfds)) {
            int tmp = read(term->rfd, buffer, to_read);
            *rx_size = tmp > 0 ? tmp : 0;
        }
    }
#elif defined(WIN32_TERM_IMPL)
    if (to_write) {
        DWORD count = 0;
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buffer, to_write, &count, NULL);
        *tx_size = count;
    }
    if (to_read && _kbhit()) {
        wchar_t w_buf[64] = {0};
        size_t count = EVAL_MIN(to_read / 6, STATIC_ARRAY_SIZE(w_buf));
        DWORD w_chars = 0;
        ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), w_buf, count, &w_chars, NULL);
        *rx_size = WideCharToMultiByte(CP_UTF8, 0,
            w_buf, w_chars, buffer, to_read, NULL, NULL);
    }
#else
    UNUSED(to_read);
    if (to_write) {
        printf("%s", buffer);
        *tx_size = to_write;
    }
#endif
}

static void term_update(chardev_t* dev)
{
    chardev_term_t* term = dev->data;
    uint32_t flags = 0;
    char buffer[257] = {0};
    size_t rx_size = 0, tx_size = 0;

    spin_lock(&term->io_lock);
    spin_lock(&term->lock);
    rx_size = EVAL_MIN(ringbuf_space(&term->rx), sizeof(buffer));
    tx_size = ringbuf_peek(&term->tx, buffer, 256);
    spin_unlock(&term->lock);

    term_push_io(term, buffer, &rx_size, &tx_size);

    spin_lock(&term->lock);
    ringbuf_write(&term->rx, buffer, rx_size);
    ringbuf_skip(&term->tx, tx_size);
    flags = term_update_flags(term);
    spin_unlock(&term->lock);
    spin_unlock(&term->io_lock);

    if (flags) chardev_notify(&term->chardev, flags);
}

static size_t term_read(chardev_t* dev, void* buf, size_t nbytes)
{
    chardev_term_t* term = dev->data;
    size_t ret = 0;
    spin_lock(&term->lock);
    ret = ringbuf_read(&term->rx, buf, nbytes);
    if (!ringbuf_avail(&term->rx) && spin_try_lock(&term->io_lock)) {
        char buffer[256] = {0};
        size_t rx_size = sizeof(buffer);
        term_push_io(term, buffer, &rx_size, NULL);
        ringbuf_write(&term->rx, buffer, rx_size);
        spin_unlock(&term->io_lock);
    }
    term_update_flags(term);
    spin_unlock(&term->lock);
    return ret;
}

static size_t term_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    chardev_term_t* term = dev->data;
    size_t ret = 0;
    spin_lock(&term->lock);
    ret = ringbuf_write(&term->tx, buf, nbytes);
    if (!ringbuf_space(&term->tx) && spin_try_lock(&term->io_lock)) {
        char buffer[257] = {0};
        size_t tx_size = ringbuf_peek(&term->tx, buffer, 256);
        term_push_io(term, buffer, NULL, &tx_size);
        ringbuf_skip(&term->tx, tx_size);
        spin_unlock(&term->io_lock);
    }
    term_update_flags(term);
    spin_unlock(&term->lock);
    return ret;
}

static uint32_t term_poll(chardev_t* dev)
{
    chardev_term_t* term = dev->data;
    return atomic_load_uint32(&term->flags);
}

static void term_remove(chardev_t* dev)
{
    chardev_term_t* term = dev->data;
    term_update(dev);
    ringbuf_destroy(&term->rx);
    ringbuf_destroy(&term->tx);
#ifdef POSIX_TERM_IMPL
    if (term->rfd != 0) close(term->rfd);
    if (term->wfd != 1 && term->wfd != term->rfd) close(term->wfd);
#endif
    free(term);
}

PUBLIC chardev_t* chardev_term_create(void)
{
    DO_ONCE(term_rawmode());
    return chardev_fd_create(0, 1);
}

PUBLIC chardev_t* chardev_fd_create(int rfd, int wfd)
{
#ifndef POSIX_TERM_IMPL
    if (rfd != 0 || wfd != 1) {
        rvvm_error("No FD chardev support on non-POSIX");
        return NULL;
    }
#endif

    chardev_term_t* term = safe_new_obj(chardev_term_t);
    ringbuf_create(&term->rx, 256);
    ringbuf_create(&term->tx, 256);
    term->chardev.data = term;
    term->chardev.read = term_read;
    term->chardev.write = term_write;
    term->chardev.poll = term_poll;
    term->chardev.update = term_update;
    term->chardev.remove = term_remove;
    term->rfd = rfd;
    term->wfd = wfd;

    return &term->chardev;
}

PUBLIC chardev_t* chardev_pty_create(const char* path)
{
    if (rvvm_strcmp(path, "stdout")) return chardev_term_create();
#ifdef POSIX_TERM_IMPL
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd >= 0) return chardev_fd_create(fd, fd);
    rvvm_error("Could not open PTY %s", path);
#else
    UNUSED(path);
    rvvm_error("No PTY chardev support on non-POSIX");
#endif
    return NULL;
}
