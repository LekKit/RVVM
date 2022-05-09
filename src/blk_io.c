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

#define FILE_POS_INVALID 0
#define FILE_POS_READ    1
#define FILE_POS_WRITE   2

#ifdef __unix__
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>

static bool try_lock_fd(int fd)
{
    struct flock flk = {0};
    flk.l_type = F_WRLCK;
    flk.l_whence = SEEK_SET;
    flk.l_start = 0;
    flk.l_len = 0;
    return fcntl(fd, F_SETLK, &flk) == 0 || (errno != EACCES && errno != EAGAIN);
}

#ifdef __linux__
#include <sys/syscall.h>
#endif

#else
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>

/*
 * On Windows, ftell/fseek use 32-bit offsets.
 * Older releases of MinGW lack _fseeki64/_ftelli64,
 * but provide GNU fseeko64/ftello64, these work fine as well.
 */

#ifdef __MINGW32__
#define fseek fseeko64
#define ftell ftello64
#else
#define fseek _fseeki64
#define ftell _ftelli64
#endif

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
#endif

#endif

struct blk_io_rvfile {
    uint64_t size;
    uint64_t pos;
#ifdef __unix__
    int fd;
#else
    uint64_t pos_real;
    uint8_t  pos_state;
    spinlock_t lock;
    FILE* fp;
#endif
};

rvfile_t* rvopen(const char* filepath, uint8_t mode)
{
#if defined(__unix__)
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
    struct stat file_stat = {0};
    fstat(fd, &file_stat);
    file->size = file_stat.st_size;
    file->pos = 0;
    file->fd = fd;
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

#ifdef _WIN32
    if ((mode & RVFILE_EXCL) && !LockFile((HANDLE)_get_osfhandle(_fileno(fp)), 0, 0, 0, 0)) {
        rvvm_error("File %s is busy", filepath);
        fclose(fp);
        return NULL;
    }
#endif

    rvfile_t* file = safe_calloc(sizeof(rvfile_t), 1);
    fseek(fp, 0, SEEK_END);
    file->size = ftell(fp);
    file->pos = 0;
    file->pos_state = FILE_POS_INVALID;
    file->fp = fp;
    spin_init(&file->lock);
    return file;
#endif
}

void rvclose(rvfile_t *file)
{
    if (!file) return;
#if defined(__unix__)
    close(file->fd);
    free(file);
#else
    spin_lock_slow(&file->lock);
    fclose(file->fp);
    spin_unlock(&file->lock);
    free(file);
#endif
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
#if defined(__unix__)
    ssize_t ret = pread(file->fd, destination, count, pos_real);
    if (ret < 0) ret = 0;
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
#if defined(__unix__)
    ssize_t ret = pwrite(file->fd, source, count, pos_real);
    if (ret < 0) ret = 0;
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
    if (!file) return 0;
#if defined(__unix__) && defined(__linux__) && defined(__NR_fallocate)
    // FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE
    return syscall(__NR_fallocate, file->fd, 0x3, offset, count) == 0;
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
#if defined(__unix__)
    return fsync(file->fd);
#else
    return fflush(file->fp) == 0;
#endif
}

bool rvtruncate(rvfile_t* file, uint64_t length)
{
    file->size = length;
#if defined(__unix__)
    return ftruncate(file->fd, length) == 0;
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
