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
#include "utils.h"
#include "mem_ops.h"

#if (defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)) && !defined(__EMSCRIPTEN__)
#include <sys/types.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

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

#endif

typedef struct {
    chardev_t chardev;
    spinlock_t lock;
    uint32_t flags;
    uint32_t rx_size, rx_cur;
    uint32_t tx_size;
    char rx_buf[256];
    char tx_buf[256];
    int rfd, wfd;
} chardev_term_t;

static uint32_t term_update_flags(chardev_term_t* term)
{
    uint32_t flags = 0;
    if (term->rx_cur < term->rx_size) flags |= CHARDEV_RX;
    if (term->tx_size < sizeof(term->tx_buf)) flags |= CHARDEV_TX;

    return flags & ~atomic_swap_uint32(&term->flags, flags);
}

static void term_update(chardev_t* dev)
{
    chardev_term_t* term = dev->data;
    uint32_t flags = 0;
    size_t buf_size = 0;
    char buffer[257] = {0}; // For unlocked write
#if defined(POSIX_TERM_IMPL)
    fd_set rfds, wfds;
    struct timeval timeout = {0};
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(term->rfd, &rfds);
    FD_SET(term->wfd, &wfds);
    int nfds = 1 + (term->rfd > term->wfd ? term->rfd : term->wfd);
    if (select(nfds, &rfds, &wfds, NULL, &timeout) > 0) {
        spin_lock(&term->lock);
        if (FD_ISSET(term->rfd, &rfds) && term->rx_cur == term->rx_size) {
            int tmp = read(term->rfd, term->rx_buf, sizeof(term->rx_buf));
            term->rx_size = tmp > 0 ? tmp : 0;
            term->rx_cur = 0;
        }
        if (FD_ISSET(term->wfd, &wfds) && term->tx_size) {
            buf_size = term->tx_size;
            memcpy(buffer, term->tx_buf, term->tx_size);
            term->tx_size = 0;
        }
        flags = term_update_flags(term);
        spin_unlock(&term->lock);
    }
    if (buf_size) while (write(term->wfd, buffer, buf_size) < 0);
#elif defined(WIN32_TERM_IMPL)
    spin_lock(&term->lock);
    if (term->rx_cur == term->rx_size && _kbhit()) {
        wchar_t w_buf[64] = {0};
        DWORD w_chars = 0;
        ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), w_buf, STATIC_ARRAY_SIZE(w_buf), &w_chars, NULL);
        term->rx_size = WideCharToMultiByte(CP_UTF8, 0,
            w_buf, w_chars, term->rx_buf, sizeof(term->rx_buf), NULL, NULL);
        term->rx_cur = 0;
    }
    if (term->tx_size) {
        buf_size = term->tx_size;
        memcpy(buffer, term->tx_buf, term->tx_size);
        term->tx_size = 0;
    }
    flags = term_update_flags(term);
    spin_unlock(&term->lock);
    if (buf_size) {
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buffer, buf_size, NULL, NULL);
    }
#else
    spin_lock(&term->lock);
    if (term->tx_size) {
        memcpy(buffer, term->tx_buf, term->tx_size);
        term->tx_size = 0;
    }
    flags = term_update_flags(term);
    spin_unlock(&term->lock);
    if (buf_size) printf("%s", buffer);
#endif

    if (flags) chardev_notify(&term->chardev, flags);
}

static size_t term_read(chardev_t* dev, void* buf, size_t nbytes)
{
    chardev_term_t* term = dev->data;
    spin_lock(&term->lock);
    size_t len = term->rx_size - term->rx_cur;
    if (len > nbytes) len = nbytes;
    memcpy(buf, term->rx_buf + term->rx_cur, len);
    term->rx_cur += len;
    bool push = term->rx_cur == term->rx_size;
    spin_unlock(&term->lock);
    if (push) term_update(dev);
    return len;
}

static size_t term_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    chardev_term_t* term = dev->data;
    spin_lock(&term->lock);
    size_t len = sizeof(term->tx_buf) - term->tx_size;
    if (len > nbytes) len = nbytes;
    memcpy(term->tx_buf + term->tx_size, buf, len);
    term->tx_size += len;
    bool push = term->tx_size == sizeof(term->tx_buf);
    spin_unlock(&term->lock);
    if (push) term_update(dev);
    return len;
}

static uint32_t term_poll(chardev_t* dev)
{
    chardev_term_t* term = dev->data;
    return atomic_load_uint32(&term->flags);
}

PUBLIC chardev_t* chardev_term_create(void)
{
    DO_ONCE(term_rawmode());
    return chardev_term_fd_create(0, 1);
}

PUBLIC chardev_t* chardev_term_fd_create(int rfd, int wfd)
{
    chardev_term_t* term = safe_new_obj(chardev_term_t);
    term->chardev.data = term;
    term->chardev.read = term_read;
    term->chardev.write = term_write;
    term->chardev.poll = term_poll;
    term->chardev.update = term_update;
    term->rfd = rfd;
    term->wfd = wfd;
    return &term->chardev;
}
