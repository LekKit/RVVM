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
//#define RVVM_USER_TEST_RISCV

// Guard this for now
#if defined(__linux__) && defined(RVVM_USER_TEST)

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include "rvvmlib.h"
#include "elf_load.h"
#include "utils.h"
#include "blk_io.h"
#include "threading.h"
#include "vma_ops.h"
#include "spinlock.h"

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
#include <sys/prctl.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/vfs.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/fsuid.h>
#include <sys/time.h>

// Put syscall headers here
#include <linux/futex.h>      /* Definition of FUTEX_* constants */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>

// Damned sys_clone()
#include <sched.h>

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
    return stack_put_mem(stack, &val, sizeof(val));
}

static char *stack_put_str(char *stack, const char *str)
{
    return stack_put_mem(stack, str, rvvm_strlen(str) + 1);
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
    stack = stack_put_u64(stack, 0);

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
    *auxp++ = /* AT_UID */    11; *auxp++ = 1000;
    *auxp++ = /* AT_EUID */   12; *auxp++ = 0;
    *auxp++ = /* AT_GID */    13; *auxp++ = 0;
    *auxp++ = /* AT_EGID */   14; *auxp++ = 0;
    *auxp++ = /* AT_HWCAP */  16; *auxp++ = 0x112d;
    *auxp++ = /* AT_CLKTCK */ 17; *auxp++ = 100;
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

// RISC-V UAPI struct definitions & conversions
struct uapi_new_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct uapi_stat {
    unsigned long   dev;
    unsigned long   ino;
    unsigned int    mode;
    unsigned int    nlink;
    unsigned int    uid;
    unsigned int    gid;
    unsigned long   rdev;
    unsigned long   pad1;
    long            size;
    int             blksize;
    int             pad2;
    long            blocks;
    long            atime;
    unsigned long   atime_nsec;
    long            mtime;
    unsigned long   mtime_nsec;
    long            ctime;
    unsigned long   ctime_nsec;
    unsigned int    unused4;
    unsigned int    unused5;
};

struct uapi_statfs64 {
    size_t   type;
    size_t   bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    struct {
        int val[2];
    } fsid;
    size_t   namelen;
    size_t   frsize;
    size_t   flags;
    size_t   spare[4];
};

struct uapi_sigaction {
    void*         handler;
    unsigned long flags;
    uint64_t      mask;
};

struct uapi_clone_args {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
};

static void uapi_stat_convert(struct uapi_stat* dst, const struct stat* src)
{
    dst->dev = src->st_dev;
    dst->ino = src->st_ino;
    dst->mode = src->st_mode;
    dst->nlink = src->st_nlink;
    dst->uid = src->st_uid;
    dst->gid = src->st_gid;
    dst->rdev = src->st_rdev;
    dst->pad1 = 0;
    dst->size = src->st_size;
    dst->blksize = src->st_blksize;
    dst->pad2 = 0;
    dst->blocks = src->st_blocks;
    dst->atime = src->st_atime;
    dst->atime_nsec = 0;
    dst->mtime = src->st_mtime;
    dst->mtime_nsec = 0;
    dst->ctime = src->st_ctime;
    dst->ctime_nsec = 0;
}

static void uapi_statfs64_convert(struct uapi_statfs64* dst, const struct statfs* src)
{
    dst->type = src->f_type;
    dst->bsize = src->f_bsize;
    dst->blocks = src->f_blocks;
    dst->bfree = src->f_bfree;
    dst->bavail = src->f_bavail;
    dst->files = src->f_files;
    dst->ffree = src->f_ffree;
    memcpy(&dst->fsid, &src->f_fsid, sizeof(dst->fsid));
    dst->namelen = src->f_namelen;
    dst->frsize = src->f_frsize;
    dst->flags = src->f_flags;
}

static void uapi_sigaction_convert(struct uapi_sigaction* dst, const struct sigaction* src)
{
    dst->handler = src->sa_handler;
    dst->flags = src->sa_flags;
    memcpy(&dst->mask, &src->sa_flags, sizeof(dst->mask));
}

static rvvm_machine_t* proc_ctx; // Emulated RVVM process context

// Return negative values on -1 error like a syscall interface does
static rvvm_addr_t errno_ret(int64_t val)
{
    if (val == -1) {
        return -errno;
    } else {
        return val;
    }
}

// Return strlen or error for functions like getcwd()
static rvvm_addr_t errno_ret_str(const char* str, size_t len)
{
    if (str == NULL) {
        return -errno;
    } else {
        return rvvm_strnlen(str, len);
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

// This is for debugging sake
static elf_desc_t elf = {
    .base = NULL,
};
static elf_desc_t interp = {
    .base = NULL,
};

static bool proc_mem_readable(const void* addr, size_t size)
{
    static int fd;
    DO_ONCE({
        fd = memfd_create("null", 0);
        if (fd < 0) rvvm_fatal("Failed to create memfd!");
    });
    return write(fd, addr, size) == (ssize_t)size;
}

#define prefix_path ""

static const char* wrap_path(char* buffer, const char* path, size_t size)
{
    if ((rvvm_strfind(path, "/usr") == path || rvvm_strfind(path, "/") == path
     || rvvm_strfind(path, "/lib") == path)
     && rvvm_strfind(path, "/sys") != path
     && rvvm_strfind(path, "/proc") != path
     && rvvm_strfind(path, "/var/tmp") != path
     && rvvm_strfind(path, "/tmp") != path
     && rvvm_strfind(path, "/dev") != path) {
         size_t prefix_len = rvvm_strlcpy(buffer, prefix_path, size);
         rvvm_strlcpy(buffer + prefix_len, path, size - prefix_len);
         return buffer;
    }
    return path;
}

static size_t unwrap_path(char* path, size_t len)
{
    if (rvvm_strfind(path, prefix_path) == path) {
        rvvm_strlcpy(path, path + rvvm_strlen(prefix_path), len);
        return rvvm_strlen(prefix_path);
    }
    return 0;
}

//static spinlock_t tcr_lock;
//static rvvm_addr_t tcr_tid;
static struct uapi_sigaction siga[64] = {0};

//#define rvvm_info(...) DO_ONCE(rvvm_info(__VA_ARGS__))

void sig_handler(int signal)
{
    rvvm_warn("Received signal %d", signal);
}

// Main execution loop (Run the user CPU, handle syscalls)
void* rvvm_user_thread(void* arg)
{
    rvvm_cpu_handle_t cpu = arg;
    char path_buf[1024] = {0};
    char path_buf1[1024] = {0};

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
                case 17: { // getcwd
                    char* buf = (char*)a0;
                    rvvm_info("sys_getcwd(%lx, %lx)", a0, a1);
                    a0 = errno_ret_str(getcwd(buf, a1), a1);
                    if ((int64_t)a0 > 0) a0 -= unwrap_path(buf, a1);
                    break;
                }
                case 19: // eventfd2
                    rvvm_info("sys_eventfd2(%lx, %lx)", a0, a1);
                    a0 = errno_ret(eventfd(a0, a1));
                    break;
                case 20: // epoll_create1
                    rvvm_info("sys_epoll_create1(%lx)", a0);
                    a0 = errno_ret(epoll_create1(a0));
                    break;
                case 21: // epoll_ctl
                    // TODO struct conversion
                    rvvm_info("sys_epoll_ctl(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(epoll_ctl(a0, a1, a2, (void*)a3));
                    break;
                case 22: // epoll_pwait
                    // TODO struct conversion
                    rvvm_info("sys_epoll_pwait(%lx, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(epoll_pwait(a0, (void*)a1, a2, a3, (const void*)a4));
                    break;
                case 23: // dup
                    rvvm_info("sys_dup(%ld)", a0);
                    a0 = errno_ret(dup(a0));
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
                    // TODO: I sure hope not many ioctl() interfaces need struct conversion...
                    rvvm_info("sys_ioctl(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(ioctl(a0, a1, a2));
                    break;
                case 32: // flock
                    rvvm_info("sys_flock(%ld, %lx)", a0, a1);
                    a0 = errno_ret(flock(a0, a1));
                    break;
                case 33: // mknodat
                    rvvm_info("sys_mknodat(%ld, %s, %lx, %lx)", a0, (const char*)a1, a2, a3);
                    a0 = errno_ret(mknodat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2, a3));
                    break;
                case 34: // mkdirat
                    rvvm_info("sys_mkdirat(%ld, %s, %lx)", a0, (const char*)a1, a2);
                    a0 = errno_ret(mkdirat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2));
                    break;
                case 35: // unlinkat
                    rvvm_info("sys_unlinkat(%ld, %s, %lx)", a0, (const char*)a1, a2);
                    a0 = errno_ret(unlinkat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2));
                    break;
                case 36: // symlinkat
                    rvvm_info("sys_symlinkat(%s, %ld, %s)", (const char*)a0, a1, (const char*)a2);
                    a0 = errno_ret(symlinkat(wrap_path(path_buf, (const char*)a0, sizeof(path_buf)), a1,
                        wrap_path(path_buf1, (const char*)a2, sizeof(path_buf1))));
                    break;
                case 43: { // statfs64
                    struct statfs stfs = {0};
                    rvvm_info("sys_statfs64(%s, %lx, %lx)", (const char*)a0, a1, a2);
                    a0 = errno_ret(statfs(wrap_path(path_buf, (const char*)a0, sizeof(path_buf)), &stfs));
                    uapi_statfs64_convert((struct uapi_statfs64*)a1, &stfs);
                    break;
                }
                case 44: { // fstatfs64
                    struct statfs stfs = {0};
                    rvvm_info("sys_fstatfs64(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(fstatfs(a0, &stfs));
                    uapi_statfs64_convert((struct uapi_statfs64*)a1, &stfs);
                    break;
                }
                case 45: // truncate64
                    rvvm_info("sys_truncate64(%s, %lx)", (const char*)a0, a1);
                    a0 = errno_ret(truncate(wrap_path(path_buf, (const char*)a0, sizeof(path_buf)), a1));
                    break;
                case 46: // ftruncate64
                    rvvm_info("sys_ftruncate64(%ld, %lx)", a0, a1);
                    a0 = errno_ret(ftruncate(a0, a1));
                    break;
                case 47: // fallocate
                    rvvm_info("sys_fallocate(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(fallocate(a0, a1, a2, a3));
                    break;
                case 48: // faccessat
                    rvvm_info("sys_faccessat(%ld, %s, %lx)", a0, (const char*)a1, a2);
                    a0 = errno_ret(faccessat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2, 0));
                    break;
                case 49: // chdir
                    rvvm_info("sys_chdir(%s)", (const char*)a0);
                    a0 = errno_ret(chdir(wrap_path(path_buf, (const char*)a0, sizeof(path_buf))));
                    break;
                case 50: // fchdir
                    rvvm_info("sys_fchdir(%ld)", a0);
                    a0 = errno_ret(fchdir(a0));
                    break;
                case 53: // fchmodat
                    rvvm_info("sys_fchmodat(%ld, %s, %lx)", a0, (const char*)a1, a2);
                    a0 = errno_ret(fchmodat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2, 0));
                    break;
                case 54: // fchownat
                    rvvm_info("sys_fchownat(%ld, %s, %lx, %lx, %lx)", a0, (const char*)a1, a2, a3, a4);
                    a0 = errno_ret(fchownat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2, a3, a4));
                    break;
                case 56: // openat
                    rvvm_info("sys_openat(%ld, %s, %lx, %lx)", a0, (const char*)a1, a2, a3);
                    a0 = errno_ret(openat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2, a3));
                    break;
                case 57: // close
                    rvvm_info("sys_close(%ld)", a0);
                    a0 = errno_ret(close(a0));
                    break;
                case 59: // pipe2
                    rvvm_info("sys_pipe2(%lx, %lx)", a0, a1);
                    a0 = errno_ret(pipe2((int*)a0, a1));
                    break;
                case 61: // getdents64
                    // TODO: struct conversion(?)
                    rvvm_info("sys_getdents64(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(syscall(SYS_getdents64, a0, a1, a2));
                    break;
                case 62: // lseek
                    //rvvm_info("sys_lseek(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(lseek(a0, a1, a2));
                    break;
                case 63: // read
                    //rvvm_info("sys_read(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(read(a0, (void*)a1, a2));
                    break;
                case 64: // write
                    rvvm_info("sys_write(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(write(a0, (const void*)a1, a2));
                    break;
                case 65: // readv
                    // TODO: struct conversion(?)
                    //rvvm_info("sys_readv(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(readv(a0, (const void*)a1, a2));
                    break;
                case 66: // writev
                    // TODO: struct conversion(?)
                    //rvvm_info("sys_writev(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(writev(a0, (const void*)a1, a2));
                    break;
                case 67: // pread64
                    //rvvm_info("sys_pread64(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(pread(a0, (void*)a1, a2, a3));
                    break;
                case 68: // pwrite64
                    //rvvm_info("sys_pwrite64(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(pwrite(a0, (const void*)a1, a2, a3));
                    break;
                case 72: // pselect6_time32
                    // TODO: struct conversion
                    rvvm_info("sys_pselect6_time32(%lx, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(pselect(a0, (void*)a1, (void*)a2, (void*)a3, (void*)a4, (void*)a5));
                    break;
                case 73: // ppoll_time32
                    // TODO: struct conversion
                    rvvm_info("sys_ppoll_time32(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret(ppoll((void*)a0, a1, (void*)a2, (void*)a3));
                    break;
                case 78: // readlinkat
                    rvvm_info("sys_readlinkat(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(readlinkat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), (char*)a2, a3));
                    break;
                case 79: { // newfstatat
                    struct stat st = {0};
                    rvvm_info("sys_newfstatat(%ld, %s, %lx, %lx)", a0, (const char*)a1, a2, a3);
                    a0 = errno_ret(fstatat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), &st, a3));
                    uapi_stat_convert((struct uapi_stat*)a2, &st);
                    break;
                }
                case 80: { // newfstat
                    struct stat st = {0};
                    rvvm_info("sys_newfstat(%ld, %lx)", a0, a1);
                    a0 = errno_ret(fstat(a0, &st));
                    uapi_stat_convert((struct uapi_stat*)a1, &st);
                    break;
                }
                case 82: // fsync
                    rvvm_info("sys_fsync(%ld)", a0);
                    a0 = errno_ret(fsync(a0));
                    break;
                case 90: // capget
                    rvvm_info("sys_capget(%lx, %lx)", a0, a1);
                    a0 = errno_ret(syscall(SYS_capget, a0, a1));
                    break;
                case 91: // capset
                    rvvm_info("sys_capset(%lx, %lx)", a0, a1);
                    a0 = errno_ret(syscall(SYS_capset, a0, a1));
                    break;
                case 93: // exit
                    rvvm_info("sys_exit(%ld)", a0);
                    //exit(a0);
                    syscall(SYS_exit, a0);
                    break;
                case 94: // exit_group
                    rvvm_info("sys_exit_group(%ld)", a0);
                    exit(a0);
                    break;
                case 96: // set_tid_address
                    rvvm_warn("sys_set_tid_address(%lx)", a0);
                    a0 = -38; // ENOSYS
                    break;
                case 98: // futex
                    rvvm_info("sys_futex(%lx, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(syscall(SYS_futex, a0, a1, a2, a3, a4, a5));
                    break;
                case 99: // set_robust_list
                    rvvm_warn("sys_set_robust_list(%lx, %lx)", a0, a1);
                    a0 = -38; // ENOSYS
                    break;
                case 103: // setitimer
                    rvvm_info("sys_setitimer(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(setitimer(a0, (const void*)a1, (void*)a2));
                    break;
                case 113: // clock_gettime
                    // TODO: struct conversion?
                    rvvm_info("sys_clock_gettime(%lx, %lx)", a0, a1);
                    a0 = errno_ret(clock_gettime(a0, (void*)a1));
                    break;
                case 115: // clock_nanosleep
                    // TODO: struct conversion?
                    rvvm_info("sys_clock_nanosleep(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(clock_nanosleep(a0, a1, (const void*)a2, (void*)a3));
                    break;
                case 129: // kill
                    rvvm_warn("sys_kill(%lx, %lx)", a0, a1);
                    a0 = errno_ret(kill(a0, a1));
                    break;
                case 130: // tkill
                    rvvm_warn("sys_tkill(%lx, %lx)", a0, a1);
                    a0 = errno_ret(tgkill(getpid(), a0, a1));
                    break;
                case 134: { // rt_sigaction
                    struct sigaction sa = {0};
                    rvvm_info("sys_rt_sigaction(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    if (a0 < STATIC_ARRAY_SIZE(siga)) {
                        if (a2) memcpy((void*)a2, &siga[a0], a3);
                        if (a1) {
                            memcpy(&siga[a0], (const void*)a1, a3);

                            // Register a shim signal handler
                            if (a0 != 11) {
                                memcpy(&sa.sa_mask, &siga[a0].mask, 8);
                                sa.sa_flags = siga[a0].flags & ~SA_SIGINFO;
                                sa.sa_handler = siga[a0].handler;
                                if (sa.sa_handler != SIG_DFL && sa.sa_handler != SIG_IGN) {
                                    sa.sa_handler = sig_handler;
                                }
                                sigaction(a0, &sa, NULL);
                            }
                        }
                        a0 = 0;
                    } else {
                        a0 = -EINVAL;
                    }
                    break;
                }
                case 135: // rt_sigprocmask
                    rvvm_info("sys_rt_sigprocmask(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(sigprocmask(a0, (const void*)a1, (void*)a2));
                    break;
                case 144: // setgid
                    rvvm_info("sys_setgid(%lx)", a0);
                    a0 = errno_ret(setgid(a0));
                    break;
                case 146: // setuid
                    rvvm_info("sys_setuid(%lx)", a0);
                    a0 = errno_ret(setuid(a0));
                    break;
                case 149: // setresgid
                    rvvm_info("sys_setresgid(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(setresgid(a0, a1, a2));
                    break;
                case 151: // setfsuid
                    rvvm_info("sys_setfsuid(%lx)", a0);
                    a0 = errno_ret(setfsuid(a0));
                    break;
                case 152: // setfsgid
                    rvvm_info("sys_setfsgid(%lx)", a0);
                    a0 = errno_ret(setfsgid(a0));
                    break;
                case 153: // times
                    // TODO: struct conversion(?)
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
                        struct uapi_new_utsname name = {
                            .sysname = "Linux",
                            .nodename = "rvvm-user",
                            .release = "6.6.6",
                            .version = "RVVM " RVVM_VERSION,
                            .machine = "riscv64",
                        };
                        memcpy((void*)a0, &name, sizeof(name));
                        a0 = 0;
                    }
                    break;
                case 165: // getrusage
                    rvvm_info("sys_getrusage(%lx, %lx)", a0, a1);
                    a0 = errno_ret(getrusage(a0, (void*)a1));
                    break;
                case 166: // umask
                    rvvm_info("sys_umask(%lx)", a0);
                    a0 = errno_ret(umask(a0));
                    break;
                case 167: // prctl
                    rvvm_info("sys_prctl(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret(prctl(a0, a1, a2, a3, a4));
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
                case 177: // getegid
                    rvvm_info("sys_getegid()");
                    a0 = errno_ret(getegid());
                    break;
                case 178: // gettid
                    rvvm_info("sys_gettid()");
                    a0 = errno_ret(gettid());
                    break;
                case 179: // sysinfo
                    // TODO: struct conversion(?)
                    rvvm_info("sys_sysinfo(%lx)", a0);
                    a0 = errno_ret(sysinfo((void*)a0));
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
                case 198: // socket
                    rvvm_info("sys_socket(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(socket(a0, a1, a2));
                    break;
                case 199: // socketpair
                    rvvm_info("sys_socketpair(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(socketpair(a0, a1, a2, (int*)a3));
                    break;
                case 200: // bind
                    // TODO struct conversion
                    rvvm_info("sys_bind(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(bind(a0, (void*)a1, a2));
                    break;
                case 201: // listen
                    rvvm_info("sys_listen(%ld, %lx)", a0, a1);
                    a0 = errno_ret(listen(a0, a1));
                    break;
                case 202: // accept
                    // TODO: struct conversion(?)
                    rvvm_info("sys_accept(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(accept(a0, (void*)a1, (void*)a2));
                    break;
                case 203: // connect
                    // TODO: struct conversion(?)
                    rvvm_info("sys_connect(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(connect(a0, (void*)a1, a2));
                    break;
                case 204: // getsockname
                    // TODO: struct conversion(?)
                    rvvm_info("sys_getsockname(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(getsockname(a0, (void*)a1, (void*)a2));
                    break;
                case 205: // getpeername
                    // TODO: struct conversion(?)
                    rvvm_info("sys_getpeername(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(getpeername(a0, (void*)a1, (void*)a2));
                    break;
                case 206: // sendto
                    // TODO: struct conversion(?)
                    rvvm_info("sys_sendto(%ld, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(sendto(a0, (const void*)a1, a2, a3, (void*)a4, a5));
                    break;
                case 207: // recvfrom
                    // TODO: struct conversion(?)
                    rvvm_info("sys_recvfrom(%ld, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(recvfrom(a0, (void*)a1, a2, a3, (void*)a4, (void*)a5));
                    break;
                case 208: // setsockopt
                    rvvm_info("sys_setsockopt(%ld, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret(setsockopt(a0, a1, a2, (void*)a3, a4));
                    break;
                case 209: // getsockopt
                    rvvm_info("sys_getsockopt(%ld, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret(getsockopt(a0, a1, a2, (void*)a3, (void*)a4));
                    break;
                case 210: // shutdown
                    rvvm_info("sys_shutdown(%ld, %lx)", a0, a1);
                    a0 = errno_ret(shutdown(a0, a1));
                    break;
                case 212: // recvmsg
                    // TODO: struct conversion(?)
                    rvvm_info("sys_recvmsg(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(recvmsg(a0, (void*)a1, a2));
                    break;
                case 214: // brk
                    rvvm_info("sys_brk(%lx)", a0);
                    a0 = (size_t)emulated_brk((void*)a0);
                    break;
                case 215: // munmap
                    rvvm_info("sys_munmap(%lx, %lx)", a0, a1);
                    a0 = errno_ret(munmap((void*)a0, a1));
                    break;
                case 216: // mremap
                    rvvm_info("sys_mremap(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret((size_t)mremap((void*)a0, a1, a2, a3, (void*)a4));
                    break;
                case 220: // clone
                    rvvm_warn("sys_clone(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    //long clone(unsigned long flags, void *stack,
                    // int *parent_tid, unsigned long tls,
                    // int *child_tid);
                    if ((a0 & 0x00010100) == 0x00010100) {
#if 1
                        // CLONE_THREAD | CLONE_VM (Aka fork for threads)
                        // TODO this is spectacularly broken I guess
                        rvvm_cpu_handle_t thread = rvvm_create_user_thread(proc_ctx);
                        for (size_t i=1; i<32; ++i) {
                            rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + i, rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + i));
                        }
                        for (size_t i=0; i<32; ++i) {
                            rvvm_write_cpu_reg(thread, RVVM_REGID_F0 + i, rvvm_read_cpu_reg(cpu, RVVM_REGID_F0 + i));
                        }
                        rvvm_write_cpu_reg(thread, RVVM_REGID_PC, rvvm_read_cpu_reg(cpu, RVVM_REGID_PC) + 4);
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 2, a1); // sp
                        if (a0 & CLONE_SETTLS) rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 4, a3); // tp
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 10, 0); // a0

                        //thread_detach(thread_create(rvvm_user_thread, thread));
                        uint8_t* new_host_stack = safe_new_arr(uint8_t, 0x100000) + 0x100000;
                        a0 = errno_ret(clone((void*)rvvm_user_thread, new_host_stack,
                                   a0 & ~CLONE_SETTLS, thread, a2, NULL, a4));
#else
                        a0 = -38;
#endif
                    } else {
                        // This should be OK(?)
                        rvvm_warn("sys_fork()");
                        a0 = errno_ret(fork());
                    }
                    break;
                case 221: { // execve
                    rvvm_warn("sys_execve(%lx, %lx, %lx)", a0, a1, a2);
                    if (access(wrap_path(path_buf, (const char*)a0, sizeof(path_buf)), F_OK)) {
                        a0 = -ENOENT;
                        break;
                    }
                    char** orig_argv = (void*)a1;
                    char* new_argv[256] = {"/proc/self/exe", "-user", 0};
                    for (size_t i=2; i<255 && orig_argv[i - 2]; ++i) new_argv[i] = orig_argv[i - 2];
                    new_argv[2] = (char*)a0;
                    a0 = errno_ret(execve("/proc/self/exe", new_argv, (void*)a2));
                    break;
                }
                case 222: // mmap
                    rvvm_info("sys_mmap(%lx, %lx, %lx, %lx, %lx, %lx)",
                            a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret((size_t)mmap((void*)a0, a1, a2, a3, a4, a5));
                    break;
                case 226: // mprotect
                    rvvm_info("sys_mprotect(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(mprotect((void*)a0, a1, a2));
                    break;
                case 233: // madvise
                    rvvm_info("sys_madvise(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(madvise((void*)a0, a1, a2));
                    break;
                case 242: // accept4
                    // TODO: struct conversion(?)
                    rvvm_info("sys_accept4(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(accept4(a0, (void*)a1, (void*)a2, a3));
                    break;
                case 259: // riscv_flush_icache
                    rvvm_info("riscv_flush_icache(%lx, %lx, %lx)", a0, a1, a2);
                    rvvm_flush_icache(proc_ctx, a0, a1 - a0);
                    a0 = 0;
                    break;
                case 260: // wait4
                    // TODO: struct conversion(?)
                    rvvm_info("sys_wait4(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(wait4(a0, (void*)a1, a2, (void*)a3));
                    break;
                case 261: // prlimit64
                    // TODO: struct conversion(?)
                    rvvm_info("sys_prlimit64(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(prlimit(a0, a1, (const void*)a2, (void*)a3));
                    break;
                case 269: // sendmmsg
                    rvvm_info("sys_sendmmsg(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(sendmmsg(a0, (void*)a1, a2, a3));
                    break;
                case 276: // renameat2
                    rvvm_info("sys_renameat2(%ld, %s, %ld, %s, %lx)", a0, (const char*)a1, a2, (const char*)a3, a4);
                    a0 = errno_ret(renameat2(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)),
                                             a2, wrap_path(path_buf1, (const char*)a3, sizeof(path_buf1)), a4));
                    break;
                case 278: // getrandom
                    rvvm_info("sys_getrandom(%lx, %lx, %lx)", a0, a1, a2);
                    rvvm_randombytes((char*)a0, a1);
                    a0 = a1;
                    break;
                case 279: // memfd_create
                    rvvm_info("sys_memfd_create(%s, %lx)", (const char*)a0, a1);
                    a0 = errno_ret(memfd_create((const char*)a0, a1));
                    break;
#if 0
                // TODO: return TID so pthread_join works
                // For now glibc falls back to clone()
                case 435: { // clone3
                    struct uapi_clone_args* cl = (void*)a0;
                    rvvm_warn("sys_clone3(%lx, %lx)", a0, a1);
                    if ((cl->flags & 0x00010100) == 0x00010100) {
                        // CLONE_VM (Aka fork for threads)
                        // TODO this is spectacularly broken I guess
                        rvvm_cpu_handle_t thread = rvvm_create_user_thread(proc_ctx);
                        for (size_t i=1; i<32; ++i) {
                            rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + i, rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + i));
                        }
                        for (size_t i=0; i<32; ++i) {
                            rvvm_write_cpu_reg(thread, RVVM_REGID_F0 + i, rvvm_read_cpu_reg(cpu, RVVM_REGID_F0 + i));
                        }
                        rvvm_write_cpu_reg(thread, RVVM_REGID_PC, rvvm_read_cpu_reg(cpu, RVVM_REGID_PC) + 4);
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 2, cl->stack + cl->stack_size); // sp
                        if (cl->flags & 0x80000) rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 4, cl->tls); // tp
                        rvvm_write_cpu_reg(thread, RVVM_REGID_X0 + 10, 0); // a0
                        spin_lock(&tcr_lock);
                        thread_detach(thread_create(rvvm_user_thread, thread));

                        while(!tcr_tid);
                        a0 = tcr_tid;
                        tcr_tid = 0;
                        spin_unlock(&tcr_lock);
                    } else {
                        // This should be OK(?)
                        rvvm_warn("sys_fork()");
                        a0 = errno_ret(fork());
                    }
                    break;
                }
#endif
                case 439: // faccessat2
                    rvvm_info("sys_faccessat2(%ld, %s, %lx, %lx)", a0, (const char*)a1, a2, a3);
                    a0 = errno_ret(faccessat(a0, wrap_path(path_buf, (const char*)a1, sizeof(path_buf)), a2, a3));
                    break;
                default:
                    rvvm_error("Unknown syscall %ld!", a7);
                    a0 = -38; // ENOSYS
                    break;
            }
            rvvm_info("  -> %lx", a0);
            rvvm_write_cpu_reg(cpu, RVVM_REGID_X0 + 10, a0);
            rvvm_write_cpu_reg(cpu, RVVM_REGID_PC, rvvm_read_cpu_reg(cpu, RVVM_REGID_PC) + 4);
        } else {
            // Boom!
            rvvm_warn("Exception %lx (tval %lx) at PC %lx, SP %lx",
                      rvvm_read_cpu_reg(cpu, RVVM_REGID_CAUSE),
                      rvvm_read_cpu_reg(cpu, RVVM_REGID_TVAL),
                      rvvm_read_cpu_reg(cpu, RVVM_REGID_PC),
                      rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 2));
            for (uint32_t i=0; i<32; ++i) {
                rvvm_warn("X%d: %016lx", i, rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + i));
            }

            rvvm_addr_t pc = rvvm_read_cpu_reg(cpu, RVVM_REGID_PC);
            rvvm_addr_t pc_al = EVAL_MAX(pc - 16, pc & ~0xFFF);

            rvvm_warn("Backtrace:");
            void **fp = NULL;
            void** next_fp = (void*)rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + 8);
            do {
                rvvm_warn(" PC %lx", pc);
                if (pc >= (size_t)elf.base && pc < (size_t)elf.base + elf.buf_size) {
                    rvvm_warn("  @ Main binary, reloc: %lx", pc - (size_t)elf.base);
                }
                if (pc >= (size_t)interp.base && pc < (size_t)interp.base + interp.buf_size) {
                    rvvm_warn("  @ Interpreter, reloc: %lx)", pc - (size_t)interp.base);
                }
                if (next_fp <= fp) break;
                if (!proc_mem_readable(fp, 8)) {
                    rvvm_warn(" * * * Frame pointer points to inaccessible memory!");
                    break;
                }
                next_fp = fp[-2];
                rvvm_warn("Next FP: %p", next_fp);
                pc = (size_t)fp[-1];
                fp = next_fp;
            } while (true);

            if (proc_mem_readable((void*)pc_al, 32)) {
                rvvm_warn("Instruction bytes around PC:");
                for (size_t i=0; i<32; ++i) {
                    printf("%02x", *(uint8_t*)(pc_al + i));
                }
                printf("\n");
                for (size_t i=0; i<32; ++i) {
                    printf("%s", (pc_al + i == pc) ? "^ " : "  ");
                }
                printf("\n");
            } else {
                rvvm_warn(" * * * PC points to inaccessible memory!");
            }
            break;
        }
    }

    rvvm_free_user_thread(cpu);
    return NULL;
}

// Jump into _start after setting up the context
static void jump_start(void* entry, void* stack_top)
{
#ifdef RVVM_USER_TEST_RISCV
    register size_t a0 __asm__("a0") = (size_t) entry;
    register size_t sp __asm__("sp") = (size_t) stack_top;

    __asm__ __volatile__(
        "jr a0;"
        :
        : "r" (a0), "r" (sp)
        :
    );
#elif defined(RVVM_USER_TEST_X86)
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
    char path_buf[1024] = {0};
    /*elf_desc_t elf = {
        .base = NULL,
    };
    elf_desc_t interp = {
        .base = NULL,
    };*/
    rvfile_t* file = rvopen(wrap_path(path_buf, argv[0], sizeof(path_buf)), 0);
    bool success = file && elf_load_file(file, &elf);
    rvclose(file);
    if (!success) {
        rvvm_error("Failed to load ELF %s", argv[0]);
        return -1;
    }
    rvvm_info("Loaded ELF %s at base %lx, entry %lx,\n%ld PHDRs at %lx",
              argv[0], (size_t)elf.base, elf.entry, elf.phnum, elf.phdr);

    if (elf.interp_path) {
        rvvm_info("ELF interpreter at %s", elf.interp_path);
        file = rvopen(wrap_path(path_buf, elf.interp_path, sizeof(path_buf)), 0);
        success = file && elf_load_file(file, &interp);
        rvclose(file);
        if (!success) {
            rvvm_error("Failed to load interpreter %s", elf.interp_path);
            return -1;
        }
        rvvm_info("Loaded interpreter %s at base %lx, entry %lx,\n%ld PHDRs at %lx",
                  elf.interp_path, (size_t)interp.base, interp.entry, interp.phnum, interp.phdr);
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
