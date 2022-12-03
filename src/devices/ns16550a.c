/*
ns16550a.c - NS16550A UART
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#include "ns16550a.h"
#include "spinlock.h"
#include "utils.h"

#ifdef USE_FDT
#include "fdtlib.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#define NS16550A_REG_SIZE 0x8

struct ns16550a_data {
    plic_ctx_t* plic;
    uint32_t irq;
    spinlock_t lock;

    uint8_t ier;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t scr;
    uint8_t dll;
    uint8_t dlm;

    uint8_t buf;
    uint8_t len;
};

// Read
#define NS16550A_REG_RBR_DLL 0x0
#define NS16550A_REG_IIR     0x2
// Write
#define NS16550A_REG_THR_DLL 0x0
#define NS16550A_REG_FCR     0x2
// RW
#define NS16550A_REG_IER_DLM 0x1
#define NS16550A_REG_LCR     0x3
#define NS16550A_REG_MCR     0x4
#define NS16550A_REG_LSR     0x5
#define NS16550A_REG_MSR     0x6
#define NS16550A_REG_SCR     0x7

#define NS16550A_IER_RECV    0x1
#define NS16550A_IER_THR     0x2
#define NS16550A_IER_LSR     0x4
#define NS16550A_IER_MSR     0x8

#define NS16550A_IIR_FIFO    0xC0
#define NS16550A_IIR_NONE    0x1
#define NS16550A_IIR_MSR     0x0
#define NS16550A_IIR_THR     0x2
#define NS16550A_IIR_RECV    0x4
#define NS16550A_IIR_LSR     0x6

#define NS16550A_LSR_RECV    0x1
#define NS16550A_LSR_THR     0x60

#define NS16550A_LCR_DLAB    0x80

#if defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>

static struct termios orig_term_opts;

static void terminal_origmode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term_opts);
}

static void terminal_rawmode()
{
    static bool once = true;
    if (once) {
        tcgetattr(STDIN_FILENO, &orig_term_opts);
        atexit(terminal_origmode);
        struct termios term_opts = orig_term_opts;
        term_opts.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        term_opts.c_iflag &= ~(IXON | ICRNL);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_opts);
        once = false;
    }
}

static uint8_t terminal_readchar(void* addr)
{
    fd_set rfds;
    struct timeval timeout = {0};
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    if (select(1, &rfds, NULL, NULL, &timeout) > 0) {
        return read(0, addr, 1) == 1;
    }
    return 0;
}

#elif defined(_WIN32) && !defined(UNDER_CE)
#include <windows.h>
#include <conio.h>

static void terminal_rawmode()
{
    //AttachConsole(ATTACH_PARENT_PROCESS);
    SetConsoleOutputCP(CP_UTF8);
    // ENABLE_VIRTUAL_TERMINAL_INPUT
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0x200);
    // ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), 0x5);
}

static uint8_t terminal_readchar(void* addr)
{
    static char t_buff[32];
    static size_t t_head = 0, t_tail = 0;
    if (t_head == t_tail && _kbhit()) {
        wchar_t w_buff[8];
        DWORD w_chars;
        ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), w_buff, ARRAYSIZE(w_buff), &w_chars, NULL);
        t_head = WideCharToMultiByte(CP_UTF8, 0, w_buff, w_chars, t_buff, sizeof(t_buff), NULL, NULL);
        t_tail = 0;
    }
    if (t_head != t_tail) {
        *(uint8_t*)addr = t_buff[t_tail++];
        return 1;
    } else return 0;
}

#else
#warning No UART input support!
static void terminal_rawmode()
{
}

static uint8_t terminal_readchar(void* addr)
{
    UNUSED(addr);
    return 0;
}
#endif

static bool ns16550a_mmio_read(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    uint8_t *value = (uint8_t*)memory_data;
    UNUSED(size);
    spin_lock(&regs->lock);
    // Read char from stdin
    if (!regs->len) regs->len = terminal_readchar(&regs->buf);
    switch (offset) {
        case NS16550A_REG_RBR_DLL:
            if (regs->lcr & NS16550A_LCR_DLAB) {
                *value = regs->dll;
            } else {
                *value = regs->buf;
                regs->len = 0;
                regs->buf = 0;
            }
            break;
        case NS16550A_REG_IER_DLM:
            if (regs->lcr & NS16550A_LCR_DLAB) {
                *value = regs->dlm;
            } else {
                *value = regs->ier;
            }
            break;
        case NS16550A_REG_IIR:
            if (regs->len && (regs->ier & NS16550A_IER_RECV)) {
                *value = NS16550A_IIR_RECV | NS16550A_IIR_FIFO;
            } else if (regs->ier & NS16550A_IER_THR) {
                *value = NS16550A_IIR_THR | NS16550A_IIR_FIFO;
            } else {
                *value = NS16550A_IIR_NONE | NS16550A_IIR_FIFO;
            }
            break;
        case NS16550A_REG_LCR:
            *value = regs->lcr;
            break;
        case NS16550A_REG_MCR:
            *value = regs->mcr;
            break;
        case NS16550A_REG_LSR:
            *value = NS16550A_LSR_THR | (regs->len ? NS16550A_LSR_RECV : 0);
            break;
        case NS16550A_REG_MSR:
            *value = 0xF0;
            break;
        case NS16550A_REG_SCR:
            *value = regs->scr;
            break;
        default:
            *value = 0;
            break;
    }
    spin_unlock(&regs->lock);
    return true;
}

static bool ns16550a_mmio_write(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    uint8_t value = *(uint8_t*)memory_data;
    UNUSED(size);
    spin_lock(&regs->lock);
    switch (offset) {
        case NS16550A_REG_THR_DLL:
            if (regs->lcr & NS16550A_LCR_DLAB) {
                regs->dll = value;
            } else {
                putc(value, stdout);
                fflush(stdout);
            }
            break;
        case NS16550A_REG_IER_DLM:
            if (regs->lcr & NS16550A_LCR_DLAB) {
                regs->dlm = value;
            } else {
                regs->ier = value;
                if (regs->len && (regs->ier & NS16550A_IER_RECV)) {
                    plic_send_irq(regs->plic, regs->irq);
                } else if (regs->ier & (NS16550A_IER_THR | NS16550A_IER_LSR)) {
                    plic_send_irq(regs->plic, regs->irq);
                }
            }
            break;
        case NS16550A_REG_LCR:
            regs->lcr = value;
            break;
        case NS16550A_REG_MCR:
            regs->mcr = value;
            break;
        case NS16550A_REG_SCR:
            regs->scr = value;
            break;
        default:
            break;
    }
    spin_unlock(&regs->lock);
    return true;
}

static void ns16550a_update(rvvm_mmio_dev_t* device)
{
    struct ns16550a_data* regs = (struct ns16550a_data*)device->data;
    if (regs->plic) {
        spin_lock(&regs->lock);
        regs->len = regs->len ? 1 : terminal_readchar(&regs->buf);
        if (regs->len && (regs->ier & NS16550A_IER_RECV)) {
            plic_send_irq(regs->plic, regs->irq);
        } else if (regs->ier & (NS16550A_IER_THR | NS16550A_IER_LSR)) {
            // This fixes OpenBSD tty(?), doesn't work from mmio handlers
            // Might as well be related to PLIC, lets disable it for now
            //plic_send_irq(regs->plic, regs->irq);
        }
        spin_unlock(&regs->lock);
    }
}

static rvvm_mmio_type_t ns16550a_dev_type = {
    .name = "ns16550a",
    .update = ns16550a_update,
};

PUBLIC void ns16550a_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq)
{
    terminal_rawmode();

    struct ns16550a_data* ptr = safe_calloc(sizeof(struct ns16550a_data), 1);
    ptr->plic = plic;
    ptr->irq = irq;
    spin_init(&ptr->lock);

    rvvm_mmio_dev_t ns16550a = {0};
    ns16550a.min_op_size = 1;
    ns16550a.max_op_size = 1;
    ns16550a.read = ns16550a_mmio_read;
    ns16550a.write = ns16550a_mmio_write;
    ns16550a.type = &ns16550a_dev_type;
    ns16550a.addr = base_addr;
    ns16550a.size = NS16550A_REG_SIZE;
    ns16550a.data = ptr;
    rvvm_attach_mmio(machine, &ns16550a);
#ifdef USE_FDT
    struct fdt_node* uart = fdt_node_create_reg("uart", base_addr);
    fdt_node_add_prop_reg(uart, "reg", base_addr, NS16550A_REG_SIZE);
    fdt_node_add_prop_str(uart, "compatible", "ns16550a");
    fdt_node_add_prop_u32(uart, "clock-frequency", 0x2625a00);
    fdt_node_add_prop_u32(uart, "fifo-size", 16);
    fdt_node_add_prop_str(uart, "status", "okay");
    if (plic) {
        fdt_node_add_prop_u32(uart, "interrupt-parent", plic_get_phandle(plic));
        fdt_node_add_prop_u32(uart, "interrupts", irq);
    }
    fdt_node_add_child(rvvm_get_fdt_soc(machine), uart);
#endif
}

PUBLIC void ns16550a_init_auto(rvvm_machine_t* machine)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, NS16550A_DEFAULT_MMIO, NS16550A_REG_SIZE);
    if (addr == NS16550A_DEFAULT_MMIO) {
        rvvm_cmdline_append(machine, "console=ttyS");
#ifdef USE_FDT
        struct fdt_node* chosen = fdt_node_find(rvvm_get_fdt_root(machine), "chosen");
        fdt_node_add_prop_str(chosen, "stdout-path", "/soc/uart@10000000");
#endif
    }
    ns16550a_init(machine, addr, plic, plic_alloc_irq(plic));
}
