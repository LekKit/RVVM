/*
file.c - Cross-platform file API implementation for RVVM
Copyright (C) 2022 KotB <github.com/0xCatPKG>
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

#include "file.h"
#include "utils.h"

RVFILE* rvopen(const char* filepath, uint8_t mode)
{
#if defined(__unix__)
    int fd;
    if (mode & RVMODE_READWRITE) fd = open(filepath, O_RDWR | O_CREAT, 0666);
    else if (mode & RVMODE_WRITEONLY) fd = open(filepath, O_WRONLY | O_CREAT, 0666);
    else fd = open(filepath, O_RDONLY);
    if (!fd) return NULL;
#else
    FILE* fd;
    if (mode & RVMODE_READWRITE) fd = fopen(filepath, "wb+");
    else if (mode & RVMODE_WRITEONLY) fd = fopen(filepath, "wb");
    else fd = fopen(filepath, "rb");
    if (!fd) return NULL;
#endif

    RVFILE* file = safe_calloc(sizeof(RVFILE), 1);
#if defined(__unix__)
    file->fd = fd;
#else
    file->_fd = (void*)fd;
#endif
    file->mode = mode;
    return file;
}

int rvfileno(RVFILE* file)
{
#if defined (__unix__)
    return file->fd;
#else
    return fileno((FILE*)file->_fd);
#endif
}

void rvclose(RVFILE *file)
{
    #if defined (__unix__)
        close(file->fd);
    #else
        return fclose((FILE*)file->_fd);
    #endif
}

uint64_t rvread(void* destination, uint64_t count, uint64_t offset, RVFILE* file)
#if defined(__unix__)
{
    if (file->mode & RVMODE_ASYNC) {
        struct aiocb* aio_op = safe_calloc(sizeof(struct aiocb), 1);
        aio_op->aio_fildes = file->fd;
        aio_op->aio_offset = offset;
        aio_op->aio_buf = destination;
        aio_op->aio_nbytes = count;
        aio_op->aio_lio_opcode = LIO_READ;

        file->aio_op = aio_op;
        return 0;
    } else {
        return pread(file->fd, destination, count, offset);
    }
}
#else
{
    FILE* fp = (FILE*)file->_fd;
    fseek(fp, offset, SEEK_SET);
    return fread(destination, count, 1, fp);
}
#endif

uint64_t rvwrite(void* source, uint64_t count, uint64_t offset, RVFILE* file)
#if defined(__unix__)
{
    if (file->mode & RVMODE_ASYNC) {
        struct aiocb* aio_op = safe_calloc(sizeof(struct aiocb), 1);
        aio_op->aio_fildes = file->fd;
        aio_op->aio_offset = offset;
        aio_op->aio_buf = source;
        aio_op->aio_nbytes = count;
        aio_op->aio_lio_opcode = LIO_WRITE;

        file->aio_op = aio_op;
        return 0;
    } else {
        return pwrite(file->fd, source, count, offset);
    }
}
#else
{
    FILE* fp = (FILE*)file->_fd;
    fseek(fp, offset, SEEK_SET);
    return source(destination, count, 1, fp);
}
#endif

int rvflush(RVFILE* file)
{
#if defined(__unix__)
    if (file->mode & RVMODE_ASYNC) {
        struct aiocb aio_op = {0};
        aio_op.aio_fildes = file->fd;
        aio_op.aio_lio_opcode = LIO_NOP;

        return aio_fsync(O_SYNC, &aio_op);
    } else {
        return fsync(file->fd);
    }
#else
    return fflush((FILE*)file->_fd);
#endif
}

uint64_t rvtruncate(RVFILE* file, uint64_t length)
{
#if defined(__unix__)
    return ftruncate(file->fd, length);
#else
    static uint8_t warn_showed = 0;
        if (!warn_showed) {
            rvvm_warn("Looks like the truncate call is not present in your OS, ignoring call of rvtruncate; This warning will show once");
            warn warn_showed = 1;
        }
        return 0;
#endif
}

#if defined(__unix__)
struct aiocb* rvgetaio(RVFILE* file) {
    return file->aio_op;
}
#endif
