/*
blk_io.c - Cross-platform Block & File IO library
Copyright (C) 2022 KotB <github.com/0xCatPKG>
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

// Force 64-bit file offsets
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

// Needed for pread()/pwrite(), syscall() when not passing -std=gnu..
#define _GNU_SOURCE
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "blk_io.h"

// Maximum buffer size processed per internal IO syscall
#define RVFILE_MAX_BUFF 0x10000000

#if (defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)) && !defined(USE_STDIO)
// POSIX implementation using open, pread, pwrite...
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#define POSIX_FILE_IMPL

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifdef __FreeBSD__
#include <sys/syscall.h> // For SYS_fspacectl()
#endif

static bool try_lock_fd(int fd)
{
    struct flock flk = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
    };
    return fcntl(fd, F_SETLK, &flk) == 0 || (errno != EACCES && errno != EAGAIN);
}

#elif defined(_WIN32) && !defined(USE_STDIO)
// Win32 implementation using CreateFile, OVERLAPPED, ReadFile...
#include <windows.h>
#define WIN32_FILE_IMPL
// Prototypes for older winapi headers
#define DEVIOCTL_SET_SPARSE    0x000900c4
#define DEVIOCTL_SET_ZERO_DATA 0x000980c8
typedef struct {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER BeyondFinalZero;
} SET_ZERO_DATA_INFO;

#else
// C stdio implementation using fopen, fread...
// Emulates pread by using locks around fseek+fread
#include <stdio.h>

#ifdef _WIN32
// For rvfile_get_win32_handle()
#include <windows.h>
#include <io.h>
#endif

#define RVFILE_POS_INVALID 0x0
#define RVFILE_POS_READ    0x1
#define RVFILE_POS_WRITE   0x2
#endif

// RVVM internal headers come after system headers because of safe_free()
#include "mem_ops.h"
#include "utils.h"
#include "spinlock.h"

struct blk_io_rvfile {
    uint64_t size;
    uint64_t pos;
#if defined(POSIX_FILE_IMPL)
    int fd;
#elif defined(WIN32_FILE_IMPL)
    HANDLE handle;
#else
    uint64_t pos_real;
    FILE* fp;
    spinlock_t lock;
    uint8_t  pos_state;
#endif
};

static inline bool rvfile_grow_internal(rvfile_t* file, uint64_t length)
{
    while (true) {
        uint64_t file_size = atomic_load_uint64(&file->size);
        if (likely(length <= file_size)) {
            // File is big enough
            break;
        }
        if (atomic_cas_uint64(&file->size, file_size, length)) {
            // Grow success
            return true;
        }
    }
    return false;
}

rvfile_t* rvopen(const char* filepath, uint8_t filemode)
{
    if (filemode & ~RVFILE_LEGAL_FLAGS) return NULL;
#if defined(POSIX_FILE_IMPL)
    int open_flags = O_CLOEXEC;
    if (filemode & RVFILE_RW) {
        if (filemode & RVFILE_TRUNC) open_flags |= O_TRUNC;
        if (filemode & RVFILE_CREAT) {
            open_flags |= O_CREAT;
            if (filemode & RVFILE_EXCL) open_flags |= O_EXCL;
        }
        open_flags |= O_RDWR;
    } else open_flags |= O_RDONLY;
#ifdef O_DIRECT
    if (filemode & RVFILE_DIRECT) open_flags |= O_DIRECT;
#endif

    int fd = open(filepath, open_flags, 0644);
    if (fd < 0) return NULL;

    if ((filemode & RVFILE_EXCL) && !try_lock_fd(fd)) {
        rvvm_error("File %s is busy", filepath);
        close(fd);
        return NULL;
    }

    rvfile_t* file = safe_new_obj(rvfile_t);
    file->size = lseek(fd, 0, SEEK_END);
    file->fd = fd;
    return file;
#elif defined(WIN32_FILE_IMPL)
    DWORD access = GENERIC_READ | ((filemode & RVFILE_RW) ? GENERIC_WRITE : 0);
    DWORD share = (filemode & RVFILE_EXCL) ? 0 : (FILE_SHARE_READ | FILE_SHARE_WRITE);
    DWORD disp = OPEN_EXISTING;
    DWORD attr = FILE_ATTRIBUTE_NORMAL;
    if (filemode & RVFILE_DIRECT) attr = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    if (filemode & RVFILE_RW) {
        if (filemode & RVFILE_CREAT) {
            disp = OPEN_ALWAYS;
            if (filemode & RVFILE_EXCL) disp = CREATE_NEW;
        } else {
            if (filemode & RVFILE_TRUNC) disp = TRUNCATE_EXISTING;
        }
    }

    size_t path_len = rvvm_strlen(filepath);
    wchar_t* u16_path = safe_new_arr(wchar_t, path_len + 1);
    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, u16_path, path_len + 1);
    HANDLE handle = CreateFileW(u16_path, access, share, NULL, disp, attr, NULL);
    free(u16_path);

    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) {
            rvvm_error("File %s is busy", filepath);
        }
        return NULL;
    }

    DWORD sizeh = 0;
    DWORD sizel = GetFileSize(handle, &sizeh);
    DWORD tmp = 0;
    DeviceIoControl(handle, DEVIOCTL_SET_SPARSE, NULL, 0, NULL, 0, &tmp, NULL);

    rvfile_t* file = safe_new_obj(rvfile_t);
    file->size = ((uint64_t)sizeh) << 32 | sizel;
    file->handle = handle;
    if ((filemode & RVFILE_TRUNC) && disp == OPEN_ALWAYS) {
        // Handle RVFILE_TRUNC properly
        rvtruncate(file, 0);
    }
    return file;
#else
    const char* open_mode = "rb";
    if (filemode & RVFILE_RW) {
        open_mode = "rb+";
        if ((filemode & RVFILE_TRUNC) || (filemode & RVFILE_CREAT)) {
            // NOTE: This implementation is not atomically safe for RVFILE_CREAT and RVFILE_TRUNC
            FILE* file_exists = fopen(filepath, "rb");
            if (file_exists) {
                fclose(file_exists);
                if (filemode & RVFILE_TRUNC) {
                    // File exists and we want to truncate it
                    open_mode = "wb+";
                }
                if ((filemode & RVFILE_CREAT) && (filemode & RVFILE_EXCL)) {
                    // File exists but we requested exclusive creation
                    return NULL;
                }
            } else if (filemode & RVFILE_CREAT) {
                // File doesn't exist but we want to create it
                open_mode = "wb+";
            }
        }
    }

    FILE* fp = fopen(filepath, open_mode);
    if (!fp) return NULL;

#ifdef _IONBF
    // Disable stdio buffering altogether for coherence sake
    setvbuf(fp, NULL, _IONBF, 0);
#endif

    rvfile_t* file = safe_new_obj(rvfile_t);
    fseek(fp, 0, SEEK_END);
    file->size = ftell(fp);
    file->pos_state = RVFILE_POS_INVALID;
    file->fp = fp;
    return file;
#endif
}

void rvclose(rvfile_t* file)
{
    if (!file) return;
    rvfsync(file);
#if defined(POSIX_FILE_IMPL)
    close(file->fd);
#elif defined(WIN32_FILE_IMPL)
    CloseHandle(file->handle);
#else
    spin_lock_slow(&file->lock);
    fclose(file->fp);
    spin_unlock(&file->lock);
#endif
    free(file);
}

uint64_t rvfilesize(rvfile_t* file)
{
    if (!file) return 0;
    return atomic_load_uint64(&file->size);
}

// Return value of -1 means "Try again"
static int32_t rvread_chunk(rvfile_t* file, void* dst, size_t size, uint64_t offset)
{
#if defined(POSIX_FILE_IMPL)
    int32_t ret = pread(file->fd, dst, size, offset);
    if (ret < 0 && errno != EINTR) ret = 0;
#elif defined(WIN32_FILE_IMPL)
    DWORD ret = 0;
    OVERLAPPED overlapped = { .OffsetHigh = offset >> 32, .Offset = (uint32_t)offset, };
    ReadFile(file->handle, dst, size, &ret, &overlapped);
#else
    spin_lock_slow(&file->lock);
    if (offset != file->pos_real || file->pos_state != RVFILE_POS_READ) {
        fseek(file->fp, offset, SEEK_SET);
    }
    uint32_t ret = fread(dst, 1, size, file->fp);
    file->pos_real = offset + ret;
    file->pos_state = RVFILE_POS_READ;
    spin_unlock(&file->lock);
#endif
    return ret;
}

size_t rvread(rvfile_t* file, void* dst, size_t size, uint64_t offset)
{
    if (!file || size == 0) return 0;
    uint64_t pos = (offset == RVFILE_CUR) ? rvtell(file) : offset;
    uint8_t* buffer = dst;
    size_t ret = 0;

    while (ret < size) {
        size_t chunk_size = EVAL_MIN(size - ret, RVFILE_MAX_BUFF);
        int32_t tmp = rvread_chunk(file, buffer + ret, chunk_size, pos + ret);
        if (tmp > 0) {
            ret += tmp;
        } else if (tmp == 0 || pos + ret >= rvfilesize(file)) {
            // IO error, or end of file
            break;
        }
    }

    if (offset == RVFILE_CUR) rvseek(file, ret, RVFILE_SEEK_CUR);
    return ret;
}

// Return value of -1 means "Try again"
int32_t rvwrite_chunk(rvfile_t* file, const void* src, size_t size, uint64_t offset)
{
#if defined(POSIX_FILE_IMPL)
    int32_t ret = pwrite(file->fd, src, size, offset);
    if (ret < 0 && errno != EINTR) ret = 0;
#elif defined(WIN32_FILE_IMPL)
    DWORD ret = 0;
    OVERLAPPED overlapped = { .OffsetHigh = offset >> 32, .Offset = (uint32_t)offset, };
    WriteFile(file->handle, src, size, &ret, &overlapped);
#else
    spin_lock_slow(&file->lock);
    if (offset != file->pos_real || file->pos_state != RVFILE_POS_WRITE) {
        fseek(file->fp, offset, SEEK_SET);
    }
    uint32_t ret = fwrite(src, 1, size, file->fp);
    file->pos_real = offset + ret;
    file->pos_state = RVFILE_POS_WRITE;
    spin_unlock(&file->lock);
#endif
    return ret;
}

size_t rvwrite(rvfile_t* file, const void* source, size_t count, uint64_t offset)
{
    if (!file || count == 0) return 0;
    uint64_t pos = (offset == RVFILE_CUR) ? rvtell(file) : offset;
    const uint8_t* buffer = source;
    size_t ret = 0;

    while (ret < count) {
        size_t size = EVAL_MIN(count - ret, RVFILE_MAX_BUFF);
        int32_t tmp = rvwrite_chunk(file, buffer + ret, size, pos + ret);
        if (tmp > 0) {
            ret += tmp;
        } else if (tmp == 0) {
            // IO error
            break;
        }
    }

    rvfile_grow_internal(file, pos + ret);
    if (offset == RVFILE_CUR) rvseek(file, ret, RVFILE_SEEK_CUR);
    return ret;
}

bool rvtrim(rvfile_t* file, uint64_t offset, uint64_t count)
{
    if (!file) return false;
#if defined(POSIX_FILE_IMPL) && defined(__linux__) && defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
    return fallocate(file->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, count) == 0;
#elif defined(POSIX_FILE_IMPL) && defined(__FreeBSD__) && defined(SYS_fspacectl)
    struct {
        off_t r_offset;
        off_t r_len;
    } rmsr = {
        .r_offset = offset,
        .r_len = count,
    };
    // Use fspacectl(SPACECTL_DEALLOC) added in FreeBSD 14
    return syscall(SYS_fspacectl, file->fd, 1, &rmsr, 0, NULL) == 0;
#elif defined(WIN32_FILE_IMPL)
    SET_ZERO_DATA_INFO fz = {0};
    DWORD tmp = 0;
    fz.FileOffset.QuadPart = offset;
    fz.BeyondFinalZero.QuadPart = offset + count;
    return DeviceIoControl(file->handle, DEVIOCTL_SET_ZERO_DATA, &fz, sizeof(fz), NULL, 0 , &tmp, NULL);
#else
    UNUSED(file);
    UNUSED(offset);
    UNUSED(count);
    return false;
#endif
}

bool rvseek(rvfile_t* file, int64_t offset, uint8_t startpos)
{
    if (!file) return false;
    if (startpos == RVFILE_SEEK_CUR) {
        atomic_add_uint64(&file->pos, offset);
        return true;
    } else if (startpos == RVFILE_SEEK_END) {
        offset = rvfilesize(file) - offset;
    } else if (startpos != RVFILE_SEEK_SET) {
        return false;
    }
    if (offset < 0) return false;
    atomic_store_uint64(&file->pos, offset);
    return true;
}

uint64_t rvtell(rvfile_t* file)
{
    if (!file) return -1;
    return atomic_load_uint64(&file->pos);
}

bool rvfsync(rvfile_t* file)
{
    if (!file) return false;
#if defined(POSIX_FILE_IMPL)
    return fsync(file->fd) == 0;
#elif defined(WIN32_FILE_IMPL)
    return FlushFileBuffers(file->handle);
#else
    return fflush(file->fp) == 0;
#endif
}

static bool rvfile_grow_generic(rvfile_t* file, uint64_t length)
{
    bool ret = true;
    if (length && length > rvfilesize(file)) {
        // Grow the file by re-writing one byte at the new end
        char tmp = 0;
#if defined(POSIX_FILE_IMPL) || defined(WIN32_FILE_IMPL)
        if (!rvread(file, &tmp, 1, length - 1)) {
            // NOTE: This is not perfectly thread safe if there
            // are writers currently extending the end of file.
            ret = !!rvwrite(file, &tmp, 1, length - 1);
        }
#else
        spin_lock_slow(&file->lock);
        fseek(file->fp, length - 1, SEEK_SET);
        if (!fread(&tmp, 1, 1, file->fp)) {
            fseek(file->fp, length - 1, SEEK_SET);
            ret = !!fwrite(&tmp, 1, 1, file->fp);
            fflush(file->fp);
        }
        file->pos_state = RVFILE_POS_INVALID;
        spin_unlock(&file->lock);
#endif
        if (ret) rvfile_grow_internal(file, length);
    }
    return ret;
}

bool rvtruncate(rvfile_t* file, uint64_t length)
{
    if (!file) return false;
#if defined(POSIX_FILE_IMPL)
    if (ftruncate(file->fd, length)) return false;
    atomic_store_uint64(&file->size, length);
#elif defined(WIN32_FILE_IMPL)
    LONG high_len = length >> 32;
    SetFilePointer(file->handle, (uint32_t)length, &high_len, FILE_BEGIN);
    if (!SetEndOfFile(file->handle)) return false;
    atomic_store_uint64(&file->size, length);
#else
    if (length < rvfilesize(file)) {
        // Generic implementation can't shrink the file
        return false;
    }
    if (!rvfile_grow_generic(file, length)) return false;
#endif
    return true;
}

bool rvfallocate(rvfile_t* file, uint64_t length)
{
    if (!file) return false;
    if (length > rvfilesize(file)) {
#if defined(POSIX_FILE_IMPL) && (defined(__linux__) || defined(__FreeBSD__))
        if (posix_fallocate(file->fd, length - 1, 1) == 0) {
            rvfile_grow_internal(file, length);
            return true;
        }
#endif
        return rvfile_grow_generic(file, length);
    }
    return true;
}

int rvfile_get_posix_fd(rvfile_t* file)
{
#if defined(POSIX_FILE_IMPL)
    return file->fd;
#elif defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
    return fileno(file->fp);
#else
    UNUSED(file);
    return -1;
#endif
}

void* rvfile_get_win32_handle(rvfile_t* file)
{
#if defined(WIN32_FILE_IMPL)
    return file->handle;
#elif defined(_WIN32) && defined(UNDER_CE)
    return (void*)_fileno(file->fp);
#elif defined(_WIN32) && !defined(UNDER_CE)
    return (void*)_get_osfhandle(_fileno(file->fp));
#else
    UNUSED(file);
    return NULL;
#endif
}

/*
 * Block device layer
 */

// Raw block device implementation
// Be careful with function prototypes
static const blkdev_type_t blkdev_type_raw = {
    .name = "blk-raw",
    .close = (void*)rvclose,
    .read = (void*)rvread,
    .write = (void*)rvwrite,
    .trim = (void*)rvtrim,
};

static blkdev_t* blk_raw_open(const char* filename, uint8_t filemode)
{
    rvfile_t* file = rvopen(filename, filemode);
    if (!file) return NULL;
    blkdev_t* dev = safe_new_obj(blkdev_t);
    dev->type = &blkdev_type_raw;
    dev->size = rvfilesize(file);
    dev->data = file;
    return dev;
}

blkdev_t* blk_dedup_open(const char* filename, uint8_t filemode);

static bool check_file_ext(const char* filename, const char* ext)
{
    const char* r = NULL;
    while (true) {
        const char* tmp = rvvm_strfind(filename, ext);
        if (tmp == NULL) break;
        r = tmp;
        filename = r + 1;
    }
    return r && rvvm_strcmp(r, ext);
}

blkdev_t* blk_open(const char* filename, uint8_t opts)
{
    uint8_t filemode = (opts & BLKDEV_RW) ? (RVFILE_RW | RVFILE_EXCL) : 0;
    if (check_file_ext(filename, ".bdv")) {
        return NULL;
    }
    if (check_file_ext(filename, ".qcow2")) {
        rvvm_error("QCOW2 images aren't supported yet");
        return NULL;
    }
    return blk_raw_open(filename, filemode);
}

void blk_close(blkdev_t* dev)
{
    if (dev) {
        dev->type->close(dev->data);
        free(dev);
    }
}
