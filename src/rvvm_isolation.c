/*
rvvm_isolation.c - Process & thread isolation
Copyright (C) 2024  LekKit <github.com/LekKit>

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

// Needed for pledge() and possibly other stuff
#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "rvvm_isolation.h"
#include "utils.h"
#include "compiler.h"

#ifdef USE_ISOLATION

#if defined(__linux__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#define ISOLATION_DROP_ROOT_IMPL
#endif

#if defined(__linux__) && CHECK_INCLUDE(sys/prctl.h)
#include <sys/prctl.h>
#define ISOLATION_PRCTL_IMPL
#endif

#if defined(__linux__) && CHECK_INCLUDE(linux/seccomp.h) && CHECK_INCLUDE(sys/prctl.h)
#include <sys/mman.h>
#include <sys/prctl.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/unistd.h> // __NR_*
#define ISOLATION_SECCOMP_IMPL
#endif

#if defined(__OpenBSD__)
#include <unistd.h>
#define ISOLATION_PLEDGE_IMPL
#endif

#endif

// Drop all the capabilities of the calling thread, prevent privilege escalation
static void drop_thread_caps(void)
{
#ifdef ISOLATION_PRCTL_IMPL
#ifdef PR_SET_NO_NEW_PRIVS
    // Prevent privilege escalation via setuid etc
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) && errno != ENOSYS) {
        DO_ONCE(rvvm_warn("Failed to set PR_SET_NO_NEW_PRIVS!"));
    }
#endif

#ifdef PR_CAPBSET_DROP
    // Drop all capabilities
    for (int cap = 0; cap < 64; ++cap) {
        UNUSED(!prctl(PR_CAPBSET_DROP, cap, 0, 0, 0));
    }
#endif
#endif
}

// Drop from root user to nobody
static void drop_root_user(void)
{
#ifdef ISOLATION_DROP_ROOT_IMPL
    if (getuid() == 0) {
        // We are root for whatever reason, drop to nobody
        char buffer[256] = {0};
        struct passwd pwd = {0};
        struct passwd* result = NULL;
        if (getpwnam_r("nobody", &pwd, buffer, sizeof(buffer), &result)
         || setresgid(pwd.pw_gid, pwd.pw_gid, pwd.pw_gid)
         || setresuid(pwd.pw_uid, pwd.pw_uid, pwd.pw_uid)) {
            rvvm_fatal("Failed to drop root privileges!");
        }
        UNUSED(!chdir("/"));
    }
#endif
}

#ifdef ISOLATION_SECCOMP_IMPL

#define BPF_SECCOMP_ALLOW_SYSCALL(syscall) \
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, syscall, 0, 1), \
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW), \

#define BPF_SECCOMP_BLOCK_SYSCALL(syscall) \
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, syscall, 0, 1), \
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP), \

#define BPF_SECCOMP_BLOCK_RWX_MMAN(syscall) \
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, syscall, 0, 5), \
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, args[2])), \
        BPF_STMT(BPF_ALU + BPF_AND + BPF_K, ~(PROT_READ | PROT_WRITE)), \
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 0, 1, 0), \
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP), \
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW), \

static void seccomp_setup_syscall_filter(bool all_threads) {

    // Let's just hope this won't blow up out of nowhere
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, nr)),

#ifdef __NR_mmap2
        BPF_SECCOMP_BLOCK_RWX_MMAN(__NR_mmap2) // i386-specific
#endif
        BPF_SECCOMP_BLOCK_RWX_MMAN(__NR_mmap)
        BPF_SECCOMP_BLOCK_RWX_MMAN(__NR_mprotect)

        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sched_yield)
#ifdef __NR_futex
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_futex)
#endif
#ifdef __NR_futex_time64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_futex_time64)
#endif
#ifdef __NR_futex_wake
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_futex_wake)
#endif
#ifdef __NR_futex_wait
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_futex_wait)
#endif
#ifdef __NR_futex_requeue
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_futex_requeue)
#endif
#ifdef __NR_futex_waitv
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_futex_waitv)
#endif

        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pread64)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pwrite64)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sendto)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_recvfrom)
#ifdef __NR_epoll_wait
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_wait)
#endif
#ifdef __NR_epoll_pwait
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_pwait)
#endif
#ifdef __NR_epoll_pwait2
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_pwait2)
#endif
#ifdef __NR_epoll_ctl
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_ctl)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_clock_gettime)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_nanosleep)

        BPF_SECCOMP_ALLOW_SYSCALL(__NR_read)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_write)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_close)
#ifdef __NR_fstat
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fstat)
#endif
#ifdef __NR_fstat64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fstat64)
#endif
#ifdef __NR_poll
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_poll)
#endif
#ifdef __NR__llseek
        BPF_SECCOMP_ALLOW_SYSCALL(__NR__llseek)
#endif
#ifdef __NR_lseek
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_lseek)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_munmap)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_brk)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigaction)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigprocmask)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigreturn)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_ioctl)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_readv)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_writev)
#ifdef __NR_pipe
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pipe)
#endif
#ifdef __NR_select
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_select)
#endif
#ifdef __NR__newselect
        BPF_SECCOMP_ALLOW_SYSCALL(__NR__newselect)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_mremap)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_msync)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_mincore)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_madvise)
#ifdef __NR_dup
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_dup)
#endif
#ifdef __NR_dup2
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_dup2)
#endif
#ifdef __NR_pause
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pause)
#endif
#ifdef __NR_getitimer
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getitimer)
#endif
#ifdef __NR_alarm
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_alarm)
#endif
#ifdef __NR_setitimer
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_setitimer)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getpid)
#ifdef __NR_sendfile
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sendfile)
#endif
#ifdef __NR_sendfile64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sendfile64)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_socket)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_connect)
#ifdef __NR_accept
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_accept)
#endif
#ifdef __NR_socketcall
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_socketcall)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sendmsg)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_recvmsg)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_shutdown)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_bind)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_listen)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getsockname)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getpeername)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_socketpair)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_setsockopt)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getsockopt)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_clone)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_exit)
#ifdef __NR_wait4
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_wait4)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_uname)
#ifdef __NR_shmctl
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_shmctl)
#endif
#ifdef __NR_shmdt
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_shmdt)
#endif
#ifdef __NR_fcntl
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fcntl)
#endif
#ifdef __NR_fcntl64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fcntl64)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fsync)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fdatasync)
#ifdef __NR_ftruncate
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_ftruncate)
#endif
#ifdef __NR_ftruncate64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_ftruncate64)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_gettimeofday)
#ifdef __NR_getrlimit
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getrlimit)
#endif
#ifdef __NR_setrlimit
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_setrlimit)
#endif
#ifdef __NR_ugetrlimit
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_ugetrlimit)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getrusage)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sysinfo)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_times)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getuid)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getgid)
#ifdef __NR_geteuid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_geteuid)
#endif
#ifdef __NR_getegid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getegid)
#endif
#ifdef __NR_getppid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getppid)
#endif
#ifdef __NR_getpgrp
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getpgrp)
#endif
#ifdef __NR_getgroups
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getgroups)
#endif
#ifdef __NR_getresuid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getresuid)
#endif
#ifdef __NR_getresgid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getresgid)
#endif
#ifdef __NR_getsid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getsid)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigpending)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigtimedwait)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigqueueinfo)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_sigsuspend)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sigaltstack)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sync)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_gettid)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_readahead)
#ifdef __NR_time
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_time)
#endif
#ifdef __NR_sched_setaffinity
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sched_setaffinity)
#endif
#ifdef __NR_sched_getaffinity
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sched_getaffinity)
#endif
#ifdef __NR_set_thread_area
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_set_thread_area)
#endif
#ifdef __NR_get_thread_area
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_get_thread_area)
#endif
#ifdef __NR_epoll_create
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_create)
#endif
#ifdef __NR_epoll_ctl_old
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_ctl_old)
#endif
#ifdef __NR_epoll_wait_old
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_wait_old)
#endif
#ifdef __NR_set_tid_address
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_set_tid_address)
#endif
#ifdef __NR_restart_syscall
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_restart_syscall)
#endif
#ifdef __NR_fadvise64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fadvise64)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timer_create)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timer_settime)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timer_gettime)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timer_getoverrun)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timer_delete)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_clock_getres)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_clock_nanosleep)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_exit_group)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_tgkill)
#ifdef __NR_waitid
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_waitid)
#endif
#ifdef __NR_migrate_pages
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_migrate_pages)
#endif
#ifdef __NR_pselect6
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pselect6)
#endif
#ifdef __NR_pselect6_time64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pselect6_time64)
#endif
#ifdef __NR_ppoll
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_ppoll)
#endif
#ifdef __NR_set_robust_list
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_set_robust_list)
#endif
#ifdef __NR_get_robust_list
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_get_robust_list)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_splice)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_tee)
#ifdef __NR_sync_file_range
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sync_file_range)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_vmsplice)
#ifdef __NR_move_pages
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_move_pages)
#endif
#ifdef __NR_signalfd
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_signalfd)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timerfd_create)
#ifdef __NR_eventfd
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_eventfd)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_fallocate)
#ifdef __NR_timerfd_settime
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timerfd_settime)
#endif
#ifdef __NR_timerfd_gettime
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_timerfd_gettime)
#endif
#ifdef __NR_accept4
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_accept4)
#endif
#ifdef __NR_signalfd4
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_signalfd4)
#endif
#ifdef __NR_eventfd2
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_eventfd2)
#endif
#ifdef __NR_epoll_create1
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_epoll_create1)
#endif
#ifdef __NR_dup3
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_dup3)
#endif
#ifdef __NR_pipe2
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pipe2)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_preadv)
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pwritev)
#ifdef __NR_rt_tgsigqueueinfo
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rt_tgsigqueueinfo)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_recvmmsg)
#ifdef __NR_prlimit64
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_prlimit64)
#endif
#ifdef __NR_syncfs
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_syncfs)
#endif
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_sendmmsg)
#ifdef __NR_getcpu
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getcpu)
#endif
#ifdef __NR_getrandom
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_getrandom)
#endif
#ifdef __NR_memfd_create
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_memfd_create)
#endif
#ifdef __NR_membarrier
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_membarrier)
#endif
#ifdef __NR_copy_file_range
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_copy_file_range)
#endif
#ifdef __NR_preadv2
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_preadv2)
#endif
#ifdef __NR_pwritev2
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_pwritev2)
#endif
#ifdef __NR_rseq
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_rseq)
#endif
#ifdef __NR_io_uring_setup
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_io_uring_setup)
#endif
#ifdef __NR_io_uring_enter
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_io_uring_enter)
#endif
#ifdef __NR_io_uring_register
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_io_uring_register)
#endif
#ifdef __NR_cachestat
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_cachestat)
#endif

        // Arch-specific syscalls
#ifdef __NR_riscv_flush_icache
        BPF_SECCOMP_ALLOW_SYSCALL(__NR_riscv_flush_icache)
#elif defined(__riscv)
        BPF_SECCOMP_ALLOW_SYSCALL(259)
#endif

        // Return ENOSYS for everything not allowed here
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
        //BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS),
    };

    struct sock_fprog prog = {
        .filter = filter,
        .len = STATIC_ARRAY_SIZE(filter),
    };

    int flags = all_threads ? SECCOMP_FILTER_FLAG_TSYNC : 0;

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, flags) && errno != ENOSYS) {
        DO_ONCE(rvvm_warn("Failed to set seccomp syscall filter: %s!", strerror(errno)));
    }
}

#endif

void rvvm_restrict_this_thread(void)
{
    drop_root_user();
    drop_thread_caps();
#ifdef ISOLATION_SECCOMP_IMPL
    seccomp_setup_syscall_filter(false);
#endif
    // No per-thread pledge on OpenBSD :c
}

PUBLIC void rvvm_restrict_process(void)
{
    drop_root_user();
    drop_thread_caps();
#ifdef ISOLATION_SECCOMP_IMPL
    seccomp_setup_syscall_filter(true);
#elif defined(ISOLATION_PLEDGE_IMPL)
    if (pledge("stdio inet tty ioctl dns audio drm vmm error", "")) {
        DO_ONCE(rvvm_warn("Failed to enforce pledge: %s!", strerror(errno)));
    }
#endif
}
