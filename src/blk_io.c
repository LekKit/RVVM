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

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

#include <string.h>
#include "blk_io.h"
#include "utils.h"
#include "spinlock.h"
#include "threading.h"

#if (defined(__unix__) || defined(__APPLE__)) && !defined(USE_STDIO)
// POSIX implementation using open, pread, pwrite...
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#define POSIX_FILE_IMPL

#ifdef __linux__
#include <sys/syscall.h>
#endif

static bool try_lock_fd(int fd)
{
    struct flock flk = {0};
    flk.l_type = F_WRLCK;
    flk.l_whence = SEEK_SET;
    flk.l_start = 0;
    flk.l_len = 0;
    return fcntl(fd, F_SETLK, &flk) == 0 || (errno != EACCES && errno != EAGAIN);
}

#elif defined(_WIN32) && !defined(UNDER_CE) && !defined(USE_STDIO)
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

// 64-bit offset & UTF-8 filenames on win32
#if defined(_WIN32) && !defined(UNDER_CE)
#if     _WIN32_WINNT < 0x0600
#undef  _WIN32_WINNT
#define _WIN32_WINNT   0x0600
#endif
#include <windows.h>
static FILE* fopen_utf8(const char* name, const char* mode)
{
    size_t name_len = strlen(name);
    wchar_t* u16_name = safe_calloc(sizeof(wchar_t), name_len + 1);
    wchar_t u16_mode[16] = {0};
    MultiByteToWideChar(CP_UTF8, 0, name, name_len, u16_name, name_len + 1);
    MultiByteToWideChar(CP_UTF8, 0, mode, strlen(mode), u16_mode, 16);
    FILE* file = _wfopen(u16_name, u16_mode);
    free(u16_name);

    // Retry opening existing file using system locale (oh...)
    if (!file && mode[0] != 'w') file = fopen(name, mode);
    return file;
}
#define fopen fopen_utf8
#define fseek _fseeki64
#define ftell _ftelli64
#endif

#define FILE_POS_INVALID 0
#define FILE_POS_READ    1
#define FILE_POS_WRITE   2
#endif

struct blk_io_rvfile {
    char* filepath;
    uint64_t size;
    uint64_t pos;
#if defined(POSIX_FILE_IMPL)
    int fd;
#elif defined(WIN32_FILE_IMPL)
    HANDLE handle;
#else
    uint64_t pos_real;
    uint8_t  pos_state;
    spinlock_t lock;
    FILE* fp;
#endif
};

rvfile_t* rvopen(const char* filepath, uint8_t mode)
{
#if defined(POSIX_FILE_IMPL)
    int fd, open_flags = 0;
    if (mode & RVFILE_RW) {
        if (mode & RVFILE_TRUNC) open_flags |= O_TRUNC;
        if (mode & RVFILE_CREAT) {
            open_flags |= O_CREAT;
            if (mode & RVFILE_EXCL) open_flags |= O_EXCL;
        }
        open_flags |= O_RDWR;
    } else open_flags |= O_RDONLY;

    fd = open(filepath, open_flags, 0644);
    if (fd == -1) {
        return NULL;
    }

    if ((mode & RVFILE_EXCL) && !try_lock_fd(fd)) {
        rvvm_error("File %s is busy", filepath);
        close(fd);
        return NULL;
    }

    rvfile_t* file = safe_calloc(sizeof(rvfile_t), 1);
    file->filepath = safe_calloc(rvvm_strlen(filepath)+1, 1);
    memcpy(file->filepath, filepath, rvvm_strlen(filepath));
    struct stat file_stat = {0};
    fstat(fd, &file_stat);
    file->size = file_stat.st_size;
    file->pos = 0;
    file->fd = fd;

    rvvm_info("RVFILE \"%s\" opened {%s%s%s%s}", file->filepath,
              mode & RVFILE_RW ? " RVFILE_RW " : " RVFILE_RDONLY ",
              mode & RVFILE_TRUNC ? "| RVFILE_TRUNC " : "", 
              mode & RVFILE_CREAT ? "| RVFILE_CREAT" : "", 
              mode & RVFILE_EXCL ? "| RVFILE_EXCL" : "");

    return file;
#elif defined(WIN32_FILE_IMPL)
    DWORD access = GENERIC_READ;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD disp = OPEN_EXISTING;
    if (mode & RVFILE_RW) {
        if (mode & RVFILE_CREAT) {
            disp = OPEN_ALWAYS;
            if (mode & RVFILE_TRUNC) disp = CREATE_ALWAYS;
        } else {
            if (mode & RVFILE_TRUNC) disp = TRUNCATE_EXISTING;
        }

        access |= GENERIC_WRITE;
    }
    if (mode & RVFILE_EXCL) share = 0;
    size_t path_len = strlen(filepath);
    wchar_t* u16_path = safe_calloc(sizeof(wchar_t), path_len + 1);
    MultiByteToWideChar(CP_UTF8, 0, filepath, path_len, u16_path, path_len + 1);
    HANDLE handle = CreateFileW(u16_path, access, share, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
    free(u16_path);

    // Retry opening existing file using system locale (oh...)
    if (handle == INVALID_HANDLE_VALUE && (mode & (RVFILE_CREAT | RVFILE_TRUNC)) == 0) {
        handle = CreateFileA(filepath, access, share, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE) rvvm_warn("Non UTF-8 filepath passed to rvopen()");
    }

    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) rvvm_error("File %s is busy", filepath);
        return NULL;
    }
    DWORD sizeh;
    DWORD sizel = GetFileSize(handle, &sizeh);
    DWORD tmp;
    DeviceIoControl(handle, DEVIOCTL_SET_SPARSE, NULL, 0, NULL, 0, &tmp, NULL);

    rvfile_t* file = safe_calloc(sizeof(rvfile_t), 1);
    file->filepath = safe_calloc(rvvm_strlen(filepath)+1, 1);
    memcpy(file->filepath, filepath, rvvm_strlen(filepath));
    file->size = ((uint64_t)sizeh) << 32 | sizel;
    file->pos = 0;
    file->handle = handle;

    rvvm_info("RVFILE \"%s\" opened {%s%s%s%s}", file->filepath,
              mode & RVFILE_RW ? " RVFILE_RW " : " RVFILE_RDONLY ",
              mode & RVFILE_TRUNC ? "| RVFILE_TRUNC " : "", 
              mode & RVFILE_CREAT ? "| RVFILE_CREAT" : "", 
              mode & RVFILE_EXCL ? "| RVFILE_EXCL" : "");

    return file;
#else
    const char* open_mode;
    if ((mode & RVFILE_TRUNC) && (mode & RVFILE_RW)) {
        open_mode = "wb+";
    } else if (mode & RVFILE_RW) {
        open_mode = "rb+";
    } else {
        open_mode = "rb";
    }

    FILE* fp = fopen(filepath, open_mode);
    if (!fp && (mode & RVFILE_RW) && (mode & RVFILE_CREAT)) fp = fopen(filepath, "wb+");
    if (!fp) {
        return NULL;
    }

    rvfile_t* file = safe_calloc(sizeof(rvfile_t), 1);
    file->filepath = safe_calloc(rvvm_strlen(filepath)+1, 1);
    memcpy(file->filepath, filepath, rvvm_strlen(filepath));
    fseek(fp, 0, SEEK_END);
    file->size = ftell(fp);
    file->pos = 0;
    file->pos_state = FILE_POS_INVALID;
    file->fp = fp;
    spin_init(&file->lock);

    rvvm_info("RVFILE \"%s\" opened {%s%s%s%s}", file->filepath,
              mode & RVFILE_RW ? " RVFILE_RW " : " RVFILE_RDONLY ",
              mode & RVFILE_TRUNC ? "| RVFILE_TRUNC " : "", 
              mode & RVFILE_CREAT ? "| RVFILE_CREAT" : "", 
              mode & RVFILE_EXCL ? "| RVFILE_EXCL" : "");

    return file;
#endif
}

void rvclose(rvfile_t *file)
{
    if (!file) return;
#if defined(POSIX_FILE_IMPL)
    close(file->fd);
#elif defined(WIN32_FILE_IMPL)
    CloseHandle(file->handle);
#else
    spin_lock_slow(&file->lock);
    fclose(file->fp);
    spin_unlock(&file->lock);
#endif
    rvvm_info("RVFILE \"%s\" closed", file->filepath);
    free(file->filepath);
    free(file);
}

uint64_t rvfilesize(rvfile_t* file)
{
    if (!file) return 0;
    return file->size;
}

size_t rvread(rvfile_t* file, void* destination, size_t count, uint64_t offset)
{
    if (!file) return 0;
    uint64_t pos_real = (offset == RVFILE_CURPOS) ? file->pos : offset;
#if defined(POSIX_FILE_IMPL)
    ssize_t ret = pread(file->fd, destination, count, pos_real);
    if (ret < 0) ret = 0;
#elif defined(WIN32_FILE_IMPL)
    OVERLAPPED overlapped = { .OffsetHigh = pos_real >> 32, .Offset = (uint32_t)pos_real };
    DWORD ret = 0;
    ReadFile(file->handle, destination, count, &ret, &overlapped);
#else
    spin_lock_slow(&file->lock);
    if (pos_real != file->pos_real || !(file->pos_state & FILE_POS_READ)) {
        fseek(file->fp, pos_real, SEEK_SET);
    }
    size_t ret = fread(destination, 1, count, file->fp);
    file->pos_real = pos_real + ret;
    file->pos_state = FILE_POS_READ;
    spin_unlock(&file->lock);
#endif
    if (offset == RVFILE_CURPOS) file->pos += ret;
    return ret;
}

size_t rvwrite(rvfile_t* file, const void* source, size_t count, uint64_t offset)
{
    if (!file) return 0;
    uint64_t pos_real = (offset == RVFILE_CURPOS) ? file->pos : offset;
#if defined(POSIX_FILE_IMPL)
    ssize_t ret = pwrite(file->fd, source, count, pos_real);
    if (ret < 0) ret = 0;
#elif defined(WIN32_FILE_IMPL)
    OVERLAPPED overlapped = { .OffsetHigh = pos_real >> 32, .Offset = (uint32_t)pos_real };
    DWORD ret = 0;
    WriteFile(file->handle, source, count, &ret, &overlapped);
#else
    spin_lock_slow(&file->lock);
    if (pos_real != file->pos_real || !(file->pos_state & FILE_POS_WRITE)) {
        fseek(file->fp, pos_real, SEEK_SET);
    }
    size_t ret = fwrite(source, 1, count, file->fp);
    file->pos_real = pos_real + ret;
    file->pos_state = FILE_POS_WRITE;
    spin_unlock(&file->lock);
#endif
    if (offset == RVFILE_CURPOS) file->pos += ret;
    if (pos_real + ret > file->size) file->size = pos_real + ret;
    return ret;
}

bool rvtrim(rvfile_t* file, uint64_t offset, uint64_t count)
{
    if (!file) return false;
#if defined(POSIX_FILE_IMPL) && defined(__linux__) && defined(__NR_fallocate)
    // FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE
    return syscall(__NR_fallocate, file->fd, 0x3, offset, count) == 0;
#elif defined(WIN32_FILE_IMPL)
    SET_ZERO_DATA_INFO fz;
    DWORD tmp;
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
    if (!file || startpos > RVFILE_END) return false;
    if (startpos == RVFILE_CUR) {
        offset = file->pos + offset;
    } else if (startpos == RVFILE_END) {
        offset = file->size - offset;
    }
    if (startpos != RVFILE_SET && offset < 0) return false;
    file->pos = (uint64_t)offset;
    return true;
}

uint64_t rvtell(rvfile_t* file)
{
    if (!file) return -1;
    return file->pos;
}

bool rvflush(rvfile_t* file)
{
    if (!file) return false;
    // Do not issue kernel-side buffer flushing
#if defined(POSIX_FILE_IMPL)
    //return fsync(file->fd) == 0;
    return true;
#elif defined(WIN32_FILE_IMPL)
    //return FlushFileBuffers(file->handle);
    return true;
#else
    return fflush(file->fp) == 0;
#endif
}

bool rvtruncate(rvfile_t* file, uint64_t length)
{
    if (!file) return false;
    file->size = length;
#if defined(POSIX_FILE_IMPL)
    return ftruncate(file->fd, length) == 0;
#elif defined(WIN32_FILE_IMPL)
    LONG high_len = length >> 32;
    SetFilePointer(file->handle, (uint32_t)length, &high_len, FILE_BEGIN);
    return SetEndOfFile(file->handle);
#else
    char tmp = 0;
    if (length) {
        spin_lock_slow(&file->lock);
        fseek(file->fp, length - 1, SEEK_SET);
        fread(&tmp, 1, 1, file->fp);
        fseek(file->fp, length - 1, SEEK_SET);
        fwrite(&tmp, 1, 1, file->fp);
        file->pos_state = FILE_POS_INVALID;
        spin_unlock(&file->lock);
    }
    return true;
#endif
}

/*
 * Async IO
 */

static void* async_io_task(void** data)
{
    rvaio_op_t* iolist = (rvaio_op_t*)data[0];
    size_t count = (size_t)data[1];
    rvfile_async_callback_t va_callback = (rvfile_async_callback_t)data[2];
    void* va_userdata = data[3];

    uint8_t va_result = ASYNC_IO_DONE;
    bool op_result;
    for (size_t i=0; i<count; ++i) {
        switch (iolist[i].opcode) {
            case RVFILE_ASYNC_READ:
                op_result = rvread(iolist[i].file, iolist[i].buffer, iolist[i].length, iolist[i].offset) == iolist[i].length;
                break;
            case RVFILE_ASYNC_WRITE:
                op_result = rvwrite(iolist[i].file, iolist[i].buffer, iolist[i].length, iolist[i].offset) == iolist[i].length;
                break;
            case RVFILE_ASYNC_TRIM:
                op_result = rvtrim(iolist[i].file, iolist[i].offset, iolist[i].length);
                break;
            default:
                rvvm_warn("Unknown opcode %d in async_io_task()!", iolist[i].opcode);
                return NULL;
        }
        if (iolist[i].callback) iolist[i].callback(iolist[i].file, iolist[i].userdata, op_result ? ASYNC_IO_DONE : ASYNC_IO_FAIL);
        if (!op_result) va_result = ASYNC_IO_FAIL;
    }
    if (va_callback) va_callback(NULL, va_userdata, va_result);
    free(iolist);
    return NULL;
}

bool rvread_async(rvfile_t* file, void* destination, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    if (!file || offset == RVFILE_CURPOS) return false;
    rvaio_op_t op = {file, destination, offset, count, userdata, callback, RVFILE_ASYNC_READ};
    return rvasync_va(&op, 1, NULL, NULL);
}

bool rvwrite_async(rvfile_t* file, const void* source, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    if (!file || offset == RVFILE_CURPOS) return false;
    rvaio_op_t op = {file, (void*)source, offset, count, userdata, callback, RVFILE_ASYNC_WRITE};
    return rvasync_va(&op, 1, NULL, NULL);
}

bool rvfsync_async(rvfile_t* file)
{
    UNUSED(file);
    return false;
}

bool rvasync_va(rvaio_op_t* iolist, size_t count, rvfile_async_callback_t callback, void* userdata)
{
    rvaio_op_t* task_iolist = safe_calloc(sizeof(rvaio_op_t), count);
    memcpy(task_iolist, iolist, sizeof(rvaio_op_t) * count);
    void* args[4] = {task_iolist, (void*)count, (void*)callback, userdata};
    thread_create_task_va(async_io_task, args, 4);
    return true;
}

/*
 * Block device layer
 */

// Raw block device implementation
// Be careful with function prototypes
static blkdev_type_t blkdev_type_raw = {
    .name = "raw",
    .close = (void*)rvclose,
    .read = (void*)rvread,
    .write = (void*)rvwrite,
    .trim = (void*)rvtrim,
    .sync = (void*)rvflush,
};

static bool blk_init_raw(blkdev_t* dev, rvfile_t* file)
{
    dev->type = &blkdev_type_raw;
    dev->size = rvfilesize(file);
    dev->data = file;
    return true;
}

blkdev_t* blk_open(const char* filename, uint8_t opts)
{
    uint8_t filemode = 0;
    if (opts & RVFILE_RW) filemode |= (RVFILE_RW | RVFILE_EXCL);
    rvfile_t* file = rvopen(filename, filemode);
    if (!file) return NULL;

    blkdev_t* dev = safe_calloc(sizeof(blkdev_t), 1);
#if 0
    char magic_buf[4] = {0};
    rvread(file, magic_buf, 4, 0);
    if (memcmp(magic_buf, "RVVD", 4))
        blk_init_rvvd(dev, file);
    else
#endif
    blk_init_raw(dev, file);

    dev->pos = 0;
    return dev;
}

void blk_close(blkdev_t* dev)
{
    if (dev) {
        dev->type->close(dev->data);
        free(dev);
    }
}
