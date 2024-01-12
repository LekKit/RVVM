/*
rvvm_user.c - RVVM Userland binary emulator
Copyright (C) 2023  LekKit <github.com/LekKit>
                    nebulka1

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

/*
 * This thing is hugely WIP, altho it runs static binaries fairly well.
 * With dynamic binaries, it usually either crashes in ld-linux.so, or
 * in random executable locations... Debugging this is a nightmare.
 *
 * Upon debugging this thing, I figured it would be a good idea to remove
 * CPU/syscall emulation out of the equation, so simply test a combination
 * of ELF loader + stack setup thingy. So there is a native x86_64 jump_start()
 * implementation to do exactly that
 *
 * Some helpful resources:
 *   https://jborza.com/post/2021-05-11-riscv-linux-syscalls/
 *
 * Now if we ever get this to work, here are some interesting further goals:
 *
 * - Implement some kind of fake /usr overlay so you can still run RISC-V
 *   binaries without chroot, and without putting RISC-V libs into your system
 *
 * - Allow to run Linux binaries on non-Linux host to some degree. The ELF
 *   loader runs even on Windows (lol), so do the rest of RVVM abstractions.
 *   So maybe for many simple syscalls we can do exactly that, at least on Mac/BSD..
 *
 * - Ask some guys that use qemu-user for build system purposes to try out rvvm-user :D
 *   With some local JIT patches rvvm-user already beats qemu-user on statically
 *   built benches
 */

//#define RVVM_USER_TEST
//#define RVVM_USER_TEST_X86

// Guard this for now
#if defined(__linux__) && defined(RVVM_USER_TEST)

#define _GNU_SOURCE

#include "rvvmlib.h"
#include "elf_load.h"
#include "utils.h"
#include "blk_io.h"
#include "threading.h"

#include <string.h>
#include <stdio.h>

#include <errno.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <sys/random.h>
#include <time.h>
#include <sys/times.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

// Describes the executable to be ran
typedef struct {
    // Self explanatory
    size_t argc;
    const char** argv;
    const char** envp;

    size_t base;         // Main ELF base address (relocation)
    size_t entry;        // Main ELF entry point
    size_t interp_base;  // ELF interpreter (aka linker usually) base address
    size_t interp_entry; // ELF interpreter entry point
    size_t phdr;         // Address of ELF PHDR section
    size_t phnum;        // Number of PHDRs
} exec_desc_t;

// Guest process stack setup routines
static char *stack_put_mem(char *stack, const void *mem, size_t len)
{
    stack -= len;
    memcpy(stack, mem, len);
    return stack;
}

static void *stack_put_u64(char *stack, uint64_t val)
{
    return stack_put_mem(stack, &val, sizeof val);
}

static char *stack_put_str(char *stack, const char *str)
{
    return stack_put_mem(stack, str, strlen(str) + 1);
}

static char *init_stack(char *stack, exec_desc_t* desc)
{
    /* Stack layout (upside down):
     * 1. argc (8 bytes)
     * 2. string pointers: argv, 0, envp, 0
     * 3. auxv
     * 4. padding
     * 5. random bytes (16)
     * 6. string data: argv, envp
     * 7. string data: execfn
     * 8. null (8 bytes)
     */

    // 8. null
    stack_put_u64(stack, 0);

    // 7. string data: execfn
    char *execfn = stack = stack_put_str(stack, desc->argv[0]);

    // 6. string data: argv, envp
    size_t envc = 0;
    while (desc->envp[envc]) envc++;
    char **string_ptrs = safe_calloc(desc->argc + envc + 2, sizeof(char *));
    string_ptrs[desc->argc + envc + 1] = 0;
    for (size_t i = envc - 1; i < envc; i--)
        string_ptrs[desc->argc + 1 + i] = stack = stack_put_str(stack, desc->envp[i]);
    string_ptrs[desc->argc] = 0;
    for (size_t i = desc->argc - 1; i < desc->argc; i--)
        string_ptrs[i] = stack = stack_put_str(stack, desc->argv[i]);

    // 5. random bytes
    char *random_bytes = (stack -= 16);
    rvvm_randombytes(random_bytes, 16);

    // 4. align to 16 bytes
    stack = (char *)(((size_t)stack) & ~0xFULL);

    // 3. auxv, then null
    uint64_t auxv[32 * 2], *auxp = auxv;
    *auxp++ = /* AT_PHDR */    3; *auxp++ = desc->phdr;
    *auxp++ = /* AT_PHENT */   4; *auxp++ = 56;
    *auxp++ = /* AT_PHNUM */   5; *auxp++ = desc->phnum;
    *auxp++ = /* AT_PAGESZ */  6; *auxp++ = 0x1000;
    *auxp++ = /* AT_BASE */    7; *auxp++ = desc->interp_base;
    *auxp++ = /* AT_FLAGS */   8; *auxp++ = 0;
    *auxp++ = /* AT_ENTRY */   9; *auxp++ = desc->entry;
    *auxp++ = /* AT_UID */    11; *auxp++ = 0;
    *auxp++ = /* AT_EUID */   12; *auxp++ = 0;
    *auxp++ = /* AT_GID */    13; *auxp++ = 0;
    *auxp++ = /* AT_EGID */   14; *auxp++ = 0;
    *auxp++ = /* AT_RANDOM */ 25; *auxp++ = (size_t)random_bytes;
    *auxp++ = /* AT_EXECFN */ 31; *auxp++ = (size_t)execfn;
    *auxp++ = /* AT_NULL */    0; *auxp++ = 0;
    stack = stack_put_mem(stack, auxv, (auxp - auxv) * sizeof(uint64_t));

    // 2. string pointers
    stack = stack_put_mem(stack, string_ptrs, (desc->argc + envc + 2) * sizeof(char *));
    free(string_ptrs);

    // 1. argc
    stack = stack_put_u64(stack, desc->argc);

    return stack;
}

struct new_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static rvvm_machine_t* proc_ctx; // Emulated RVVM process context

// Return negative values on error like a syscall interface does
static rvvm_addr_t errno_ret(rvvm_addr_t val)
{
    if ((int64_t)val == -1) {
        return -errno;
    } else {
        return val;
    }
}

#define BRK_HEAP_SIZE 0x1000000

// We can't touch the native brk heap since it would likely blow up the process
static void* emulated_brk(void* addr)
{
    static uint8_t* brk_buffer;
    static uint8_t* brk_ptr;
    DO_ONCE({
        brk_buffer = safe_calloc(BRK_HEAP_SIZE, 1);
        brk_ptr = brk_buffer;
    });
    if ((uint8_t*)addr >= brk_buffer && (uint8_t*)addr < brk_buffer + BRK_HEAP_SIZE) {
        brk_ptr = addr;
    }
    return brk_ptr;
}

// Main execution loop (Run the user CPU, handle syscalls)
void* rvvm_user_thread(void* arg)
{
    rvvm_cpu_handle_t cpu = arg;

    while (true) {
        rvvm_addr_t cause = rvvm_run_user_thread(cpu);
        if (cause == 8) {
            // Handle syscall trap
            rvvm_addr_t a0 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 10);
            rvvm_addr_t a1 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 11);
            rvvm_addr_t a2 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 12);
            rvvm_addr_t a3 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 13);
            rvvm_addr_t a4 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 14);
            rvvm_addr_t a5 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 15);
            rvvm_addr_t a7 = rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 17);
            switch (a7) {
                case 17: // getcwd
                    rvvm_info("sys_getcwd(%lx, %lx)", a0, a1);
                    a0 = errno_ret((size_t)getcwd((void*)a0, a1));
                    break;
                case 24: // dup3
                    rvvm_info("sys_dup3(%ld, %ld, %lx)", a0, a1, a2);
                    a0 = errno_ret(dup3(a0, a1, a2));
                    break;
                case 25: // fcntl64
                    rvvm_info("sys_fcntl64(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(fcntl(a0, a1, a2));
                    break;
                case 29: // ioctl
                    rvvm_info("sys_ioctl(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(ioctl(a0, a1, a2));
                    break;
                case 48: // faccessat
                    rvvm_info("sys_faccessat(%ld, %s, %lx)", a0, (const char*)a1, a2);
                    a0 = errno_ret(faccessat(a0, (const char*)a1, a2, 0));
                    break;
                case 49: // chdir
                    rvvm_info("sys_chdir(%s)", (const char*)a0);
                    a0 = errno_ret(chdir((const char*)a0));
                    break;
                case 56: // openat
                    rvvm_info("sys_openat(%ld, %s, %lx, %lx)", a0, (const char*)a1, a2, a3);
                    a0 = errno_ret(openat(a0, (const char*)a1, a2, a3));
                    break;
                case 57: // close
                    rvvm_info("sys_close(%ld)", a0);
                    a0 = errno_ret(close(a0));
                    break;
                case 59: // pipe2
                    rvvm_info("sys_pipe2(%lx, %lx)", a0, a1);
                    a0 = errno_ret(pipe2((void*)a0, a1));
                    break;
                /*case 61: // getdents64
                    rvvm_info("sys_getdents64(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret()
                    break;*/
                case 62: // lseek
                    rvvm_info("sys_lseek(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(lseek(a0, a1, a2));
                    break;
                case 63: // read
                    rvvm_info("sys_read(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(read(a0, (void*)a1, a2));
                    break;
                case 64: // write
                    rvvm_info("sys_write(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(write(a0, (const void*)a1, a2));
                    break;
                case 65: // readv
                    rvvm_info("sys_readv(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(readv(a0, (const struct iovec*)a1, a2));
                    break;
                case 66: // writev
                    rvvm_info("sys_writev(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(writev(a0, (const struct iovec*)a1, a2));
                    break;
                case 73: // ppoll_time32
                    rvvm_info("sys_ppoll_time32(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = ppoll((void*)a0, a1, (void*)a2, (void*)a3);
                    break;
                case 78: // readlinkat
                    rvvm_info("sys_readlinkat(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(readlinkat(a0, (const char*)a1, (char*)a2, a3));
                    break;
                case 79: // newfstatat
                    rvvm_info("sys_newfstatat(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(fstatat(a0, (const char*)a1, (void*)a2, a3));
                    break;
                case 80: // newfstat
                    rvvm_info("sys_newfstatat(%ld, %lx)", a0, a1);
                    a0 = errno_ret(fstat(a0, (void*)a1));
                    break;
                case 93: // exit
                    rvvm_info("sys_exit(%ld)", a0);
                    exit(a0);
                    break;
                case 94: // exit_group
                    rvvm_info("sys_exit_group(%ld)", a0);
                    exit(a0);
                    break;
                case 96: // set_tid_address
                    rvvm_warn("sys_set_tid_address(%lx)", a0);
                    a0 = -38; // ENOSYS
                    break;
                case 99: // set_robust_list
                    rvvm_warn("sys_set_robust_list(%lx, %lx)", a0, a1);
                    a0 = -38; // ENOSYS
                    break;
                case 113: // clock_gettime
                    rvvm_info("sys_clock_gettime(%lx, %lx)", a0, a1);
                    a0 = errno_ret(clock_gettime(a0, (void*)a1));
                    break;
                case 115: // clock_nanosleep
                    //rvvm_info("sys_clock_nanosleep(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(clock_nanosleep(a0, a1, (const void*)a2, (void*)a3));
                    break;
                case 129: // kill
                    rvvm_info("sys_kill(%lx, %lx)", a0, a1);
                    a0 = errno_ret(kill(a0, a1));
                    break;
                case 134: // rt_sigaction
                    // TODO: segv with this thing
                    rvvm_info("sys_rt_sigaction(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = 0;//errno_ret(sigaction(a0, (const void*)a1, (void*)a2));
                    break;
                case 135: // rt_sigprocmask
                    // TODO: ^
                    rvvm_info("sys_rt_sigprocmask(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = 0;//errno_ret(sigprocmask(a0, (const void*)a1, (void*)a2));
                    break;
                case 144: // setgid
                    rvvm_info("sys_setgid(%lx)", a0);
                    a0 = errno_ret(setgid(a0));
                    break;
                case 146: // setuid
                    rvvm_info("sys_setuid(%lx)", a0);
                    a0 = errno_ret(setuid(a0));
                    break;
                case 153: // times
                    rvvm_info("sys_times(%lx)", a0);
                    a0 = errno_ret(times((void*)a0));
                    break;
                case 154: // setpgid
                    rvvm_info("sys_setpgid(%lx, %lx)", a0, a1);
                    a0 = errno_ret(setpgid(a0, a1));
                    break;
                case 155: // getpgid
                    rvvm_info("sys_getpgid(%lx)", a0);
                    a0 = errno_ret(getpgid(a0));
                    break;
                case 160: // newuname
                    rvvm_info("sys_newuname(%lx)", a0);
                    if (a0) {
                        // Just lie about the host details
                        struct new_utsname name = {
                            .sysname = "Linux",
                            .nodename = "rvvm-user",
                            .release = "6.6.6",
                            .version = "RVVM "RVVM_VERSION,
                            .machine = "riscv64",
                        };
                        memcpy((void*)a0, &name, sizeof(name));
                        a0 = 0;
                    }
                    break;
                case 172: // getpid
                    rvvm_info("sys_getpid()");
                    a0 = errno_ret(getpid());
                    break;
                case 173: // getppid
                    rvvm_info("sys_getppid()");
                    a0 = errno_ret(getppid());
                    break;
                case 174: // getuid
                    rvvm_info("sys_getuid()");
                    a0 = errno_ret(getuid());
                    break;
                case 175: // geteuid
                    rvvm_info("sys_geteuid()");
                    a0 = errno_ret(geteuid());
                    break;
                case 176: // getgid
                    rvvm_info("sys_getgid()");
                    a0 = errno_ret(getgid());
                    break;
                case 178: // gettid
                    rvvm_info("sys_gettid()");
                    a0 = errno_ret(gettid());
                    break;
                case 194: // shmget
                    rvvm_info("sys_shmget(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(shmget(a0, a1, a2));
                    break;
                case 196: // shmat
                    rvvm_info("sys_shmat(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret((size_t)shmat(a0, (void*)a1, a2));
                    break;
                case 197: // shmdt
                    rvvm_info("sys_shmdt(%lx)", a0);
                    a0 = errno_ret(shmdt((void*)a0));
                    break;
                case 214: // brk
                    rvvm_info("sys_brk(%lx)", a0);
                    a0 = (size_t)emulated_brk((void*)a0);
                    break;
                case 215: // munmap
                    rvvm_info("sys_munmap(%lx, %lx)", a0, a1);
                    a0 = errno_ret(munmap((void*)a0, a1));
                    break;
                case 220: // clone
                    if (a0 & 0x100) {
                        // CLONE_VM (Aka fork for threads)
                        // TODO this is spectacularly broken I guess
                        rvvm_cpu_handle_t thread = rvvm_create_user_thread(proc_ctx);
                        rvvm_warn("sys_clone(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                        rvvm_write_cpu_reg(thread, RVVM_REGID_PC, rvvm_read_cpu_reg(cpu, RVVM_REGID_PC));
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 2, a1); // sp
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 4, a4); // tp
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 10, 0); // a0
                        thread_detach(thread_create(rvvm_user_thread, thread));
                        a0 = 1;
                    } else {
                        // This should be OK(?)
                        rvvm_warn("sys_fork()");
                        a0 = fork();
                    }
                    break;
                case 222: // mmap
                    rvvm_info("sys_mmap(%lx, %lx, %lx, %lx, %lx, %lx)",
                            a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret((size_t)mmap((void*)a0, a1, a2, a3, a4, a5));
                    break;
                case 226: // mprotect
                    rvvm_info("sys_mprotect(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(mprotect((void*)a0, a1, a2));
                    break;
                case 260: // wait4
                    rvvm_info("sys_wait4(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(wait4(a0, (void*)a1, a2, (void*)a3));
                    break;
                case 261: // prlimit64
                    rvvm_info("sys_prlimit64(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = 0;
                    break;
                case 278: // getrandom
                    rvvm_info("sys_getrandom(%lx, %lx, %lx)", a0, a1, a2);
                    rvvm_randombytes((char*)a0, a1);
                    a0 = 0;
                    break;
                default:
                    rvvm_warn("Unknown syscall %ld!", a7);
                    a0 = -38; // ENOSYS
                    break;
            }
            rvvm_info("  -> %lx", a0);
            rvvm_write_cpu_reg(cpu, RVVM_REGID_X0 + 10, a0);
        } else {
            uint8_t* pc = (uint8_t*)(size_t)rvvm_read_cpu_reg(cpu, RVVM_REGID_PC);
            rvvm_warn("Exception at PC %p", pc);
            rvvm_warn("SP: %lx", rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 2));
            rvvm_warn("CAUSE: %lx", rvvm_read_cpu_reg(cpu, RVVM_REGID_CAUSE));
            rvvm_warn("TVAL: %lx", rvvm_read_cpu_reg(cpu, RVVM_REGID_TVAL));
            rvvm_warn("insn bytes: %02x%02x%02x%02x", pc[0], pc[1], pc[2], pc[3]);
            break;
        }
    }
    return NULL;
}

// Jump into _start after setting up the context
static void jump_start(void* entry, void* stack_top)
{
#ifdef RVVM_USER_TEST_X86
    register size_t rax __asm__("rax") = (size_t) entry;
    register size_t rsp __asm__("rsp") = (size_t) stack_top;
    register size_t rdx __asm__("rdx") = (size_t) &exit; // Why do we even need to pass this?

    __asm__ __volatile__(
        "jmp *%0;"
        :
        : "r" (rax), "r" (rsp), "r" (rdx)
        :
    );
#else
    proc_ctx = rvvm_create_userland(true);
    rvvm_cpu_handle_t cpu = rvvm_create_user_thread(proc_ctx);

    rvvm_write_cpu_reg(cpu, RVVM_REGID_X0 + 2, (size_t)stack_top);
    rvvm_write_cpu_reg(cpu, RVVM_REGID_PC,     (size_t)entry);

    rvvm_user_thread(cpu);

    rvvm_free_machine(proc_ctx);
#endif
}

int rvvm_user(int argc, const char** argv, const char** envp)
{
    elf_desc_t elf = {
        .base = NULL,
    };
    elf_desc_t interp = {
        .base = NULL,
    };
    rvfile_t* file = rvopen(argv[0], 0);
    bool success = file && elf_load_file(file, &elf);
    rvclose(file);
    if (!success) {
        rvvm_error("Failed to load ELF %s", argv[0]);
        return -1;
    }
    rvvm_info("Loaded ELF %s at base %lx, entry %lx",
              argv[0], (size_t)elf.base, elf.entry);

    if (elf.interp_path) {
        rvvm_info("ELF interpreter at %s", elf.interp_path);
        file = rvopen(elf.interp_path, 0);
        success = file && elf_load_file(file, &interp);
        rvclose(file);
        if (!success) {
            rvvm_error("Failed to load interpreter %s", elf.interp_path);
            return -1;
        }
        rvvm_info("Loaded interpreter %s at base %lx, entry %lx",
                  elf.interp_path, (size_t)interp.base, interp.entry);
    }

    exec_desc_t desc = {
        .argc = argc,
        .argv = argv,
        .envp = envp,
        .base = (size_t)elf.base,
        .entry = elf.entry,
        .interp_base = (size_t)interp.base,
        .interp_entry = interp.entry,
        .phdr = elf.phdr,
        .phnum = elf.phnum,
    };

    #define STACK_SIZE 0x800000
    void* stack_buffer = safe_calloc(STACK_SIZE, 1);
    void* stack_top = (uint8_t*)stack_buffer + STACK_SIZE;
    stack_top = init_stack(stack_top, &desc);
    rvvm_info("Stack top at %p", stack_top);

    if (elf.interp_path) {
        jump_start((void*)interp.entry, stack_top);
    } else {
        jump_start((void*)elf.entry, stack_top);
    }

    free(stack_buffer);
    return 0;
}

#else

#include "utils.h"

int rvvm_user(int argc, char** argv, char** envp)
{
    UNUSED(argc); UNUSED(argv); UNUSED(envp);
    rvvm_warn("Userland emulation not available, define RVVM_USER_TEST");
    return -1;
}

#endif
