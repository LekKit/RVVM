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
#include "threading.h"

//#define AIO_LINUX

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
    fcntl(fd, F_SETLK, &flk);
    return errno != EACCES && errno != EAGAIN;
}

#ifdef __linux__
#include <sys/syscall.h>
#ifdef AIO_LINUX
#include <linux/aio_abi.h>
#endif
#endif

#else
#include <stdio.h>
#endif

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

#define FILE_POS_INVALID 0
#define FILE_POS_READ    1
#define FILE_POS_WRITE   2

rvfile_t* rvopen(const char* filepath, uint8_t mode)
{
#if defined(__unix__)
    int fd, open_flags = 0;
    if (mode & RVFILE_RW) {
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
    file->pos = 0;
    file->fd = fd;

#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = safe_calloc(sizeof(aio_context_t), 1);
    syscall(SYS_io_setup, IO_MAX_OPS, ctx);
    file->ptr = (void*)ctx;
#endif

    return file;
#else
    const char* open_mode;
    if ((mode & RVFILE_CREAT) && (mode & RVFILE_RW)) {
        open_mode = "wb";
    } else if (mode & RVFILE_RW) {
        open_mode = "rb+";
    } else {
        open_mode = "rb";
    }

    FILE* fp = fopen(filepath, open_mode);
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
    file->pos = 0;
    file->pos_state = FILE_POS_INVALID;
    file->ptr = (void*)fp;
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
    fclose((FILE*)file->ptr);
    spin_unlock(&file->lock);
    free(file);
#endif
}

uint64_t rvfilesize(rvfile_t* file)
{
    if (!file) return 0;
#if defined(__unix__)
    struct stat file_stat;
    fstat(file->fd, &file_stat);
    return file_stat.st_size;
#else
    spin_lock_slow(&file->lock);
    fseek((FILE*)file->ptr, 0, SEEK_END);
    uint64_t size = ftell((FILE*)file->ptr);
    file->pos_state = FILE_POS_INVALID;
    spin_unlock(&file->lock);
    return size;
#endif
}

#if defined(__linux__) && defined(AIO_LINUX)

static struct iocb* create_linux_request(rvfile_t* file, uint16_t opcode, uint64_t* buffer, uint64_t offset, uint64_t count, rvfile_async_callback_t callback_handler, void* user_data)
{
    static uint64_t request_id = 0;
    struct iocb* op = safe_calloc(sizeof(struct iocb), 1);
    aio_request_data* userdata = safe_calloc(sizeof(aio_request_data), 1);
    userdata->request_id = request_id++;
    userdata->file = file;
    userdata->userdata = user_data;
    userdata->offset = offset;
    userdata->length = count;
    userdata->callback_handler = callback_handler;
    op->aio_data = (uint64_t)userdata;
    op->aio_lio_opcode = IOCB_CMD_PREAD;
    if (opcode == RVFILE_ASYNC_WRITE) op->aio_lio_opcode = IOCB_CMD_PWRITE;
    op->aio_fildes = file->fd;
    op->aio_buf = (uint64_t)buffer;
    op->aio_offset = offset;
    op->aio_nbytes = count;
    //op->aio_resfd = file->resfd;
    op->aio_flags = IOCB_FLAG_RESFD;
    return op;
}

#else

typedef struct {
    rvfile_t* file;
    void*     buffer;
    uint64_t  offset;
    size_t    length;
    rvfile_async_callback_t callback;
    void*    userdata;
    uint8_t  opcode;
} async_task_t;

static bool async_task_run(async_task_t* task)
{
    switch (task->opcode) {
        case RVFILE_ASYNC_READ:
            return rvread(task->file, task->buffer, task->length, task->offset) == task->length;
        case RVFILE_ASYNC_WRITE:
            return rvwrite(task->file, task->buffer, task->length, task->offset) == task->length;
        case RVFILE_ASYNC_TRIM:
            return rvtrim(task->file, task->offset, task->length);
        default:
            rvvm_warn("Unknown opcode %d in async_task_run()!", task->opcode);
            return false;
    }
}

static void* async_task(void* data)
{
    async_task_t* task = (async_task_t*)data;
    rvaio_op_t* iolist;
    uint8_t result = ASYNC_IO_DONE;

    if (task->opcode == RVFILE_ASYNC_VA) {
        iolist = (rvaio_op_t*)task->buffer;
        for (size_t i=0; i<task->length; ++i) {
            switch (iolist[i].opcode) {
                case RVFILE_ASYNC_READ:
                    if (rvread(task->file, iolist[i].buffer, iolist[i].length, iolist[i].offset) != iolist[i].length)
                        result = ASYNC_IO_FAIL;
                    break;
                case RVFILE_ASYNC_WRITE:
                    if (rvwrite(task->file, iolist[i].buffer, iolist[i].length, iolist[i].offset) != iolist[i].length)
                        result = ASYNC_IO_FAIL;
                    break;
                case RVFILE_ASYNC_TRIM:
                    if (!rvtrim(task->file, iolist[i].offset, iolist[i].length))
                        result = ASYNC_IO_FAIL;
                    break;
                default:
                    rvvm_warn("Unknown opcode %d in async_task_va()!", iolist[i].opcode);
                    return false;
            }
        }

        free(iolist);
    } else {
        result = async_task_run(task) ? ASYNC_IO_DONE : ASYNC_IO_FAIL;
    }

    task->callback(task->file, task->userdata, result);

    free(task);
    return NULL;
}

static void create_async_task(rvfile_t* file, uint8_t opcode, void* buffer, size_t length, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    async_task_t* task = safe_calloc(sizeof(async_task_t), 1);
    task->file = file;
    task->buffer = buffer;
    task->offset = offset;
    task->length = length;
    task->callback = callback;
    task->userdata = userdata;
    task->opcode = opcode;
    thread_create_task(async_task, task);
}

#endif

size_t rvread(rvfile_t* file, void* destination, size_t count, uint64_t offset)
{
    if (!file) return 0;
#if defined(__unix__)
    ssize_t ret = 0;
    if (offset == RVFILE_CURPOS) {
#if 0
        if (file->pos_state == FILE_POS_INVALID) {
            lseek(file->fd, file->pos, SEEK_SET);
            file->pos_state = FILE_POS_READ;
        }
        ret = read(file->fd, destination, count);
#else
        ret = pread(file->fd, destination, count, file->pos);
#endif
        if (ret > 0) file->pos += ret;
    } else {
        ret = pread(file->fd, destination, count, offset);
    }
    if (ret < 0) ret = 0;
    return ret;
#else
    spin_lock_slow(&file->lock);
    if (offset != RVFILE_CURPOS) {
        fseek((FILE*)file->ptr, offset, SEEK_SET);
    } else if (!(file->pos_state & FILE_POS_READ)) {
        fseek((FILE*)file->ptr, file->pos, SEEK_SET);
    }
    size_t ret = fread(destination, 1, count, (FILE*)file->ptr);
    if (offset == RVFILE_CURPOS) {
        file->pos_state = FILE_POS_READ;
        file->pos += ret;
    } else {
        file->pos_state = FILE_POS_INVALID;
    }
    spin_unlock(&file->lock);
    return ret;
#endif
}

size_t rvwrite(rvfile_t* file, const void* source, size_t count, uint64_t offset)
{
    if (!file) return 0;
#if defined(__unix__)
    ssize_t ret = 0;
    if (offset == RVFILE_CURPOS) {
#if 0
        if (file->pos_state == FILE_POS_INVALID) {
            lseek(file->fd, file->pos, SEEK_SET);
            file->pos_state = FILE_POS_WRITE;
        }
        ret = write(file->fd, source, count);
#else
        ret = pwrite(file->fd, source, count, file->pos);
#endif
        if (ret > 0) file->pos += ret;
    } else {
        ret = pwrite(file->fd, source, count, offset);
    }
    if (ret < 0) ret = 0;
    return ret;
#else
    spin_lock_slow(&file->lock);
    if (offset != RVFILE_CURPOS) {
        fseek((FILE*)file->ptr, offset, SEEK_SET);
    } else if (!(file->pos_state & FILE_POS_WRITE)) {
        fseek((FILE*)file->ptr, file->pos, SEEK_SET);
    }
    size_t ret = fwrite(source, 1, count, (FILE*)file->ptr);
    if (offset == RVFILE_CURPOS) {
        file->pos_state = FILE_POS_WRITE;
        file->pos += ret;
    } else {
        file->pos_state = FILE_POS_INVALID;
    }
    spin_unlock(&file->lock);
    return ret;
#endif
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
    int whence = SEEK_SET;
    if (startpos == RVFILE_CUR) whence = SEEK_CUR;
    if (startpos == RVFILE_END) whence = SEEK_END;
    if (startpos == RVFILE_CUR) {
        offset = file->pos + offset;
        if (offset < 0) return false;
    }
    if (startpos == RVFILE_END) {
#if defined(__unix__)
        file->pos = lseek(file->fd, offset, whence);
        file->pos_state = FILE_POS_READ | FILE_POS_WRITE;
#else
        spin_lock_slow(&file->lock);
        fseek((FILE*)file->ptr, offset, whence);
        file->pos = ftell((FILE*)file->ptr);
        file->pos_state = FILE_POS_READ | FILE_POS_WRITE;
        spin_unlock(&file->lock);
#endif
    } else if (file->pos != (uint64_t)offset) {
        file->pos = (uint64_t)offset;
        file->pos_state = FILE_POS_INVALID;
    }
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
    return fflush((FILE*)file->ptr) == 0;
#endif
}

bool rvtruncate(rvfile_t* file, uint64_t length)
{
#if defined(__unix__)
    return ftruncate(file->fd, length) == 0;
#else
    char tmp = 0;
    if (length) {
        spin_lock_slow(&file->lock);
        fseek((FILE*)file->ptr, length - 1, SEEK_SET);
        fread(&tmp, 1, 1, (FILE*)file->ptr);
        fseek((FILE*)file->ptr, length - 1, SEEK_SET);
        fwrite(&tmp, 1, 1, (FILE*)file->ptr);
        file->pos_state = FILE_POS_INVALID;
        spin_unlock(&file->lock);
    }
    return true;
#endif
}

bool rvread_async(rvfile_t* file, void* destination, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    if (!file || offset == RVFILE_CURPOS) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->ptr;
    struct iocb* op = create_linux_request(file, IOCB_CMD_PREAD, (uint64_t*)destination, offset, count, callback, userdata);
    return syscall(SYS_io_submit, ctx, 1, op);
#else
    create_async_task(file, RVFILE_ASYNC_READ, destination, count, offset, callback, userdata);
    return true;
#endif
}

bool rvwrite_async(rvfile_t* file, const void* source, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    if (!file || offset == RVFILE_CURPOS) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->ptr;
    struct iocb* op = create_linux_request(file, IOCB_CMD_PWRITE, (uint64_t*)source, offset, count, callback, userdata);
    return syscall(SYS_io_submit, ctx, 1, op);
#else
    create_async_task(file, RVFILE_ASYNC_WRITE, (void*)source, count, offset, callback, userdata);
    return true;
#endif
}

bool rvfsync_async(rvfile_t* file)
{
    if (!file) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->ptr;
    struct iocb* op = create_linux_request(file, IOCB_CMD_FSYNC, NULL, 0, 0, NULL, NULL);
    return syscall(SYS_io_submit, ctx, 1, op);
#else
    UNUSED(file);
    return false;
#endif
}

bool rvasync_va(rvfile_t* file, rvaio_op_t* iolist, size_t count, rvfile_async_callback_t callback, void* userdata)
{
    if (!file) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->ptr;
    struct iocb** rlist = safe_calloc(sizeof(struct iocb), count);
    for (size_t i = 0; i < count; i++) {
        rlist[i] = create_linux_request(file, iolist[i].opcode, iolist[i].buffer, iolist[i].offset, iolist[i].length, callback, userdata);
    }
    return syscall(SYS_io_submit, ctx, count, rlist) >= 0;
#else
    rvaio_op_t* task_iolist = safe_calloc(sizeof(rvaio_op_t), count);
    memcpy(task_iolist, iolist, sizeof(rvaio_op_t) * count);
    create_async_task(file, RVFILE_ASYNC_VA, task_iolist, count, 0, callback, userdata);
    return true;
#endif
}
