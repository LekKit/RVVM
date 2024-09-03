/*
rvvm_user.c - RVVM Linux binary emulator
Copyright (C) 2024  LekKit <github.com/LekKit>
              2023  nebulka1

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
 *   https://gpages.juszkiewicz.com.pl/syscalls-table/syscalls.html
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

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

//#define RVVM_USER_TEST
//#define RVVM_USER_TEST_X86
//#define RVVM_USER_TEST_RISCV

// Guard this for now
#if defined(RVVM_USER_TEST)

#include <stdio.h>

#include <errno.h>
#include <unistd.h>

#include <time.h>     // clock_gettime(), etc
#include <signal.h>   // sigaction(), etc

#include <sys/types.h>
#include <sys/param.h>

#include <sys/time.h> // setitimer()

// File stuff
#include <dirent.h>    // readdir(), etc
#include <sys/file.h>  // fcntl(), flock(), fallocate(), openat()
#include <sys/uio.h>   // readv(), writev()
#include <sys/stat.h>  // mknodat(), mkdirat(), fchmod(), fchmodat(), fstatat(), fstat(), umask(), statx()
#include <sys/mount.h> // struct statfs

// VMA manipulation
#include <sys/mman.h> // mmap(), munmap(), mprotect()
#include <sys/shm.h>  // shmget(), shmctl(), shmat(), shmdt()

// Sockets
#include <sys/socket.h>
#include <sys/ioctl.h>  // ioctl()
#include <sys/select.h> // select()
#include <poll.h>       // poll()

// Misc
#include <sys/times.h>    // times()
#include <sys/wait.h>     // wait4()
#include <sys/resource.h> // getrusage()
#include <grp.h>          // setgroups()

// Linux-specific stuff
#ifdef __linux__
#include <sys/eventfd.h> // eventfd()
#include <sys/epoll.h>   // epoll_create1(), etc
#include <sys/sysinfo.h> // sysinfo()
#include <sys/fsuid.h>   // setfsuid(), setfsgid()
#include <sys/vfs.h>     // struct statfs

// Put syscall headers here
#include <linux/futex.h> // FUTEX_*
#include <sys/syscall.h> // SYS_*
#include <unistd.h>
#endif

#include "rvvmlib.h"
#include "elf_load.h"
#include "mem_ops.h"
#include "utils.h"
#include "blk_io.h"
#include "threading.h"
#include "vma_ops.h"
#include "spinlock.h"
#include "rvtimer.h"
#include "stacktrace.h"

#define RVVM_USER_RISCV64

#ifdef RVVM_USER_RISCV64
typedef uint64_t uapi_size_t;
typedef uint64_t uapi_ulong_t;
typedef int64_t  uapi_long_t;
#else
typedef uint32_t uapi_size_t;
typedef uint32_t uapi_ulong_t;
typedef int32_t  uapi_long_t;
#endif

#define UAPI_PATH_MAX 4096

#define UAPI_EPERM   1
#define UAPI_ENOENT  2
#define UAPI_EINTR   4
#define UAPI_EIO     5
#define UAPI_EBADF   9
#define UAPI_EAGAIN  11
#define UAPI_ENOMEM  12
#define UAPI_EACCESS 13
#define UAPI_EFAULT  14
#define UAPI_EBUSY   16
#define UAPI_EEXIST  17
#define UAPI_EINVAL  22
#define UAPI_ENOSYS  38

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
    uapi_ulong_t dev;
    uapi_ulong_t ino;
    uint32_t     mode;
    uint32_t     nlink;
    uint32_t     uid;
    uint32_t     gid;
    uapi_ulong_t rdev;
    uapi_ulong_t pad1;
    uapi_long_t  size;
    int32_t      blksize;
    int32_t      pad2;
    uapi_long_t  blocks;
    uapi_long_t  atime;
    uapi_ulong_t atime_nsec;
    uapi_long_t  mtime;
    uapi_ulong_t mtime_nsec;
    uapi_long_t  ctime;
    uapi_ulong_t ctime_nsec;
    uint32_t     unused4;
    uint32_t     unused5;
};

struct uapi_statfs64 {
    uapi_size_t type;
    uapi_size_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    struct {
        int32_t val[2];
    } fsid;
    uapi_size_t namelen;
    uapi_size_t frsize;
    uapi_size_t flags;
    uapi_size_t spare[4];
};

struct uapi_sigaction {
    uapi_size_t  handler;
    uapi_ulong_t mask;
    uapi_ulong_t flags;
};

struct uapi_sigaltstack {
    uapi_size_t sp;
    int32_t flags;
    uapi_size_t size;
};

struct uapi_sched_param {
    int32_t sched_priority;
};

struct uapi_cap_data_struct {
    uint32_t effective;
    uint32_t permitted;
    uint32_t inheritable;
};

union uapi_epoll_data {
    uapi_size_t ptr;
    int32_t     fd;
    uint32_t    u32;
    uint64_t    u64;
};

struct uapi_epoll_event {
    uint32_t event;
    union uapi_epoll_data data;
};

struct uapi_timeval32 {
    uapi_long_t tv_sec;
    uapi_long_t tv_usec;
};

struct uapi_timespec32 {
    uapi_long_t tv_sec;
    uapi_long_t tv_nsec;
};

struct uapi_timespec {
    uint64_t tv_sec;
    uint64_t tv_nsec;
};

struct uapi_pollfd {
    int32_t  fd;
    uint16_t events;
    uint16_t revents;
};

struct uapi_linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[0];
};

#ifdef __riscv

BUILD_ASSERT(sizeof(struct uapi_stat) == sizeof(struct stat));
BUILD_ASSERT(sizeof(struct uapi_statfs64) == sizeof(struct statfs));

#endif

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
    dst->namelen = 256;
    dst->frsize = src->f_bsize;
    dst->flags = src->f_flags;
}

/*
static void uapi_sigaction_convert(struct uapi_sigaction* dst, const struct sigaction* src)
{
    dst->handler = src->sa_handler;
    dst->flags = src->sa_flags;
    memcpy(&dst->mask, &src->sa_flags, sizeof(dst->mask));
}
*/

static rvvm_machine_t* userland; // Emulated RVVM process context

// Short cast rvvm_addr_t -> void*
static void* to_ptr(rvvm_addr_t addr)
{
    return (void*)(size_t)addr;
}

// Short cast rvvm_addr_t -> const char*
static const char* to_str(rvvm_addr_t addr)
{
    return (const char*)(size_t)addr;
}

// Return last errno like a syscall interface
static int last_errno(void)
{
    // TODO: Host->Guest errno conversion
    return -errno;
}

// Return negative values on -1 error like a syscall interface does
static rvvm_addr_t errno_ret(int64_t val)
{
    if (val == -1) {
        return last_errno();
    } else {
        return val;
    }
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
    static int fd = 0;
    DO_ONCE({
        fd = vma_anon_memfd(4096);
        if (fd < 0) rvvm_fatal("Failed to create memfd!");
    });
    return write(fd, addr, size) == (ssize_t)size;
}

#ifndef __riscv
static char* prefix_path = "/home/lekkit/stuff/userland/debian";
#else
static char* prefix_path = NULL;
#endif

static bool fake_root = true;

static bool path_bypass(const char* path)
{
    return prefix_path == NULL
        || rvvm_strfind(path, "/dev") == path
        || rvvm_strfind(path, "/sys") == path
        || rvvm_strfind(path, "/proc") == path
        || rvvm_strfind(path, "/tmp") == path
        || rvvm_strfind(path, "/var/tmp") == path;
}

static bool path_wrapped(const char* path)
{
    return prefix_path == NULL
        || rvvm_strfind(path, prefix_path) == path
        || path_bypass(path);
}

static const char* wrap_path(char* buffer, const char* path)
{
    if (prefix_path && path) {
        if (path_bypass(path)) {
            return path;
        }

        if (rvvm_strfind(path, "/") == path) {
            size_t prefix_len = rvvm_strlcpy(buffer, prefix_path, UAPI_PATH_MAX);
            rvvm_strlcpy(buffer + prefix_len, path, UAPI_PATH_MAX - prefix_len);
            return buffer;
        }
    }
    return path;
}

static size_t unwrap_path(char* buffer, const char* path, size_t size)
{
    if (prefix_path && rvvm_strfind(path, prefix_path) == path) {
        size_t len = rvvm_strlen(prefix_path) + 1;
        size_t off = rvvm_strlcpy(buffer, "/", size);
        return rvvm_strlcpy(buffer + off, path + len, size - off);
    }

    return rvvm_strlcpy(buffer, path, UAPI_PATH_MAX);
}

static struct uapi_sigaction siga[64] = {0};

void sig_handler(int signal)
{
    rvvm_info("Received signal %d", signal);
}

typedef struct {
    rvvm_hart_t* cpu;
    uint32_t* child_settid;
    uint32_t* child_cleartid;
    uint32_t tid;
} rvvm_user_thread_t;

// Main execution loop (Run the user CPU, handle syscalls)
static void* rvvm_user_thread_wrap(void* arg);

#define BRK_HEAP_SIZE 0x40000000

static spinlock_t brk_lock = {0};
static uint8_t* brk_buffer = NULL;
static uint8_t* brk_ptr = NULL;

// We can't touch the native brk heap since it would likely blow up the process
static void* rvvm_sys_brk(void* addr)
{
    uint8_t* brk_new = addr;
    uint8_t* brk_ret = NULL;
    DO_ONCE({
        brk_buffer = vma_alloc(NULL, BRK_HEAP_SIZE, VMA_RDWR);
        brk_ptr = brk_buffer;
    });

    spin_lock(&brk_lock);
    if (brk_new >= brk_buffer && brk_new < (brk_buffer + BRK_HEAP_SIZE)) {
        if (brk_new > brk_ptr) {
            // Newly allocated brk memory should be zeroed
            memset(brk_ptr, 0, brk_new - brk_ptr);
        }
        brk_ptr = brk_new;
    } else if (brk_new) {
        rvvm_warn("invalid brk %p, current %p, off %ld!", brk_new, brk_ptr, brk_new - brk_buffer);
    }
    brk_ret = brk_ptr;
    spin_unlock(&brk_lock);
    return brk_ret;
}

#define UAPI_CLONE_VM             0x00000100
#define UAPI_CLONE_VFORK          0x00004000
#define UAPI_CLONE_SETTLS         0x00080000
#define UAPI_CLONE_PARENT_SETTID  0x00100000
#define UAPI_CLONE_CHILD_CLEARTID 0x00200000
#define UAPI_CLONE_CHILD_SETTID   0x01000000

#define UAPI_CLONE_INVALID_THREAD_FLAGS 0x7E02F000

// long sys_clone(unsigned long flags, void *stack, int *parent_tid, unsigned long tls, int *child_tid);
static int rvvm_sys_clone(rvvm_hart_t* cpu, uint32_t flags, size_t stack, uint32_t* parent_tid, size_t tls, uint32_t* child_tid)
{
    if ((flags & UAPI_CLONE_VM) && !(flags & UAPI_CLONE_VFORK)) {
        if (flags & UAPI_CLONE_INVALID_THREAD_FLAGS) {
            rvvm_warn("sys_clone(): Invalid flags %x", flags);
            return -UAPI_EINVAL;
        }

        rvvm_user_thread_t* thread = safe_new_obj(rvvm_user_thread_t);
        thread->cpu = rvvm_create_user_thread(userland);

        // Clone all CPU state
        for (size_t i=1; i<32; ++i) {
            rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_X0 + i, rvvm_read_cpu_reg(cpu, RVVM_REGID_X0 + i));
        }
        for (size_t i=0; i<32; ++i) {
            rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_F0 + i, rvvm_read_cpu_reg(cpu, RVVM_REGID_F0 + i));
        }

        // Land after syscall entry
        rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_PC, rvvm_read_cpu_reg(cpu, RVVM_REGID_PC) + 4);

        // Set guest stack pointer
        rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_X0 + 2, stack);

        if (flags & UAPI_CLONE_SETTLS) {
            // Set guest TLS register
            rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_X0 + 4, tls);
        }

        // Return 0 in cloned thread
        rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_X0 + 10, 0); // a0

        // Spawn the thread using portable RVVM thread facilities
        thread_detach(thread_create_ex(rvvm_user_thread_wrap, thread, 0));

        uint32_t tid = atomic_load_uint32(&thread->tid);
        while (!tid) {
            sleep_ms(0);
            tid = atomic_load_uint32(&thread->tid);
        }

        if ((flags & UAPI_CLONE_PARENT_SETTID) && parent_tid) {
            atomic_store_uint32(parent_tid, tid);
        }

        if (flags & UAPI_CLONE_CHILD_SETTID) {
            thread->child_settid = child_tid;
        }

        if (flags & UAPI_CLONE_CHILD_CLEARTID) {
            thread->child_cleartid = child_tid;
        }

        return tid;
    } else {
        // Emulate vfork via fork too
        return fork();
    }
}

#define UAPI_FUTEX_CMD_MASK    0x3F
#define UAPI_FUTEX_WAIT        0x0
#define UAPI_FUTEX_WAKE        0x1
#define UAPI_FUTEX_WAIT_BITSET 0x9
#define UAPI_FUTEX_WAKE_BITSET 0xA

static int rvvm_sys_futex(uint32_t* addr, int futex_op, uint32_t val, size_t val2, uint32_t* uaddr2, uint32_t val3)
{
#if defined(__linux__)
    return errno_ret(syscall(SYS_futex, addr, futex_op, val, val2, uaddr2, val3));
#else
    UNUSED(val2); UNUSED(uaddr2); UNUSED(val3);
    switch (futex_op & UAPI_FUTEX_CMD_MASK) {
        case UAPI_FUTEX_WAIT:
        case UAPI_FUTEX_WAIT_BITSET:
            if (atomic_load_uint32(addr) == val) {
                sleep_ms(1);
            }
            return 0;
        case UAPI_FUTEX_WAKE:
        case UAPI_FUTEX_WAKE_BITSET:
            return 0;
    }
    rvvm_warn("Unimplemented futex op %x", futex_op);
    return -UAPI_EINVAL;
#endif
}

static struct timeval* uapi_ts32_to_timeval(struct timeval* tv, const struct uapi_timespec32* ts32)
{
    if (ts32) {
        tv->tv_sec = ts32->tv_sec;
        tv->tv_usec = ts32->tv_nsec / 1000;
        return tv;
    }
    return NULL;
}

static int rvvm_sys_select_time32(int nfds, void* rfds, void* wfds, void* efds, const struct uapi_timespec32* ts32)
{
    // TODO: fd_set conversion
    struct timeval tv = {0};
    return errno_ret(select(nfds, rfds, wfds, efds, uapi_ts32_to_timeval(&tv, ts32)));
}

static int rvvm_sys_poll_time32(void* pfds, size_t npfds, const struct uapi_timespec32* ts32)
{
    // TODO: struct pollfd conversion
    int timeout = -1;
    if (ts32) {
        timeout = (ts32->tv_sec * 1000) + (ts32->tv_nsec / 1000000);
    }
    return errno_ret(poll(pfds, npfds, timeout));
}

static int64_t rvvm_sys_getdents64(int fd, void* dirp, size_t size)
{
    int64_t ret = 0;
    ret = errno_ret(syscall(SYS_getdents64, fd, dirp, size));
    //rvvm_warn("getdents64(%d, %p, %ld) -> %ld (%s)", fd, dirp, size, ret, (ret < 0) ? strerror(errno) : "Success");
    return ret;
    DIR* dir = fdopendir(dup(fd));
    if (dir) {
        struct dirent* dent = NULL;
        while ((dent = readdir(dir))) {
            size_t name_len = rvvm_strlen(dent->d_name);
            size_t dirent_size = sizeof(struct uapi_linux_dirent64) + name_len + 1;
            if (dirent_size <= size) {
                struct uapi_linux_dirent64* dirent = dirp;
                dirent->d_ino = dent->d_ino;
                dirent->d_off = dirent_size;
                dirent->d_reclen = dirent_size;
                // TODO: Figure d_type somehow?
                dirent->d_type = 0;
                memcpy(dirent->d_name, dent->d_name, name_len);
                dirent->d_name[name_len] = 0;

                ret += dirent_size;
                size -= dirent_size;
                dirp = ((uint8_t*)dirp) + dirent_size;
            } else {
                closedir(dir);

                return ret;
            }
        }
        closedir(dir);
        return ret;
    } else {
        return -UAPI_ENOENT;
    }
}

#define UAPI_PROT_READ  0x1
#define UAPI_PROT_WRITE 0x2
#define UAPI_PROT_EXEC  0x4

#define UAPI_MAP_SHARED          0x000001
#define UAPI_MAP_PRIVATE         0x000002
#define UAPI_MAP_FIXED           0x000010
#define UAPI_MAP_ANON            0x000020
#define UAPI_MAP_FIXED_NOREPLACE 0x100000

#define UAPI_MAP_ILLEGAL         0xE00000

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

static spinlock_t mmap_lock = {0};

static inline int rvvm_sys_prot(int prot)
{
    int ret = PROT_NONE;
    if (prot & UAPI_PROT_READ)  ret |= PROT_READ;
    if (prot & UAPI_PROT_WRITE) ret |= PROT_WRITE;
    // No real PROT_EXEC, since rvvm-user reads code when translating
    if (prot & UAPI_PROT_EXEC)  ret |= PROT_READ;
    return ret;
}

static rvvm_addr_t rvvm_sys_mmap(void* addr, size_t size, int prot, int flags, int fd, uint64_t offset)
{
    int mmap_flags = 0;
    if (flags & UAPI_MAP_ILLEGAL) {
        return -UAPI_EINVAL;
    }

    spin_lock(&mmap_lock);
    if (flags & UAPI_MAP_SHARED) mmap_flags |= MAP_SHARED;
    if (flags & UAPI_MAP_PRIVATE) mmap_flags |= MAP_PRIVATE;
    if (flags & UAPI_MAP_ANON) mmap_flags |= MAP_ANON;

    if (flags & UAPI_MAP_FIXED_NOREPLACE) {
#ifdef __linux__
        mmap_flags |= MAP_FIXED_NOREPLACE;
#else
        mmap_flags |= MAP_FIXED;
#endif
    }

    if (flags & UAPI_MAP_FIXED) {
        // This flag has destructive semantics...
        mmap_flags |= MAP_FIXED;
#ifndef __linux__
        munmap(addr, size);
#endif
    }

    rvvm_addr_t ret = errno_ret((size_t)mmap(addr, size, rvvm_sys_prot(prot), mmap_flags, fd, offset));
    spin_unlock(&mmap_lock);
    return ret;
}

static int rvvm_sys_munmap(void* addr, size_t size)
{
    spin_lock(&mmap_lock);
    rvvm_addr_t ret = errno_ret(munmap(addr, size));
    spin_unlock(&mmap_lock);
    return ret;
}

extern uint64_t __thread_selfid(void);

static int rvvm_sys_gettid(void)
{
#if defined(__APPLE__)
    return __thread_selfid();
#else
    return gettid();
#endif
}

static int fake_uid = 0;
static int fake_gid = 0;

static int rvvm_sys_getuid(void)
{
    if (fake_root) return fake_uid;
    return errno_ret(getuid());
}

static int rvvm_sys_getgid(void)
{
    if (fake_root) return fake_gid;
    return errno_ret(getgid());
}

static int rvvm_sys_setuid(int uid)
{
    if (fake_root) {
        fake_uid = uid;
        return 0;
    }
    return errno_ret(setuid(uid));
}

static int rvvm_sys_setgid(int gid)
{
    if (fake_root) {
        fake_gid = gid;
        return 0;
    }
    return errno_ret(setgid(gid));
}

static int rvvm_sys_getresuid(int* ruid, int* euid, int* suid)
{
    if (ruid) *ruid = rvvm_sys_getuid();
    if (euid) *euid = rvvm_sys_getuid();
    if (suid) *suid = rvvm_sys_getuid();
    return 0;
}

static int rvvm_sys_getresgid(int* rgid, int* egid, int* sgid)
{
    if (rgid) *rgid = rvvm_sys_getgid();
    if (egid) *egid = rvvm_sys_getgid();
    if (sgid) *sgid = rvvm_sys_getgid();
    return 0;
}

static rvvm_addr_t rvvm_sys_getcwd(char* buffer, size_t size)
{
    char tmp[UAPI_PATH_MAX] = {0};
    if (!getcwd(tmp, size)) {
        return last_errno();
    }
    return unwrap_path(buffer, tmp, size);
}

static rvvm_addr_t rvvm_sys_readlinkat(int dirfd, const char* pathname, char* buffer, size_t size)
{
    char tmp[UAPI_PATH_MAX] = {0};
    if (readlinkat(dirfd, wrap_path(tmp, pathname), tmp, size) < 0) {
        return last_errno();
    }
    return unwrap_path(buffer, tmp, size);
}

//#define rvvm_info(...) rvvm_warn(__VA_ARGS__);
//#define rvvm_info(...)

static void* rvvm_user_thread_wrap(void* arg)
{
    rvvm_user_thread_t* thread = arg;
    rvvm_hart_t* cpu = thread->cpu;
    bool running = true;

    char path_buf[UAPI_PATH_MAX] = {0};
    char path_buf1[UAPI_PATH_MAX] = {0};

    // Set up thread tid, child_tid
    atomic_store_uint32(&thread->tid, rvvm_sys_gettid());
    if (thread->child_settid) {
        atomic_store_uint32(thread->child_settid, atomic_load_uint32(&thread->tid));
    }

    while (running) {
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
                    rvvm_info("sys_getcwd(%lx, %lx)", a0, a1);
                    a0 = rvvm_sys_getcwd(to_ptr(a0), a1);
                    break;
                }
#ifdef __linux__
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
                    a0 = errno_ret(epoll_ctl(a0, a1, a2, to_ptr(a3)));
                    break;
                case 22: // epoll_pwait
                    // TODO struct conversion
                    rvvm_info("sys_epoll_pwait(%lx, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(epoll_wait(a0, to_ptr(a1), a2, a3));
                    break;
#endif
                case 23: // dup
                    rvvm_info("sys_dup(%ld)", a0);
                    a0 = errno_ret(dup(a0));
                    break;
                case 24: // dup3
                    rvvm_info("sys_dup3(%ld, %ld, %lx)", a0, a1, a2);
                    a0 = errno_ret(dup2(a0, a1));
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
                    rvvm_info("sys_mknodat(%ld, %s, %lx, %lx)", a0, to_str(a1), a2, a3);
                    a0 = errno_ret(mknodat(a0, wrap_path(path_buf, to_str(a1)), a2, a3));
                    break;
                case 34: // mkdirat
                    rvvm_info("sys_mkdirat(%ld, %s, %lx)", a0, to_str(a1), a2);
                    a0 = errno_ret(mkdirat(a0, wrap_path(path_buf, to_str(a1)), a2));
                    break;
                case 35: // unlinkat
                    rvvm_info("sys_unlinkat(%ld, %s, %lx)", a0, to_str(a1), a2);
                    a0 = errno_ret(unlinkat(a0, wrap_path(path_buf, to_str(a1)), a2));
                    break;
                case 36: // symlinkat
                    rvvm_info("sys_symlinkat(%s, %ld, %s)", to_str(a0), a1, to_str(a2));
                    a0 = errno_ret(symlinkat(wrap_path(path_buf, to_str(a0)), a1,
                                             wrap_path(path_buf1, to_str(a2))));
                    break;
                case 37: // linkat
                    rvvm_info("sys_linkat(%ld, %s, %ld, %s, %lx)", a0, to_str(a1), a2, to_str(a3), a4);
                    a0 = errno_ret(linkat(a0, wrap_path(path_buf, to_str(a1)),
                                          a2, wrap_path(path_buf1, to_str(a3)), a4));
                    break;
                case 43: { // statfs64
                    struct statfs stfs = {0};
                    rvvm_info("sys_statfs64(%s, %lx, %lx)", to_str(a0), a1, a2);
                    a0 = errno_ret(statfs(wrap_path(path_buf, to_str(a0)), &stfs));
                    uapi_statfs64_convert(to_ptr(a1), &stfs);
                    break;
                }
                case 44: { // fstatfs64
                    struct statfs stfs = {0};
                    rvvm_info("sys_fstatfs64(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(fstatfs(a0, &stfs));
                    uapi_statfs64_convert(to_ptr(a1), &stfs);
                    break;
                }
                case 45: // truncate64
                    rvvm_info("sys_truncate64(%s, %lx)", to_str(a0), a1);
                    a0 = errno_ret(truncate(wrap_path(path_buf, to_str(a0)), a1));
                    break;
                case 46: // ftruncate64
                    rvvm_info("sys_ftruncate64(%ld, %lx)", a0, a1);
                    a0 = errno_ret(ftruncate(a0, a1));
                    break;
#ifdef __linux__
                case 47: // fallocate
                    rvvm_info("sys_fallocate(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(fallocate(a0, a1, a2, a3));
                    break;
#endif
                case 48: // faccessat
                    rvvm_info("sys_faccessat(%ld, %s, %lx)", a0, to_str(a1), a2);
                    a0 = errno_ret(faccessat(a0, wrap_path(path_buf, to_str(a1)), a2, 0));
                    break;
                case 49: // chdir
                    rvvm_info("sys_chdir(%s)", to_str(a0));
                    a0 = errno_ret(chdir(wrap_path(path_buf, to_str(a0))));
                    break;
                case 50: // fchdir
                    rvvm_info("sys_fchdir(%ld)", a0);
                    a0 = errno_ret(fchdir(a0));
                    break;
                case 52: // fchmod
                    rvvm_info("sys_fchmodat(%ld, %lx)", a0, a1);
                    a0 = errno_ret(fchmod(a0, a1));
                    break;
                case 53: // fchmodat
                    rvvm_info("sys_fchmodat(%ld, %s, %lx)", a0, to_str(a1), a2);
                    a0 = errno_ret(fchmodat(a0, wrap_path(path_buf, to_str(a1)), a2, 0));
                    break;
                case 54: // fchownat
                    if (fake_root) {
                        a0 = 0;
                    } else {
                        rvvm_info("sys_fchownat(%ld, %s, %lx, %lx, %lx)", a0, to_str(a1), a2, a3, a4);
                        a0 = errno_ret(fchownat(a0, wrap_path(path_buf, to_str(a1)), a2, a3, a4));
                    }
                    break;
                case 55: // fchown
                    if (fake_root) {
                        a0 = 0;
                    } else {
                        rvvm_info("sys_fchownat(%ld, %lx, %lx)", a0, a1, a2);
                        a0 = errno_ret(fchown(a0, a1, a2));
                    }
                    break;
                case 56: // openat
                    rvvm_info("sys_openat(%ld, %s, %lx, %lx)", a0, to_str(a1), a2, a3);
                    a0 = errno_ret(openat(a0, wrap_path(path_buf, to_str(a1)), a2, a3));
                    break;
                case 57: // close
                    a0 = errno_ret(close(a0));
                    break;
                case 59: // pipe2
                    rvvm_info("sys_pipe2(%lx, %lx)", a0, a1);
                    a0 = errno_ret(pipe(to_ptr(a0)));
                    break;
                case 61: // getdents64
                    a0 = rvvm_sys_getdents64(a0, to_ptr(a1), a2);
                    break;
                case 62: // lseek
                    a0 = errno_ret(lseek(a0, a1, a2));
                    break;
                case 63: // read
                    a0 = errno_ret(read(a0, to_ptr(a1), a2));
                    break;
                case 64: // write
                    a0 = errno_ret(write(a0, to_ptr(a1), a2));
                    break;
                case 65: // readv
                    // TODO: struct conversion(?)
                    a0 = errno_ret(readv(a0, to_ptr(a1), a2));
                    break;
                case 66: // writev
                    // TODO: struct conversion(?)
                    a0 = errno_ret(writev(a0, to_ptr(a1), a2));
                    break;
                case 67: // pread64
                    a0 = errno_ret(pread(a0, to_ptr(a1), a2, a3));
                    break;
                case 68: // pwrite64
                    a0 = errno_ret(pwrite(a0, to_ptr(a1), a2, a3));
                    break;
                case 72: // pselect6_time32
                    a0 = rvvm_sys_select_time32(a0, to_ptr(a1), to_ptr(a2), to_ptr(a3), to_ptr(a4));
                    break;
                case 73: // ppoll_time32
                    a0 = rvvm_sys_poll_time32(to_ptr(a0), a1, to_ptr(a2));
                    break;
                case 78: // readlinkat
                    rvvm_info("sys_readlinkat(%ld, %s, %lx, %lx)", a0, to_str(a1), a2, a3);
                    a0 = rvvm_sys_readlinkat(a0, to_str(a1), to_ptr(a2), a3);
                    break;
                case 79: { // newfstatat
                    struct stat st = {0};
                    rvvm_info("sys_newfstatat(%ld, %s, %lx, %lx)", a0, to_str(a1), a2, a3);
                    a0 = errno_ret(fstatat(a0, wrap_path(path_buf, to_str(a1)), &st, a3));
                    uapi_stat_convert(to_ptr(a2), &st);
                    break;
                }
                case 80: { // newfstat
                    struct stat st = {0};
                    rvvm_info("sys_newfstat(%ld, %lx)", a0, a1);
                    a0 = errno_ret(fstat(a0, &st));
                    uapi_stat_convert(to_ptr(a1), &st);
                    break;
                }
                case 82: // fsync
                    a0 = errno_ret(fsync(a0));
                    break;
                case 83: // fdatasync
                    a0 = errno_ret(fsync(a0));
                    break;
                case 88: // utimensat - ignore
                    a0 = 0;
                    break;
                case 90: // capget - stub
                    if (a1) {
                        struct uapi_cap_data_struct* cap = to_ptr(a1);
                        memset(cap, 0, sizeof(*cap));
                    }
                    a0 = 0;
                    break;
                case 91: // capset - ignore
                    a0 = 0;
                    break;
                case 93: // exit
                    running = false;
                    break;
                case 94: // exit_group
                    _Exit(a0);
                    break;
                case 96: // set_tid_address
                    thread->child_cleartid = to_ptr(a0);
                    a0 = atomic_load_uint32(&thread->tid);
                    break;
                case 98: // futex
                    a0 = rvvm_sys_futex(to_ptr(a0), a1, a2, a3, to_ptr(a4), a5);
                    break;
                case 99: // set_robust_list
                    // TODO: Implement this
                    rvvm_info("sys_set_robust_list(%lx, %lx)", a0, a1);
                    a0 = 0;
                    break;
                case 101: // nanosleep
                    // TODO: Struct conversion
                    a0 = errno_ret(nanosleep(to_ptr(a0), to_ptr(a1)));
                    break;
                case 103: // setitimer
                    rvvm_info("sys_setitimer(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(setitimer(a0, to_ptr(a1), to_ptr(a2)));
                    break;
                case 113: // clock_gettime
                    // TODO: Struct conversion!
                    rvvm_info("sys_clock_gettime(%lx, %lx)", a0, a1);
                    a0 = errno_ret(clock_gettime(a0, to_ptr(a1)));
                    break;
                case 114: // clock_getres
                    rvvm_info("sys_clock_getres(%lx, %lx)", a0, a1);
                    a0 = errno_ret(clock_getres(a0, to_ptr(a1)));
                    break;
#ifdef __linux__
                case 115: // clock_nanosleep
                    // TODO: struct conversion?
                    rvvm_info("sys_clock_nanosleep(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(clock_nanosleep(a0, a1, to_ptr(a2), to_ptr(a3)));
                    break;
#endif
                case 118: // sched_setparam - ignore
                case 119: // sched_setscheduler - ignore
                case 120: // sched_getscheduler - ignore
                    a0 = 0;
                    break;
                case 121: // sched_getparam - stub
                    if (a1) {
                        struct uapi_sched_param* param = to_ptr(a1);
                        memset(param, 0, sizeof(*param));
                    }
                    a0 = 0;
                    break;
                case 122: // sched_setaffinity - ignore
                    a0 = 0;
                    break;
                case 123: // sched_getaffinity - stub
                    if (a2 && a1) {
                        memset(to_ptr(a2), 0, a1);
                        *(uint8_t*)to_ptr(a2) = 1;
                    }
                    a0 = 0;
                    break;
                case 124: // sched_yield
                    sleep_ms(0);
                    a0 = 0;
                    break;
                case 125: // sched_get_priority_max - ignore
                case 126: // sched_get_priority_min - ignore
                    a0 = 0;
                    break;
                case 129: // kill
                    rvvm_warn("sys_kill(%lx, %lx)", a0, a1);
                    a0 = errno_ret(kill(a0, a1));
                    break;
#ifdef __linux__
                case 130: // tkill
                    rvvm_warn("sys_tkill(%lx, %lx)", a0, a1);
                    a0 = errno_ret(tgkill(getpid(), a0, a1));
                    break;
                case 131: // tgkill
                    rvvm_warn("sys_tgkill(%lx, %lx, %ld)", a0, a1, a2);
                    a0 = errno_ret(tgkill(a0, a1, a2));
                    break;
#endif
                case 134: { // rt_sigaction
                    struct sigaction sa = {0};
                    rvvm_info("sys_rt_sigaction(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    if (a0 < STATIC_ARRAY_SIZE(siga)) {
                        if (a2) memcpy(to_ptr(a2), &siga[a0], a3);
                        if (a1) {
                            memcpy(&siga[a0], to_ptr(a1), a3);

                            // Register a shim signal handler
                            if (a0 != 11) {
                                memcpy(&sa.sa_mask, &siga[a0].mask, 8);
                                sa.sa_flags = siga[a0].flags & ~SA_SIGINFO;
                                sa.sa_handler = to_ptr(siga[a0].handler);
                                if (sa.sa_handler != SIG_DFL && sa.sa_handler != SIG_IGN) {
                                    sa.sa_handler = sig_handler;
                                }
                                sigaction(a0, &sa, NULL);
                            }
                        }
                        a0 = 0;
                    } else {
                        a0 = -UAPI_EINVAL;
                    }
                    break;
                }
                case 135: // rt_sigprocmask
                    rvvm_info("sys_rt_sigprocmask(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(sigprocmask(a0, to_ptr(a1), to_ptr(a2)));
                    break;
                case 137: // rt_sigtimedwait_time32
                    // TODO: Signal handling
                    sleep_ms(-1);
                    a0 = 0;
                    break;
                case 140: // setpriority - ignore
                    a0 = 0;
                    break;
                case 144: // setgid
                    a0 = rvvm_sys_setgid(a0);
                    break;
                case 146: // setuid
                    a0 = rvvm_sys_setuid(a0);
                    break;
                case 147: // setresuid - semi stub
                    a0 = rvvm_sys_setuid(a0);
                    break;
                case 148: // getresuid - semi stub
                    a0 = rvvm_sys_getresuid(to_ptr(a0), to_ptr(a1), to_ptr(a2));
                    break;
                case 149: // setresgid - semi stub
                    a0 = rvvm_sys_setgid(a0);
                    break;
                case 150: // getresgid - semi stub
                    a0 = rvvm_sys_getresgid(to_ptr(a0), to_ptr(a1), to_ptr(a2));
                    break;
                case 151: // setfsuid - ignore
                    a0 = rvvm_sys_getuid();
                    break;
                case 152: // setfsgid - ignore
                    a0 = rvvm_sys_getgid();
                    break;
                case 153: // times
                    // TODO: Struct conversion!
                    rvvm_info("sys_times(%lx)", a0);
                    a0 = errno_ret(times(to_ptr(a0)));
                    break;
#ifdef __linux__
                case 154: // setpgid
                    rvvm_info("sys_setpgid(%lx, %lx)", a0, a1);
                    a0 = errno_ret(setpgid(a0, a1));
                    break;
                case 155: // getpgid
                    rvvm_info("sys_getpgid(%lx)", a0);
                    a0 = errno_ret(getpgid(a0));
                    break;
#endif
                case 157: // setsid
                    rvvm_info("sys_setsid()");
                    a0 = errno_ret(setsid());
                    break;
                case 158: // getgroups
                    rvvm_warn("sys_getgroups(%lx, %lx)", a0, a1);
                    a0 = errno_ret(getgroups(a0, to_ptr(a1)));
                    break;
                case 159: // setgroups
                    if (fake_root) {
                        a0 = 0;
                    } else {
                        a0 = errno_ret(setgroups(a0, to_ptr(a1)));
                    }
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
                        memcpy(to_ptr(a0), &name, sizeof(name));
                        a0 = 0;
                    }
                    break;
                case 165: // getrusage
                    rvvm_info("sys_getrusage(%lx, %lx)", a0, a1);
                    a0 = errno_ret(getrusage(a0, to_ptr(a1)));
                    break;
                case 166: // umask
                    rvvm_info("sys_umask(%lx)", a0);
                    a0 = errno_ret(umask(a0));
                    break;
                case 167: // prctl
                    rvvm_info("sys_prctl(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    //a0 = errno_ret(prctl(a0, a1, a2, a3, a4));
                    a0 = 0;
                    break;
                case 172: // getpid
                    a0 = errno_ret(getpid());
                    break;
                case 173: // getppid
                    a0 = errno_ret(getppid());
                    break;
                case 174: // getuid
                    a0 = rvvm_sys_getuid();
                    break;
                case 175: // geteuid - semi stub
                    a0 = rvvm_sys_getuid();
                    break;
                case 176: // getgid
                    a0 = rvvm_sys_getgid();
                    break;
                case 177: // getegid - semi stub
                    a0 = rvvm_sys_getgid();
                    break;
                case 178: // gettid
                    a0 = thread->tid;
                    break;
#ifdef __linux__
                case 179: // sysinfo
                    // TODO: struct conversion(?)
                    rvvm_info("sys_sysinfo(%lx)", a0);
                    a0 = errno_ret(sysinfo(to_ptr(a0)));
                    break;
#endif
                case 194: // shmget
                    rvvm_info("sys_shmget(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(shmget(a0, a1, a2));
                    break;
                case 195: // shmctl
                    // TODO: struct conversion?
                    rvvm_info("sys_shmctl(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(shmctl(a0, a1, to_ptr(a2)));
                    break;
                case 196: // shmat
                    rvvm_info("sys_shmat(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret((size_t)shmat(a0, to_ptr(a1), a2));
                    break;
                case 197: // shmdt
                    rvvm_info("sys_shmdt(%lx)", a0);
                    a0 = errno_ret(shmdt(to_ptr(a0)));
                    break;
                case 198: // socket
                    rvvm_info("sys_socket(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(socket(a0, a1, a2));
                    break;
                case 199: // socketpair
                    rvvm_info("sys_socketpair(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(socketpair(a0, a1, a2, to_ptr(a3)));
                    break;
                case 200: // bind
                    // TODO struct conversion
                    rvvm_info("sys_bind(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(bind(a0, to_ptr(a1), a2));
                    break;
                case 201: // listen
                    rvvm_info("sys_listen(%ld, %lx)", a0, a1);
                    a0 = errno_ret(listen(a0, a1));
                    break;
                case 202: // accept
                    // TODO: struct conversion(?)
                    rvvm_info("sys_accept(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(accept(a0, to_ptr(a1), to_ptr(a2)));
                    break;
                case 203: // connect
                    // TODO: struct conversion(?)
                    rvvm_info("sys_connect(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(connect(a0, to_ptr(a1), a2));
                    break;
                case 204: // getsockname
                    // TODO: struct conversion(?)
                    rvvm_info("sys_getsockname(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(getsockname(a0, to_ptr(a1), to_ptr(a2)));
                    break;
                case 205: // getpeername
                    // TODO: struct conversion(?)
                    rvvm_info("sys_getpeername(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(getpeername(a0, to_ptr(a1), to_ptr(a2)));
                    break;
                case 206: // sendto
                    // TODO: struct conversion(?)
                    rvvm_info("sys_sendto(%ld, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(sendto(a0, to_ptr(a1), a2, a3, to_ptr(a4), a5));
                    break;
                case 207: // recvfrom
                    // TODO: struct conversion(?)
                    rvvm_info("sys_recvfrom(%ld, %lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4, a5);
                    a0 = errno_ret(recvfrom(a0, to_ptr(a1), a2, a3, to_ptr(a4), to_ptr(a5)));
                    break;
                case 208: // setsockopt
                    rvvm_info("sys_setsockopt(%ld, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret(setsockopt(a0, a1, a2, to_ptr(a3), a4));
                    break;
                case 209: // getsockopt
                    rvvm_info("sys_getsockopt(%ld, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret(getsockopt(a0, a1, a2, to_ptr(a3), to_ptr(a4)));
                    break;
                case 210: // shutdown
                    rvvm_info("sys_shutdown(%ld, %lx)", a0, a1);
                    a0 = errno_ret(shutdown(a0, a1));
                    break;
                case 211: // sendmsg
                    // TODO: struct conversion(?)
                    rvvm_info("sys_sendmsg(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(sendmsg(a0, to_ptr(a1), a2));
                    break;
                case 212: // recvmsg
                    // TODO: struct conversion(?)
                    rvvm_info("sys_recvmsg(%ld, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(recvmsg(a0, to_ptr(a1), a2));
                    break;
                case 214: // brk
                    rvvm_info("sys_brk(%lx)", a0);
                    a0 = (size_t)rvvm_sys_brk(to_ptr(a0));
                    break;
                case 215: // munmap
                    rvvm_info("sys_munmap(%lx, %lx)", a0, a1);
                    a0 = rvvm_sys_munmap(to_ptr(a0), a1);
                    break;
#ifdef __linux__
                case 216: // mremap
                    rvvm_info("sys_mremap(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = errno_ret((size_t)mremap(to_ptr(a0), a1, a2, a3, to_ptr(a4)));
                    break;
#endif
                case 220: // clone
                    rvvm_info("sys_clone(%lx, %lx, %lx, %lx, %lx)", a0, a1, a2, a3, a4);
                    a0 = rvvm_sys_clone(cpu, a0, a1, to_ptr(a2), a3, to_ptr(a4));
                    break;
                case 221: { // execve
                    rvvm_info("sys_execve(%lx, %lx, %lx)", a0, a1, a2);
                    if (access(wrap_path(path_buf, to_str(a0)), F_OK)) {
                        a0 = -ENOENT;
                        break;
                    }
                    char** orig_argv = to_ptr(a1);
                    char* new_argv[256] = {"/proc/self/exe", "-user", 0};
                    for (size_t i=2; i<255 && orig_argv[i - 2]; ++i) new_argv[i] = orig_argv[i - 2];
                    new_argv[2] = to_ptr(a0);
                    a0 = errno_ret(execve("/proc/self/exe", new_argv, to_ptr(a2)));
                    break;
                }
                case 222: // mmap
                    a0 = rvvm_sys_mmap(to_ptr(a0), a1, a2, a3, a4, a5);
                    break;
                case 223: // fadvise64_64 - ignore
                    a0 = 0;
                    break;
                case 226: // mprotect
                    a0 = errno_ret(mprotect(to_ptr(a0), a1, a2));
                    break;
#ifdef __linux__
                case 233: // madvise
                    rvvm_info("sys_madvise(%lx, %lx, %lx)", a0, a1, a2);
                    a0 = errno_ret(madvise(to_ptr(a0), a1, a2));
                    break;
                case 242: // accept4
                    // TODO: struct conversion(?)
                    rvvm_info("sys_accept4(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(accept4(a0, to_ptr(a1), to_ptr(a2), a3));
                    break;
#endif
                case 258: // riscv_hwprobe
                    a0 = -UAPI_ENOSYS;
                    break;
                case 259: // riscv_flush_icache
                    //rvvm_warn("riscv_flush_icache(%lx, %lx, %lx)", a0, a1, a2);
                    rvvm_flush_icache(userland, a0, a1 - a0);
                    a0 = 0;
                    break;
                case 260: // wait4
                    // TODO: Struct conversion
                    rvvm_info("sys_wait4(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(wait4(a0, to_ptr(a1), a2, to_ptr(a3)));
                    break;
                case 261: // prlimit64 - stub
                    rvvm_info("sys_prlimit64(%lx, %lx, %lx, %lx)", a0, a1, a2, a3);
                    //a0 = errno_ret(prlimit(a0, a1, to_ptr(a2), to_ptr(a3)));
                    a0 = -UAPI_EINVAL;
                    break;
#ifdef __linux__
                case 269: // sendmmsg
                    // TODO: Struct conversion
                    rvvm_info("sys_sendmmsg(%ld, %lx, %lx, %lx)", a0, a1, a2, a3);
                    a0 = errno_ret(sendmmsg(a0, to_ptr(a1), a2, a3));
                    break;
#endif
                case 276: // renameat2
                    rvvm_info("sys_renameat2(%ld, %s, %ld, %s, %lx)", a0, to_str(a1), a2, to_str(a3), a4);
                    a0 = errno_ret(renameat(a0, wrap_path(path_buf, to_str(a1)),
                                            a2, wrap_path(path_buf1, to_str(a3))));
                    break;
                case 277: // seccomp - stub
                    // Hitler SHOT HIMSELF after seeing this...
                    a0 = 0;
                    break;
                case 278: // getrandom
                    rvvm_randombytes(to_ptr(a0), a1);
                    a0 = a1;
                    break;
#ifdef __linux__
                case 279: // memfd_create
                    rvvm_info("sys_memfd_create(%s, %lx)", to_str(a0), a1);
                    a0 = errno_ret(memfd_create(to_str(a0), a1));
                    break;
                case 291: // statx
                    // TODO: Struct conversion!
                    rvvm_info("sys_statx(%ld, %s, %lx, %lx, %lx)", a0, to_str(a1), a2, a3, a4);
                    a0 = errno_ret(statx(a0, wrap_path(path_buf, to_str(a1)), a2, a3, to_ptr(a4)));
                    break;
#endif
                case 435: // clone3
                    // FUCK THIS FUCKING SYSCALL FOR NOW
                    a0 = -UAPI_ENOSYS;
                    break;
                case 436: // close_range - ignore
                    a0 = -UAPI_ENOSYS;
                    break;
                case 439: // faccessat2
                    rvvm_info("sys_faccessat2(%ld, %s, %lx, %lx)", a0, to_str(a1), a2, a3);
                    a0 = errno_ret(faccessat(a0, wrap_path(path_buf, to_str(a1)), a2, a3));
                    break;
                default:
#ifndef __riscv
                    rvvm_error("Unknown syscall %ld!", a7);
                    a0 = -UAPI_ENOSYS;
#else
                    a0 = errno_ret(syscall(a7, a0, a1, a2, a3, a4, a5));
#endif
                    break;
            }
            if ((int64_t)a0 < 0) {
                //rvvm_warn("Syscall %ld failed: %ld", a7, a0);
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

    if (thread->child_cleartid) {
        atomic_store_uint32(thread->child_cleartid, 0);
        rvvm_sys_futex(thread->child_cleartid, UAPI_FUTEX_WAKE, 1, 0, NULL, 0);
    }

    rvvm_free_user_thread(cpu);
    free(thread);
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
    userland = rvvm_create_userland(true);
    rvvm_user_thread_t* thread = safe_new_obj(rvvm_user_thread_t);
    thread->cpu = rvvm_create_user_thread(userland);

    rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_X0 + 2, (size_t)stack_top);
    rvvm_write_cpu_reg(thread->cpu, RVVM_REGID_PC,     (size_t)entry);

    rvvm_user_thread_wrap(thread);

    rvvm_free_machine(userland);
#endif
}

// Describes the executable to be ran
typedef struct {
    // Self explanatory
    size_t argc;
    char** argv;
    char** envp;

    size_t base;         // Main ELF base address (relocation)
    size_t entry;        // Main ELF entry point
    size_t interp_base;  // ELF interpreter (aka linker usually) base address
    size_t interp_entry; // ELF interpreter entry point
    size_t phdr;         // Address of ELF PHDR section
    size_t phnum;        // Number of PHDRs
} exec_desc_t;

/*
 * Guest process stack setup routines
 */

static void* stack_put_mem(uint8_t* stack, const void* mem, size_t len)
{
    stack -= len;
    memcpy(stack, mem, len);
    return stack;
}

static void* stack_put_size(void* stack, uapi_size_t val)
{
    return stack_put_mem(stack, &val, sizeof(val));
}

static void* stack_put_str(void* stack, const char* str)
{
    return stack_put_mem(stack, str, rvvm_strlen(str) + 1);
}

#define UAPI_AT_NULL           0
#define UAPI_AT_IGNORE         1
#define UAPI_AT_EXECFD         2
#define UAPI_AT_PHDR           3
#define UAPI_AT_PHENT          4
#define UAPI_AT_PHNUM          5
#define UAPI_AT_PAGESZ         6
#define UAPI_AT_BASE           7
#define UAPI_AT_FLAGS          8
#define UAPI_AT_ENTRY          9
#define UAPI_AT_NOTELF        10
#define UAPI_AT_UID           11
#define UAPI_AT_EUID          12
#define UAPI_AT_GID           13
#define UAPI_AT_EGID          14
#define UAPI_AT_PLATFORM      15
#define UAPI_AT_HWCAP         16
#define UAPI_AT_CLKTCK        17
#define UAPI_AT_SECURE        23
#define UAPI_AT_BASE_PLATFORM 24
#define UAPI_AT_RANDOM        25
#define UAPI_AT_EXECFN        31
#define UAPI_AT_SYSINFO_EHDR  33 // vDSO location; RISC-V specific!

static char* rvvm_user_init_stack(void* stack, exec_desc_t* desc)
{
    /*
     * Stack layout (upside down):
     * 1. argc (guest size_t)
     * 2. string pointers: argv, 0, envp, 0
     * 3. auxv
     * 4. padding
     * 5. random bytes (16)
     * 6. string data: argv, envp
     * 7. string data: execfn
     * 8. null (guest size_t)
     */

    // 8. null
    stack = stack_put_size(stack, 0);

    // 7. string data: execfn
    stack = stack_put_str(stack, desc->argv[0]);
    char* execfn = stack;

    // 6. string data: argv, envp
    size_t envc = 0;
    while (desc->envp[envc]) envc++;

    size_t string_num = desc->argc + envc + 2;
    uapi_size_t* string_ptrs = safe_new_arr(uapi_size_t, string_num);

    for (size_t i = envc; i--;) {
        stack = stack_put_str(stack, desc->envp[i]);
        string_ptrs[desc->argc + 1 + i] = (size_t)stack;
    }

    for (size_t i = desc->argc; i--;) {
        stack = stack_put_str(stack, desc->argv[i]);
        string_ptrs[i] = (size_t)stack;
    }

    // 5. random bytes
    char rng_buf[16] = {0};
    rvvm_randombytes(rng_buf, sizeof(rng_buf));
    stack = stack_put_mem(stack, rng_buf, sizeof(rng_buf));
    char* random_bytes = stack;

    // 4. align to 16 bytes
    stack = (char*)align_size_down((size_t)stack, 16);

    // 3. auxv, then null
    uapi_size_t auxv[] = {
        // TODO: UAPI_AT_EXECFD
        UAPI_AT_PHDR,          desc->phdr,
        UAPI_AT_PHENT,         56,
        UAPI_AT_PHNUM,         desc->phnum,
        UAPI_AT_PAGESZ,        0x1000,
        UAPI_AT_BASE,          desc->interp_base,
        UAPI_AT_FLAGS,         0,
        UAPI_AT_ENTRY,         desc->entry,
        UAPI_AT_UID,           getuid(),
        UAPI_AT_EUID,          getuid(),
        UAPI_AT_GID,           getgid(),
        UAPI_AT_EGID,          getgid(),
        UAPI_AT_PLATFORM,      0,
        UAPI_AT_HWCAP,         0x112d,
        UAPI_AT_CLKTCK,        100,
        UAPI_AT_SECURE,        0,
        UAPI_AT_BASE_PLATFORM, 0,
        UAPI_AT_RANDOM,        (size_t)random_bytes,
        UAPI_AT_EXECFN,        (size_t)execfn,
        UAPI_AT_NULL,
    };
    stack = stack_put_mem(stack, auxv, sizeof(auxv));

    // 2. string pointers
    stack = stack_put_mem(stack, string_ptrs, sizeof(uapi_size_t) * string_num);
    free(string_ptrs);

    // 1. argc
    stack = stack_put_size(stack, desc->argc);

    return stack;
}

#define STACK_SIZE 0x800000

extern char** environ;

/*
static char* default_envp[] = {
    "LANG=en_US.UTF-8",
    "TERM=xterm-256color",
    "DISPLAY=:0",
    "XDG_RUNTIME_DIR=/run/user/1000",
    NULL,
};
*/

int rvvm_user_linux(int argc, char** argv, char** envp)
{
    char path_buf[UAPI_PATH_MAX] = {0};
    stacktrace_init();
    /*elf_desc_t elf = {
        .base = NULL,
    };
    elf_desc_t interp = {
        .base = NULL,
    };*/
    rvfile_t* file = rvopen(wrap_path(path_buf, argv[0]), 0);
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
        file = rvopen(wrap_path(path_buf, elf.interp_path), 0);
        success = file && elf_load_file(file, &interp);
        rvclose(file);
        if (!success) {
            rvvm_error("Failed to load interpreter %s", elf.interp_path);
            return -1;
        }
        rvvm_info("Loaded interpreter %s at base %lx, entry %lx,\n%ld PHDRs at %lx",
                  elf.interp_path, (size_t)interp.base, interp.entry, interp.phnum, interp.phdr);
    }

    if (envp == NULL) {
        envp = environ;
    }

    getcwd(path_buf, sizeof(path_buf));
    if (!path_wrapped(path_buf)) {
        chdir(prefix_path);
    }

    //rvvm_set_loglevel(LOG_INFO);

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

    uint8_t* stack_buffer = safe_calloc(STACK_SIZE, 1);
    void* stack_top = rvvm_user_init_stack(stack_buffer + STACK_SIZE, &desc);

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

int rvvm_user_linux(int argc, char** argv, char** envp)
{
    UNUSED(argc); UNUSED(argv); UNUSED(envp);
    rvvm_warn("Userland emulation not available, define RVVM_USER_TEST");
    return -1;
}

#endif
