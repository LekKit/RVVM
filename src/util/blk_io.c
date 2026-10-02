/*
blk_io.c - Cross-platform Block & File IO library
Copyright (C) 2022  LekKit <github.com/LekKit>
                    0xCatPKG <0xCatPKG@rvvm.dev>
                    0xCatPKG <github.com/0xCatPKG>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// Expose pread(), pwrite(), fallocate(), fdatasync(), posix_fallocate()
#include "feature_test.h"

#include "blk_io.h"
#include "utils.h"

PUSH_OPTIMIZATION_SIZE

// Maximum buffer size processed per internal IO syscall: 256M
#define RVFILE_MAX_BUFF    0x10000000U

// Valid rvopen() flags
#define RVFILE_LEGAL_FLAGS 0x3F

#if !defined(USE_STDIO) && defined(HOST_TARGET_POSIX) && HOST_TARGET_POSIX >= 200112L

// Threaded POSIX 1003.1-2001 file implementation using pread() / pwrite()
#include <errno.h>  // For errno
#include <fcntl.h>  // For struct flock, open(), fcntl(), posix_fallocate(), fallocate(), fspacectl(), fdiscard()
#include <unistd.h> // For close(), lseek(), pread(), pwrite(), fdatasync(), ftruncate()

#if defined(HOST_TARGET_NETBSD)
#include <sys/param.h> // For __NetBSD_Version__
#endif

#ifndef O_RDONLY
#define O_RDONLY O_RDWR
#endif
#ifndef O_WRONLY
#define O_WRONLY O_RDWR
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOATIME
#define O_NOATIME 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_CREAT
#define O_CREAT 0
#endif
#ifndef O_EXCL
#define O_EXCL 0
#endif
#ifndef O_TRUNC
#define O_TRUNC 0
#endif
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#ifndef O_SYNC
#define O_SYNC 0
#endif
#ifndef O_DSYNC
#define O_DSYNC O_SYNC
#endif

#if !defined(F_OFD_SETLK) && defined(F_SETLK)
#define F_OFD_SETLK F_SETLK
#endif

static bool rvfile_try_lock_fd(int fd)
{
#if defined(F_OFD_SETLK)
    struct flock flk = {
        .l_type   = F_WRLCK,
        .l_whence = SEEK_SET,
    };
    return !fcntl(fd, F_OFD_SETLK, &flk) || (errno != EACCES && errno != EAGAIN);
#else
    return true;
#endif
}

#define POSIX_FILE_IMPL 1

#elif !defined(USE_STDIO) && defined(HOST_TARGET_WIN32)

// Threaded Win32 file implementation using OVERLAPPED ReadFile() / WriteFile()
// Fallbacks to locked seeking IO on legacy Windows versions
// Synchronizes rvtruncate()/rvfallocate() under a writer lock
#include <windows.h>
#include <winioctl.h>

#include "spinlock.h"

// Seek file with 64-bit offsets
static int64_t rvfile_win32_lseek(HANDLE handle, int64_t offset, DWORD whence)
{
    LONG  off_l = (uint32_t)(uint64_t)offset;
    LONG  off_h = ((uint64_t)offset) >> 32;
    DWORD ret_l = SetFilePointer(handle, off_l, &off_h, whence);
    if (likely(((int32_t)ret_l) != -1 || !GetLastError())) {
        return ((uint64_t)(uint32_t)ret_l) | (((uint64_t)(uint32_t)off_h) << 32);
    }
    return -1;
}

// Returns false on Windows CE, Windows 9x, or NT <5.0
// NT3.x with NewShell lies about being NT4.0... So we can't trust it either way
static bool rvfile_win32_has_threaded_io(void)
{
#if defined(HOST_TARGET_WINNT) && HOST_TARGET_WINNT >= 5
    return true;
#elif defined(HOST_TARGET_WINCE)
    return false;
#else
    static uint32_t flag = 0;
    uint32_t        tmp  = atomic_load_uint32_relax(&flag);
    if (!(tmp & 2)) {
        tmp = GetVersion();
        tmp = 2 | (uint32_t)(((uint8_t)tmp) >= 5);
        atomic_store_uint32_relax(&flag, tmp);
    }
    return !!(tmp & 1);
#endif
}

#define WIN32_FILE_IMPL 1

#else

// Standard C stdio file implementation using a lock around fseek()+fread() / fseek()+fwrite()
#include <stdio.h> // For fopen(), setvbuf(), fseek(), ftell(), fread(), fwrite(), fflush(), fileno()

#if defined(HOST_TARGET_WIN32)
#include <io.h>      // For _get_osfhandle()
#include <windows.h> // For GetModuleFileNameW() on WinCE
#endif

#include "spinlock.h"

#define RVFILE_POS_INVALID 0x0
#define RVFILE_POS_READ    0x1
#define RVFILE_POS_WRITE   0x2

static inline bool rvfile_stdio_overflow(uint64_t offset)
{
    return ((uint64_t)(long)offset) != offset;
}

#endif

struct blk_io_rvfile {
    uint64_t size;
    uint64_t pos;
#if defined(POSIX_FILE_IMPL)
    int fd;
#elif defined(WIN32_FILE_IMPL)
    HANDLE     handle;
    spinlock_t lock;
#else
    uint64_t   pos_real;
    FILE*      fp;
    spinlock_t lock;
    uint8_t    pos_state;
#endif
};

static inline void rvfile_grow_internal(rvfile_t* file, uint64_t length)
{
    uint64_t file_size = atomic_load_uint64(&file->size);
    do {
        if (file_size >= length) {
            // File is already big enough
            break;
        }
    } while (!atomic_cas_uint64_loop(&file->size, &file_size, length));
}

rvfile_t* rvopen(const char* filepath, uint32_t filemode)
{
#if defined(HOST_TARGET_WINCE)
    /*
     * Windows CE doesn't support relative file paths, nor current working directory
     * Workaround by appending executable directory before each relative path
     */
    char wince_path[256] = {0};
    if (filepath[0] != '\\') {
        wchar_t exe_path_u16[256] = {0};
        char*   exe_path_u8       = NULL;
        GetModuleFileNameW(NULL, exe_path_u16, STATIC_ARRAY_SIZE(exe_path_u16));
        exe_path_u8 = utf16_to_utf8((const uint16_t*)exe_path_u16);
        if (exe_path_u8) {
            size_t len = rvvm_strlcpy(wince_path, exe_path_u8, sizeof(wince_path));
            safe_free(exe_path_u8);
            while (len && wince_path[len - 1] != '\\') {
                len--;
            }
            rvvm_strlcpy(wince_path + len, filepath, sizeof(wince_path) - len);
            for (char* ptr = wince_path; ptr[0]; ptr++) {
                if (ptr[0] == '/') {
                    ptr[0] = '\\';
                }
            }
            filepath = wince_path;
        }
    }
#endif

#if defined(POSIX_FILE_IMPL)
    int open_flags = O_CLOEXEC | O_NONBLOCK;
    if (filemode & RVFILE_WRITE) {
        if (filemode & RVFILE_READ) {
            open_flags |= O_RDWR;
        } else {
            open_flags |= O_WRONLY;
        }
        if (filemode & RVFILE_CREAT) {
            open_flags |= O_CREAT;
            if (filemode & RVFILE_EXCL) {
                open_flags |= O_EXCL;
            }
        }
        if (filemode & RVFILE_TRUNC) {
            open_flags |= O_TRUNC;
        }
        if (filemode & RVFILE_SYNC) {
            open_flags |= O_DSYNC;
        }
    } else {
        open_flags |= O_RDONLY;
    }
    if (filemode & RVFILE_DIRECT) {
        open_flags |= O_DIRECT;
    }

    int fd = open(filepath, open_flags, 0600);
    if (fd < 0) {
        rvvm_debug("Failed to open \"%s\"", filepath);
        return NULL;
    }

    if ((filemode & RVFILE_EXCL) && !rvfile_try_lock_fd(fd)) {
        rvvm_error("Failed to open \"%s\": File is busy", filepath);
        close(fd);
        return NULL;
    }

    uint64_t size = lseek(fd, 0, SEEK_END);
    int32_t  flag = fcntl(fd, F_GETFL);
    if (size >= ((((uint64_t)1) << 63) - 1) || flag == -1 || fcntl(fd, F_SETFL, flag & ~O_NONBLOCK)) {
        // Failed to get file size (Is a directory/pipe/etc)
        rvvm_error("Failed to open \"%s\": File is not seekable", filepath);
        close(fd);
        return NULL;
    }

    rvfile_t* file = safe_new_obj(rvfile_t);
    file->size     = size;
    file->fd       = fd;

#elif defined(WIN32_FILE_IMPL)
    void*  path_u16 = utf8_to_utf16(filepath);
    HANDLE handle   = NULL;
    DWORD  access   = 0;
    DWORD  share    = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD  disp     = OPEN_EXISTING;
    DWORD  attr     = FILE_ATTRIBUTE_NORMAL;
    if ((filemode & RVFILE_READ) || !(filemode & RVFILE_WRITE)) {
        access |= GENERIC_READ;
    }
    if (filemode & RVFILE_WRITE) {
        access |= GENERIC_WRITE;
        if (filemode & RVFILE_CREAT) {
            disp = OPEN_ALWAYS;
            if (filemode & RVFILE_EXCL) {
                disp = CREATE_NEW;
            }
        } else if (filemode & RVFILE_TRUNC) {
            disp = TRUNCATE_EXISTING;
        }
        if (filemode & RVFILE_EXCL) {
            share = 0;
        }
        if (filemode & (RVFILE_SYNC | RVFILE_DIRECT)) {
            attr = FILE_FLAG_WRITE_THROUGH;
        }
    }

    if (path_u16) {
        handle = CreateFileW((wchar_t*)path_u16, access, share, NULL, disp, attr, NULL);
        safe_free(path_u16);
    }

#if defined(HOST_TARGET_WIN9X)
    if (!handle && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {
        // Try ANSI CreateFileA() (Win9x compat)
        handle = CreateFileA(filepath, access, share, NULL, disp, attr, NULL);
    }
#endif

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) {
            rvvm_error("Failed to open \"%s\": File is busy", filepath);
        }
        return NULL;
    }

    // Get file size
    int64_t size = rvfile_win32_lseek(handle, 0, FILE_END);

#if defined(IOCTL_DISK_GET_DRIVE_GEOMETRY)
    if (size == -1) {
        // SetFilePointer() failed, try DeviceIoControl(IOCTL_DISK_GET_DRIVE_GEOMETRY) for raw drives
        DWORD         drive_tmp = 0;
        DISK_GEOMETRY dg        = {0};
        if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg), &drive_tmp, NULL)) {
            // Attempt to lock the drive
            DeviceIoControl(handle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &drive_tmp, NULL);
            size = ((uint64_t)dg.Cylinders.QuadPart) * dg.TracksPerCylinder * dg.SectorsPerTrack * dg.BytesPerSector;
        }
    }
#endif

    if (size == -1) {
        // Failed to get file size
        rvvm_error("Failed to open \"%s\": File is not seekable", filepath);
        CloseHandle(handle);
        return NULL;
    }

#if defined(FSCTL_SET_SPARSE)
    // Enable sparse file support whenever possible
    DWORD sparse_tmp = 0;
    DeviceIoControl(handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &sparse_tmp, NULL);
#endif

    rvfile_t* file = safe_new_obj(rvfile_t);
    file->size     = (uint64_t)size;
    file->handle   = handle;

#else
    FILE* fp = NULL;
    if (filemode & RVFILE_WRITE) {
        if ((filemode & RVFILE_CREAT) && (filemode & RVFILE_TRUNC) && !(filemode & RVFILE_EXCL)) {
            // Non-exclusive file creation & truncation
            fp = fopen(filepath, (filemode & RVFILE_READ) ? "wb+" : "wb");
        } else {
            fp = fopen(filepath, "rb+");
            if (fp) {
                if ((filemode & RVFILE_CREAT) && (filemode & RVFILE_EXCL)) {
                    // File already exists, but we requested exclusive creation
                    fclose(fp);
                    return NULL;
                } else if (filemode & RVFILE_TRUNC) {
                    // Truncate file if it already exists
                    fclose(fp);
                    fp = fopen(filepath, (filemode & RVFILE_READ) ? "wb+" : "wb");
                }
            } else if (filemode & RVFILE_CREAT) {
                // Create file by opening in append mode
                fp = fopen(filepath, "ab");
                if (fp) {
                    fclose(fp);
                    fp = fopen(filepath, "rb+");
                }
            }
        }
    } else {
        fp = fopen(filepath, "rb");
    }

    if (!fp) {
        return NULL;
    }

    int64_t size = fseek(fp, 0, SEEK_END) ? -1 : ftell(fp);
    if (size < 0) {
        rvvm_error("Failed to open \"%s\": File is not seekable", filepath);
        fclose(fp);
        return NULL;
    }

#if defined(_IONBF)
    // Disable stdio buffering for coherence with mmap() and file cache
    setvbuf(fp, NULL, _IONBF, 0);
#endif

    rvfile_t* file = safe_new_obj(rvfile_t);
    file->size     = size;
    file->fp       = fp;
#endif

    if ((filemode & RVFILE_RW) && (filemode & RVFILE_TRUNC) && rvfilesize(file)) {
        // Handle RVFILE_TRUNC if failed natively
        if (!rvtruncate(file, 0)) {
            rvclose(file);
            return NULL;
        }
    }

    return file;
}

void rvclose(rvfile_t* file)
{
    if (likely(file)) {
        rvfsync(file);
#if defined(POSIX_FILE_IMPL)
        close(file->fd);
#elif defined(WIN32_FILE_IMPL)
        CloseHandle(file->handle);
#else
        fclose(file->fp);
#endif
        safe_free(file);
    }
}

uint64_t rvfilesize(rvfile_t* file)
{
    if (likely(file)) {
        return atomic_load_uint64(&file->size);
    }
    return 0;
}

// Return value of -1 means "Try again", 0 means "IO error / EOF"
static int32_t rvread_chunk(rvfile_t* file, void* dst, size_t size, uint64_t offset)
{
#if defined(POSIX_FILE_IMPL)
    int32_t ret = pread(file->fd, dst, size, offset);
    if (ret < 0 && errno != EINTR) {
        // IO error
        return 0;
    }
#elif defined(WIN32_FILE_IMPL)
    DWORD ret = size;
    if (rvfile_win32_has_threaded_io()) {
        OVERLAPPED overlapped = {
            .Offset     = (uint32_t)offset,
            .OffsetHigh = offset >> 32,
        };
        if (!ReadFile(file->handle, dst, size, &ret, &overlapped)) {
            // IO error
            ret = 0;
        }
    } else {
        if (rvfile_win32_lseek(file->handle, offset, FILE_BEGIN) == -1) {
            // Seek error
            ret = 0;
        } else if (!ReadFile(file->handle, dst, size, &ret, NULL)) {
            // IO error
            ret = 0;
        }
    }
#else
    if (offset != file->pos_real || file->pos_state != RVFILE_POS_READ) {
        if (rvfile_stdio_overflow(offset) || fseek(file->fp, offset, SEEK_SET)) {
            // Seek error, or offset doesn't fit into a long
            return 0;
        }
        file->pos_state = RVFILE_POS_READ;
    }
    uint32_t ret   = fread(dst, 1, size, file->fp);
    file->pos_real = offset + ret;
#endif
    return ret;
}

// Unlocked version of rvread(), requires a valid file offset
static size_t rvread_unlocked(rvfile_t* file, void* dst, size_t size, uint64_t offset)
{
    uint8_t* buffer = dst;
    size_t   ret    = 0;

    while (ret < size) {
        size_t  chunk_size = EVAL_MIN(size - ret, RVFILE_MAX_BUFF);
        int32_t tmp        = rvread_chunk(file, buffer + ret, chunk_size, offset + ret);
        if (likely(tmp > 0)) {
            ret += tmp;
            if (unlikely(ret < size) && offset + ret >= rvfilesize(file)) {
                // End of file reached
                break;
            }
        } else if (unlikely(!tmp)) {
            // IO error
            break;
        }
    }

    return ret;
}

size_t rvread(rvfile_t* file, void* dst, size_t size, uint64_t offset)
{
    size_t ret = 0;

    if (likely(file)) {
        uint64_t pos = (offset == RVFILE_POSITION) ? rvtell(file) : offset;
#if defined(POSIX_FILE_IMPL)
        ret = rvread_unlocked(file, dst, size, pos);
#elif defined(WIN32_FILE_IMPL)
        if (rvfile_win32_has_threaded_io()) {
            scoped_spin_read_lock_slow (&file->lock) {
                ret = rvread_unlocked(file, dst, size, pos);
            }
        } else {
            scoped_spin_lock_slow (&file->lock) {
                ret = rvread_unlocked(file, dst, size, pos);
            }
        }
#else
        scoped_spin_lock_slow (&file->lock) {
            ret = rvread_unlocked(file, dst, size, pos);
        }
#endif
        if (offset == RVFILE_POSITION && ret) {
            rvseek(file, pos + ret, RVFILE_SEEK_SET);
        }
    }

    return ret;
}

// Return value of -1 means "Try again", 0 means "IO error"
static int32_t rvwrite_chunk(rvfile_t* file, const void* src, size_t size, uint64_t offset)
{
#if defined(POSIX_FILE_IMPL)
    int32_t ret = pwrite(file->fd, src, size, offset);
    if (ret < 0 && errno != EINTR) {
        // IO error
        return 0;
    }
#elif defined(WIN32_FILE_IMPL)
    DWORD ret = size;
    if (rvfile_win32_has_threaded_io()) {
        OVERLAPPED overlapped = {
            .Offset     = (uint32_t)offset,
            .OffsetHigh = offset >> 32,
        };
        if (!WriteFile(file->handle, src, size, &ret, &overlapped)) {
            ret = 0;
        }
    } else {
        if (rvfile_win32_lseek(file->handle, offset, FILE_BEGIN) == -1) {
            // Seek error
            ret = 0;
        } else if (!WriteFile(file->handle, src, size, &ret, NULL)) {
            // IO error
            ret = 0;
        }
    }
#else
    if (offset != file->pos_real || file->pos_state != RVFILE_POS_WRITE) {
        if (rvfile_stdio_overflow(offset) || fseek(file->fp, offset, SEEK_SET)) {
            // Seek error, or offset doesn't fit into a long
            return 0;
        }
        file->pos_state = RVFILE_POS_WRITE;
    }
    uint32_t ret   = fwrite(src, 1, size, file->fp);
    file->pos_real = offset + ret;
#endif
    return ret;
}

// Unlocked version of rvwrite(), requires a valid file offset
static size_t rvwrite_unlocked(rvfile_t* file, const void* src, size_t size, uint64_t offset)
{
    const uint8_t* buffer = src;
    size_t         ret    = 0;

    while (ret < size) {
        size_t  chunk_size = EVAL_MIN(size - ret, RVFILE_MAX_BUFF);
        int32_t tmp        = rvwrite_chunk(file, buffer + ret, chunk_size, offset + ret);
        if (likely(tmp > 0)) {
            ret += tmp;
        } else if (unlikely(!tmp)) {
            // IO error
            break;
        }
    }

    rvfile_grow_internal(file, offset + ret);

    return ret;
}

size_t rvwrite(rvfile_t* file, const void* src, size_t size, uint64_t offset)
{
    size_t ret = 0;

    if (likely(file)) {
        uint64_t pos = (offset == RVFILE_POSITION) ? rvtell(file) : offset;
#if defined(POSIX_FILE_IMPL)
        ret = rvwrite_unlocked(file, src, size, offset);
#elif defined(WIN32_FILE_IMPL)
        if (rvfile_win32_has_threaded_io()) {
            scoped_spin_read_lock_slow (&file->lock) {
                ret = rvwrite_unlocked(file, src, size, offset);
            }
        } else {
            scoped_spin_lock_slow (&file->lock) {
                ret = rvwrite_unlocked(file, src, size, offset);
            }
        }
#else
        scoped_spin_lock_slow (&file->lock) {
            ret = rvwrite_unlocked(file, src, size, offset);
        }
#endif
        if (offset == RVFILE_POSITION && ret) {
            rvseek(file, pos + ret, RVFILE_SEEK_SET);
        }
    }

    return ret;
}

bool rvtrim(rvfile_t* file, uint64_t offset, uint64_t size)
{
    if (likely(file)) {
#if defined(POSIX_FILE_IMPL) && defined(HOST_TARGET_LINUX) && defined(FALLOC_FL_PUNCH_HOLE)
        // Use fallocate(FALLOC_FL_PUNCH_HOLE) on Linux to punch holes
        return !fallocate(file->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, size);
#elif defined(POSIX_FILE_IMPL) && defined(HOST_TARGET_DARWIN) && defined(F_PUNCHHOLE)
        // Use fcntl(F_PUNCHHOLE) on MacOS with page aligned offsets to punch holes
        uint64_t     align_off  = (offset + 0xFFF) & ~(uint64_t)0xFFF;
        uint64_t     align_size = (size - (align_off - offset)) & ~(uint64_t)0xFFF;
        fpunchhole_t punch      = {.fp_offset = align_off, .fp_length = align_size};
        return !fcntl(file->fd, F_PUNCHHOLE, &punch);
#elif defined(POSIX_FILE_IMPL) && defined(HOST_TARGET_FREEBSD) && defined(SPACECTL_DEALLOC)
        // Use fspacectl(SPACECTL_DEALLOC) added in FreeBSD 14 to punch holes
        struct spacectl_range rqsr = {
            .r_offset = offset,
            .r_len    = size,
        };
        return !fspacectl(file->fd, SPACECTL_DEALLOC, &rqsr, 0, NULL);
#elif defined(POSIX_FILE_IMPL) && defined(HOST_TARGET_NETBSD) && HOST_TARGET_NETBSD >= 7
        // Use fdiscard() added in NetBSD 7 to punch holes
        return !fdiscard(file->fd, offset, size);
#elif defined(WIN32_FILE_IMPL) && defined(FSCTL_SET_ZERO_DATA)
        // Use DeviceIoControl(FSCTL_SET_ZERO_DATA) on Windows to punch holes
        FILE_ZERO_DATA_INFORMATION fz  = {0};
        DWORD                      tmp = 0;
        fz.FileOffset.QuadPart         = offset;
        fz.BeyondFinalZero.QuadPart    = offset + size;
        return !!DeviceIoControl(file->handle, FSCTL_SET_ZERO_DATA, &fz, sizeof(fz), NULL, 0, &tmp, NULL);
#else
        UNUSED(offset);
        UNUSED(size);
#endif
    }
    return false;
}

bool rvseek(rvfile_t* file, int64_t offset, uint32_t startpos)
{
    if (likely(file)) {
        if (startpos == RVFILE_SEEK_CUR) {
            offset = rvtell(file) + offset;
        } else if (startpos == RVFILE_SEEK_END) {
            offset = rvfilesize(file) - offset;
        } else if (startpos != RVFILE_SEEK_SET) {
            // Invalid seek operation
            offset = -1;
        }
        if (offset >= 0) {
            atomic_store_uint64_relax(&file->pos, offset);
            return true;
        }
    }
    return false;
}

uint64_t rvtell(rvfile_t* file)
{
    if (likely(file)) {
        return atomic_load_uint64_relax(&file->pos);
    }
    return -1;
}

bool rvfsync(rvfile_t* file)
{
    bool ret = false;
    if (likely(file)) {
#if defined(POSIX_FILE_IMPL)
#if defined(HOST_TARGET_DARWIN) && defined(F_BARRIERFSYNC)
        // Barrier sync on MacOS, will fail on Darling and possibly elsewhere
        ret = !fcntl(file->fd, F_BARRIERFSYNC);
#endif
        // Spin on fdatasync() / fsync() for a few times in case of EINTR
        for (size_t i = 0; i < 10 && !ret; ++i) {
#if defined(HOST_TARGET_LINUX)
            ret = !fdatasync(file->fd);
#else
            ret = !fsync(file->fd);
#endif
            if (!ret && errno != EINTR) {
                break;
            }
        }
#elif defined(WIN32_FILE_IMPL)
        scoped_spin_lock_slow (&file->lock) {
            // Synchronize any previously issued IO
        }
        // Trying to flush a read-only file results in ERROR_ACCESS_DENIED, ignore
        ret = !!FlushFileBuffers(file->handle) || (GetLastError() == ERROR_ACCESS_DENIED);
#else
        scoped_spin_lock_slow (&file->lock) {
            ret = !fflush(file->fp);
        }
#endif
    }
    return ret;
}

bool rvtruncate(rvfile_t* file, uint64_t length)
{
    if (likely(file)) {
        bool resized = false;
        if (length == rvfilesize(file)) {
            return true;
        }
#if defined(POSIX_FILE_IMPL)
        resized = !ftruncate(file->fd, length);
#elif defined(WIN32_FILE_IMPL)
        scoped_spin_lock_slow (&file->lock) {
            // Perform SetFilePointer() + SetEndOfFile() under writer lock
            // to prevent ReadFile()/WriteFile() from moving file pointer
            if (rvfile_win32_lseek(file->handle, length, FILE_BEGIN)) {
                // Successfully set file pointer, set end of file here
                resized = !!SetEndOfFile(file->handle);
            }
        }
#endif
        if (resized) {
            // Successfully resized the file natively
            atomic_store_uint64(&file->size, length);
            return true;
        } else if (length >= rvfilesize(file)) {
            // Try to grow the file via rvfallocate()
            return rvfallocate(file, length);
        }
    }
    return false;
}

static bool rvfile_grow_unlocked(rvfile_t* file, uint64_t length)
{
    char tmp = 0;
    if (rvread_unlocked(file, &tmp, 1, length - 1)) {
        // File already big enough
        return true;
    }
    return !!rvwrite_unlocked(file, &tmp, 1, length - 1);
}

static bool rvfile_grow_generic(rvfile_t* file, uint64_t length)
{
    bool ret = true;
    if (length > rvfilesize(file)) {
        // Grow the file by writing one byte at the new end
#if defined(POSIX_FILE_IMPL)
        // NOTE: This is not thread safe whenever there are writers beyond the end of file
        // This only applies to POSIX implementations, where posix_fallocate() is preferable
        ret = rvfile_grow_unlocked(file, length);
#else
        scoped_spin_lock_slow (&file->lock) {
            ret = rvfile_grow_unlocked(file, length);
        }
#endif
    }
    return ret;
}

bool rvfallocate(rvfile_t* file, uint64_t length)
{
    if (likely(file)) {
        if (length <= rvfilesize(file)) {
            return true;
        }
#if defined(POSIX_FILE_IMPL)                                                                                           \
    && ((defined(HOST_TARGET_LINUX) && defined(FALLOC_FL_PUNCH_HOLE))                                                  \
        || (defined(HOST_TARGET_FREEBSD) && HOST_TARGET_FREEBSD >= 9)                                                  \
        || (defined(HOST_TARGET_NETBSD) && HOST_TARGET_NETBSD >= 7))
        // Try to grow file via posix_fallocate() (Available on Linux 2.6.23+, FreeBSD 9+, NetBSD 7+)
        if (!posix_fallocate(file->fd, length - 1, 1)) {
            rvfile_grow_internal(file, length);
            return true;
        }
#endif
        // Generic grow file implementation
        return rvfile_grow_generic(file, length);
    }
    return false;
}

int rvfile_get_posix_fd(rvfile_t* file)
{
    if (likely(file)) {
        UNUSED(file);
#if defined(POSIX_FILE_IMPL)
        return file->fd;
#elif !defined(WIN32_FILE_IMPL) && defined(HOST_TARGET_POSIX)
        return fileno(file->fp);
#endif
    }
    return -1;
}

void* rvfile_get_win32_handle(rvfile_t* file)
{
    if (likely(file)) {
        UNUSED(file);
#if defined(WIN32_FILE_IMPL)
        return (void*)file->handle;
#elif !defined(POSIX_FILE_IMPL) && defined(HOST_TARGET_WINCE)
        return (void*)_fileno(file->fp);
#elif !defined(POSIX_FILE_IMPL) && defined(HOST_TARGET_WIN32)
        return (void*)_get_osfhandle(_fileno(file->fp));
#endif
    }
    return NULL;
}

POP_OPTIMIZATION_SIZE
