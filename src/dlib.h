/*
dlib.h - Dynamic library related stuff
Copyright (C) 2023 0xCatPKG <github.com/0xCatPKG>

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

#ifndef DLIB_H
#define DLIB_H

#include "compiler.h"

#define DLIB_NAME_PROBE 1
#define DLIB_MAY_UNLOAD 2

typedef struct dlib_ctx dlib_ctx_t;

// dlib_ctx_t managment procedures
dlib_ctx_t* dlib_open(const char* path, uint16_t flags);
void dlib_close(dlib_ctx_t* handle);

// Symbol managment procedures
void* dlib_resolve(dlib_ctx_t* handle, const char* symbol_name);

#endif
