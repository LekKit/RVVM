/*
ns16550a.c - NS16550A UART emulator code
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
                    LekKit <github.com/LekKit>

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
#include <stdio.h>

#define NS16550A_REG_SIZE 0x100

struct ns16550a_data {
    uint8_t regs[8];
    uint8_t regs_dlab[2];
};

// Read DLAB = 0
#define NS16550A_REG_RBR 0
// Write DLAB = 0
#define NS16550A_REG_THR 0
// RW DLAB = 0
#define NS16550A_REG_IER 1
// RW DLAB = 1
#define NS16550A_REG_DLL 0
#define NS16550A_REG_DLM 1
// Read DLAB = 0/1
#define NS16550A_REG_IIR 2
// Write DLAB = 0/1
#define NS16550A_REG_FCR 2
// RW DLAB = 0/1
#define NS16550A_REG_LCR 3
#define NS16550A_REG_MCR 4
#define NS16550A_REG_LSR 5
#define NS16550A_REG_MSR 6
#define NS16550A_REG_SCR 7

#define NS16550A_IER_MASK    0xF

#if defined __linux__ || defined __APPLE__
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

static bool ns16550a_mmio_read(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    uint8_t *value = (uint8_t*) memory_data;
    UNUSED(size);
    //rvvm_info("Reading UART at 0x%08x", offset);
    if (regs->regs[NS16550A_REG_LCR] & 0x80) {
        //riscv32_debug(vm, "NS16550A: DLAB = 1\n");

        switch (offset) {
            case NS16550A_REG_DLL:
            case NS16550A_REG_DLM: {
                *value = regs->regs_dlab[offset];
                break;
            }
            case NS16550A_REG_IIR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR:
            case NS16550A_REG_SCR: {
                *value = regs->regs[offset];
                break;
            }
            default: {
                //riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }
    } else {
        //riscv32_debug(vm, "NS16550A: DLAB = 0\n");

        switch (offset) {
            case NS16550A_REG_RBR: {
                *value = regs->regs[offset];
                break;
            }
            case NS16550A_REG_IER: {
                *value = regs->regs[offset];
                break;
            }
            case NS16550A_REG_IIR: {
                // Some weird bullshit going on if we are reading regs[offset] properly,
                // Linux kernel does not show userspace stdout in tty...
                *value = 0;
                break;
            }
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_LSR: {
                // Read char from stdin
                *value = regs->regs[offset] | terminal_readchar(regs->regs + NS16550A_REG_RBR);
                //riscv32_debug(vm, "NS16550A: Unimplemented LSR\n");
                break;
            }
            case NS16550A_REG_MSR:
            case NS16550A_REG_SCR: {
                *value = regs->regs[offset];
                break;
            }
            default: {
                //riscv32_debug(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }
    }

    return true;
}

static bool ns16550a_mmio_write(rvvm_mmio_dev_t* device, void* memory_data, paddr_t offset, uint8_t size)
{
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    uint8_t value = *(uint8_t*) memory_data;
    UNUSED(size);
    //rvvm_info("Writing to UART at 0x%08x", offset);
    if (regs->regs[NS16550A_REG_LCR] & 0x80) {
        switch (offset) {
            case NS16550A_REG_DLL:
            case NS16550A_REG_DLM: {
                regs->regs_dlab[offset] = value;
                break;
            }
            case NS16550A_REG_FCR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_SCR: {
                regs->regs[offset] = value;
                break;
            }
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR: {
                // Registers are RO
                break;
            }
            default: {
                //riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }

    } else {
        switch (offset) {
            case NS16550A_REG_THR: {
                //if (value > 127) printf ("we are retarded\n");
                //rvvm_info("UART prints 0x%02x", value);
                putc(value, stdout);
                fflush(stdout);
                break;
            }
            case NS16550A_REG_IER: {
                regs->regs[offset] = value & NS16550A_IER_MASK;
                break;
            }
            case NS16550A_REG_FCR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_SCR: {
                regs->regs[offset] = value;
                break;
            }
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR: {
                // Registers are RO
                break;
            }
            default: {
                //riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }
    }
    return true;
}

static rvvm_mmio_type_t ns16550a_dev_type = {
    .name = "ns16550a",
};

void ns16550a_init(rvvm_machine_t* machine, paddr_t base_addr)
{
    struct ns16550a_data* ptr = safe_calloc(sizeof(struct ns16550a_data), 1);
    terminal_rawmode();
    ptr->regs[NS16550A_REG_LSR] = 0x60;

    rvvm_mmio_dev_t ns16550a = {0};
    ns16550a.min_op_size = 1;
    ns16550a.max_op_size = 1;
    ns16550a.read = ns16550a_mmio_read;
    ns16550a.write = ns16550a_mmio_write;
    ns16550a.type = &ns16550a_dev_type;
    ns16550a.begin = base_addr;
    ns16550a.end = base_addr + NS16550A_REG_SIZE;
    ns16550a.data = ptr;
    rvvm_attach_mmio(machine, &ns16550a);
    
#ifdef USE_FDT
    struct fdt_node* soc = fdt_node_find(machine->fdt, "soc");
    if (soc == NULL) {
        rvvm_warn("Missing soc node in FDT!");
        return;
    }
    
    struct fdt_node* uart = fdt_node_create_reg("uart", base_addr);
    fdt_node_add_prop_reg(uart, "reg", base_addr, NS16550A_REG_SIZE);
    fdt_node_add_prop_str(uart, "compatible", "ns16550a");
    fdt_node_add_prop_u32(uart, "clock-frequency", 0x2625a00);
    fdt_node_add_prop_u32(uart, "fifo-size", 0x80);
    fdt_node_add_prop_str(uart, "status", "okay");
    fdt_node_add_child(soc, uart);
#endif
}
