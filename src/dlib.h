/*
dlib.h - Dynamic library loader
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

#ifndef RVVM_DLIB_H
#define RVVM_DLIB_H

#include <stdint.h>
#include <stdbool.h>

#define DLIB_NAME_PROBE 0x1 // Probe various A.so, libA.so variations
#define DLIB_MAY_UNLOAD 0x2 // Allow dlib_close() to actually unload the library

typedef struct dlib_ctx dlib_ctx_t;

// Load the library
dlib_ctx_t* dlib_open(const char* path, uint32_t flags);

// Drop the library handle. unload the library if DLIB_MAY_UNLOAD was set
void dlib_close(dlib_ctx_t* lib);

// Resolve public library symbols
void* dlib_resolve(dlib_ctx_t* lib, const char* symbol_name);

// Resolve weak symbols provided by a lib (With name probing)
bool dlib_load_weak(const char* path);

#endif
