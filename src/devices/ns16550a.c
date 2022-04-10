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
    plic_ctx_t plic;
    uint32_t irq;
    spinlock_t lock;
    
    uint8_t ier;
    uint8_t iir;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t scr;
    uint8_t dll;
    uint8_t dlm;
    
    uint8_t buf;
    uint8_t len;
};

// RW DLAB = 1
#define NS16550A_REG_DLL 0
#define NS16550A_REG_DLM 1

// Read
#define NS16550A_REG_RBR 0
#define NS16550A_REG_IIR 2
// Write
#define NS16550A_REG_THR 0
#define NS16550A_REG_FCR 2
// RW
#define NS16550A_REG_IER 1
#define NS16550A_REG_LCR 3
#define NS16550A_REG_MCR 4
#define NS16550A_REG_LSR 5
#define NS16550A_REG_MSR 6
#define NS16550A_REG_SCR 7


#define NS16550A_IER_MASK  0xF
#define NS16550A_IIR_FIFO  0xC0
#define NS16550A_IIR_THR   0x2
#define NS16550A_IIR_RECV  0x4
#define NS16550A_LSR_THR   0x60
#define NS16550A_LCR_DLAB  0x80

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

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
        fcntl(STDIN_FILENO, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
        once = false;
    }
}

static uint8_t terminal_readchar(void* addr)
{
    return (read(0, addr, 1) == 1) ? 1 : 0;
}

#elif _WIN32
#include <windows.h>
#include <conio.h>

// Needs to be tested on actual windows machine
// Very likely to be broken in many ways when working
// with linux shell, needs workarounds
static void terminal_rawmode()
{
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0);
}

static uint8_t terminal_readchar(void* addr)
{
    if (_kbhit()) {
        *(uint8_t*)addr = _getch();
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
    return 0;
}
#endif

static bool ns16550a_mmio_read(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    uint8_t *value = (uint8_t*) memory_data;
    UNUSED(size);
    if (regs->lcr & NS16550A_LCR_DLAB) {
        switch (offset) {
            case NS16550A_REG_DLL:
                *value = regs->dll;
                return true;
            case NS16550A_REG_DLM:
                *value = regs->dlm;
                return true;
            default:
                break;
        }
    }
    switch (offset) {
        case NS16550A_REG_RBR:
            spin_lock(&regs->lock);
            if (regs->len) {
                *value = regs->buf;
                regs->len = 0;
            } else {
                *value = 0;
            }
            spin_unlock(&regs->lock);
            break;
        case NS16550A_REG_IER:
            *value = regs->ier;
            break;
        case NS16550A_REG_IIR:
            *value = regs->iir;
            break;
        case NS16550A_REG_LCR:
            *value = regs->lcr;
            break;
        case NS16550A_REG_MCR:
            *value = regs->mcr;
            break;
        case NS16550A_REG_LSR:
            // Read char from stdin
            spin_lock(&regs->lock);
            regs->len = regs->len ? 1 : terminal_readchar(&regs->buf);
            *value = NS16550A_LSR_THR | regs->len;
            spin_unlock(&regs->lock);
            break;
        case NS16550A_REG_MSR:
            *value = 0;
            break;
        case NS16550A_REG_SCR:
            *value = regs->scr;
            break;
        default:
            *value = 0;
            break;
    }

    return true;
}

static bool ns16550a_mmio_write(rvvm_mmio_dev_t* device, void* memory_data, size_t offset, uint8_t size)
{
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    uint8_t value = *(uint8_t*) memory_data;
    UNUSED(size);
    if (regs->lcr & NS16550A_LCR_DLAB) {
        switch (offset) {
            case NS16550A_REG_DLL:
                regs->dll = value;
                return true;
            case NS16550A_REG_DLM:
                regs->dlm = value;
                return true;
            default:
                break;
        }
    }
    switch (offset) {
        case NS16550A_REG_THR:
            putc(value, stdout);
            fflush(stdout);
            break;
        case NS16550A_REG_IER:
            //rvvm_info("NS16550A IER: 0x%x", value);
            regs->ier = value & NS16550A_IER_MASK;
            spin_lock(&regs->lock);
            if ((regs->ier & 1) && regs->len) {
                regs->iir = NS16550A_IIR_FIFO | NS16550A_IIR_RECV;
                if (regs->plic) {
                    plic_send_irq(regs->plic, regs->irq);
                }
                spin_unlock(&regs->lock);
                break;
            }
            if (regs->ier & 2) {
                regs->iir = NS16550A_IIR_FIFO | NS16550A_IIR_THR;
                if (regs->plic) {
                    plic_send_irq(regs->plic, regs->irq);
                }
            }
            spin_unlock(&regs->lock);
            break;
        case NS16550A_REG_FCR:
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
        // Registers are RO
        case NS16550A_REG_LSR:
        case NS16550A_REG_MSR:
            break;
        default:
            break;
    }
    return true;
}

static void ns16550a_update(rvvm_mmio_dev_t* device)
{
    struct ns16550a_data* regs = (struct ns16550a_data*)device->data;
    if (regs->plic) {
        spin_lock(&regs->lock);
        regs->len = regs->len ? 1 : terminal_readchar(&regs->buf);
        if (regs->len) plic_send_irq(regs->plic, regs->irq);
        spin_unlock(&regs->lock);
    }
}

static rvvm_mmio_type_t ns16550a_dev_type = {
    .name = "ns16550a",
    .update = ns16550a_update,
};

PUBLIC void ns16550a_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t plic, uint32_t irq)
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

PUBLIC void ns16550a_init_auto(rvvm_machine_t* machine, plic_ctx_t plic)
{
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
