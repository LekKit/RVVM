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

#include "blk_io.h"
#include "utils.h"
#include "threading.h"

#ifdef __unix__
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>

#ifdef __linux__
#include <linux/aio_abi.h> 
#include <sys/syscall.h>
int fallocate(int fd, int mode, off_t offset, off_t len);
#endif
#else
#include <stdio.h>
#endif

/*
 * On Windows, ftell/fseek use 32-bit offsets,
 * this breaks when mounting >=4GB images.
 * Using _fseeki64, _ftelli64 therefore
 */

#ifdef _WIN32
#define fseek _fseeki64
#define ftell _ftelli64
#endif

#define IO_MAX_OPS 512
//#define AIO_LINUX

rvfile_t* rvopen(const char* filepath, uint8_t mode)
{
#if defined(__unix__)
    int fd, open_flags = 0;
    if (mode & RVFILE_RW) {
        if (mode & RVFILE_CREAT) open_flags |= O_CREAT;
        open_flags |= O_RDWR;
    } else open_flags |= O_RDONLY;
    
    fd = open(filepath, open_flags, 0644);
    if (fd == -1) {
        return NULL;
    }

    if ((mode & RVFILE_EXCL) && flock(fd, LOCK_EX) == EWOULDBLOCK) {
        close(fd);
        return NULL;
    }
    
    rvfile_t* file = safe_calloc(sizeof(rvfile_t), 1);
    file->fd = fd;

#if defined(__linux__)
    aio_context_t* ctx = safe_calloc(sizeof(aio_context_t), 1);
    syscall(SYS_io_setup, IO_MAX_OPS, ctx);
    file->aio_context = (void*)ctx;
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
    return (rvfile_t*)fopen(filepath, open_mode);
#endif
}

void rvclose(rvfile_t *file)
{
    if (!file) return;
#if defined(__unix__)
    close(file->fd);
    free(file);
#else
    fclose((FILE*)file);
#endif
}

uint64_t rvfilesize(rvfile_t* file)
{
    if (!file) return 0;
#if defined(__unix__)
    return lseek(file->fd, 0, SEEK_END);
#else
    uint64_t cur = ftell((FILE*)file);
    uint64_t size;
    fseek((FILE*)fp, 0, SEEK_END);
    size = ftell((FILE*)fp);
    fseek((FILE*)fp, cur, SEEK_SET);
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
    userdata->lenght = length;
    userdata->callback_handler = callback_handler;
    op->aio_data = (uint64_t)userdata;
    op->aio_lio_opcode = IOCB_CMD_PREAD;
    if (opcode == RVFILE_ASYNC_WRITE) op->aio_lio_opcode = IOCB_CMD_PWRITE;
    op->aio_fildes = file->fd;
    op->aio_buf = (uint64_t)buffer;
    op->aio_offset = offset;
    op->aio_nbytes = count;
    op->aio_resfd = file->resfd;
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
        result = async_task_run(task);
    }

    task->callback(task->file, task->userdata, result);
    
    free(task);
    return NULL;
}

static bool create_async_task(rvfile_t* file, uint8_t opcode, void* buffer, size_t length, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    async_task_t* task = safe_calloc(sizeof(async_task_t), 1);
    task->file = file;
    task->buffer = buffer;
    task->offset = offset;
    task->length = length;
    task->callback = callback;
    task->userdata = userdata;
    task->opcode = opcode;
    return thread_detach(thread_create(async_task, task));
}

#endif

size_t rvread(rvfile_t* file, void* destination, size_t count, uint64_t offset)
{
    if (!file) return 0;
#if defined(__unix__)
    int res = 0;
    if (offset == RVFILE_CURPOS) res = read(file->fd, destination, count);
    else res = pread(file->fd, destination, count, offset);
    if (res < 0) return 0;
    return res;
#else
    if (offset != RVFILE_CURPOS) fseek(file, offset, SEEK_SET);
    return fread(destination, count, 1, (FILE*)file);
#endif
}

size_t rvwrite(rvfile_t* file, const void* source, size_t count, uint64_t offset)
{
    if (!file) return 0;
#if defined(__unix__)
    int res = 0;
    if (offset == RVFILE_CURPOS) res = write(file->fd, source, count);
    else res = pwrite(file->fd, source, count, offset);
    if (res < 0) return 0;
    return res;
#else
    if (offset != RVFILE_CURPOS) fseek(file, offset, SEEK_SET);
    return fwrite(source, count, 1, (FILE*)file);
#endif
}

bool rvtrim(rvfile_t* file, uint64_t offset, uint64_t count)
{
    if (!file) return 0;
#ifdef __linux__
    // FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE
    return fallocate(file->fd, 0x3, offset, count) == 0;
#else
    UNUSED(file);
    UNUSED(offset);
    UNUSED(count);
#endif
}

bool rvseek(rvfile_t* file, uint64_t offset, uint8_t startpos)
{
    if (!file) return -1;
    int whence = SEEK_SET;
    if (startpos == RVFILE_CUR) whence = SEEK_CUR;
    if (startpos == RVFILE_END) whence = SEEK_END;
#if defined(__unix__)
    return lseek(file->fd, offset, whence) != (off_t)-1;
#else
    return fseek((FILE*)file, offset, whence);
#endif
}

uint64_t rvtell(rvfile_t* file)
{
    if (!file) return -1;
#if defined(__unix__)
    return lseek(file->fd, 0, SEEK_CUR);
#else
    return ftell((FILE*)file);
#endif
}

bool rvflush(rvfile_t* file)
{
#if defined(__unix__)
    return fsync(file->fd);
#else
    UNUSED(file);
#endif
}

uint64_t rvtruncate(rvfile_t* file, uint64_t length)
{
#if defined(__unix__)
    return ftruncate(file->fd, length);
#else
    return 0;
#endif
}

bool rvread_async(rvfile_t* file, void* destination, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    if (!file || offset == RVFILE_CURPOS) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->aio_context;
    struct iocb* op = create_linux_request(file, IOCB_CMD_PREAD, (uint64_t*)destination, offset, count, callback, userdata);
    return syscall(SYS_io_submit, ctx, 1, op);
#else
    if (create_async_task(file, RVFILE_ASYNC_READ, destination, count, offset, callback, userdata)) {
        return true;
    }

    // Fallback to sync IO
    rvvm_warn("Falling back from async to sync IO");
    size_t ret = rvread(file, destination, count, offset);
    callback(file, userdata, (ret == count) ? ASYNC_IO_DONE : ASYNC_IO_FAIL);
    return true;
#endif
}

bool rvwrite_async(rvfile_t* file, const void* source, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata)
{
    if (!file || offset == RVFILE_CURPOS) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->aio_context;
    struct iocb* op = create_linux_request(file, IOCB_CMD_PWRITE, (uint64_t*)source, offset, count, callback, userdata);
    return syscall(SYS_io_submit, ctx, 1, op);
#else
    if (create_async_task(file, RVFILE_ASYNC_WRITE, (void*)source, count, offset, callback, userdata)) {
        return true;
    }

    // Fallback to sync IO
    rvvm_warn("Falling back from async to sync IO");
    size_t ret = rvwrite(file, source, count, offset);
    callback(file, userdata, (ret == count) ? ASYNC_IO_DONE : ASYNC_IO_FAIL);
    return true;
#endif
}

bool rvfsync_async(rvfile_t* file)
{
    if (!file) return false;
#if defined(__linux__) && defined(AIO_LINUX)
    aio_context_t* ctx = (aio_context_t*)file->aio_context;
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
    aio_context_t* ctx = (aio_context_t*)file->aio_context;
    struct iocb** rlist = safe_calloc(sizeof(struct iocb), count);
    for (size_t i = 0; i < count; i++) {
        rlist[i] = create_linux_request(file, iolist[i].opcode, iolist[i].buffer, iolist[i].offset, iolist[i].length, callback, userdata);
    }
    return syscall(SYS_io_submit, ctx, count, rlist) >= 0;
#else
    rvaio_op_t* task_iolist = safe_calloc(sizeof(rvaio_op_t), count);
    memcpy(task_iolist, iolist, sizeof(rvaio_op_t) * count);
    return create_async_task(file, RVFILE_ASYNC_VA, task_iolist, count, 0, callback, userdata);
#endif
}

