/*
file.h - Cross-platform file API implementation for RVVM
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

#ifndef FILE_H
#define FILE_H

#define RVMODE_READONLY  0x0
#define RVMODE_WRITEONLY 0x1
#define RVMODE_READWRITE 0x2
#define RVMODE_ASYNC     0x4

#if defined (__unix__)
#include <sys/stat.h>
#include <fcntl.h>
#include <aio.h>
#include <unistd.h>
#else
#include <stdio.h>
#endif

#include "rvvm_types.h"

typedef struct
{
    char filepath[512];
    uint8_t mode;
    uint32_t fd;
    void* _fd;

#if defined (__unix__)
    struct aiocb* aio_op;
#endif

} RVFILE ;

RVFILE* rvopen(const char* filepath, uint8_t mode);
int     rvfileno(RVFILE* file);
void    rvclose(RVFILE* file);

uint64_t rvread(void* destination, uint64_t count, uint64_t offset, RVFILE* file);
uint64_t rvwrite(void* source, uint64_t count, uint64_t offset, RVFILE* file);
int      rvflush(RVFILE* file);
uint64_t rvtruncate(RVFILE* file, uint64_t length);

#if defined (__unix__)
struct aiocb*  rvgetaio(RVFILE* file);
#endif

#endif
