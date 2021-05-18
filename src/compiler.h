/*
compiler.h - Compilers tricks and features
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#ifndef COMPILER_H
#define COMPILER_H

#if defined(__GNUC__) || defined(__llvm__) || defined(__INTEL_COMPILER)
#define GNU_EXTS
#endif

#ifdef GNU_EXTS
#define likely(x)     __builtin_expect(!!(x),1)
#define unlikely(x)   __builtin_expect(!!(x),0)
#else
#define likely(x)     (x)
#define unlikely(x)   (x)
#endif

#ifdef GNU_EXTS
#define PUBLIC        __attribute__((visibility("default")))
#define HIDDEN        __attribute__((visibility("hidden")))
#define NOINLINE      __attribute__((noinline))
#elif defined(_WIN32)
#define PUBLIC        __declspec(dllexport)
#define HIDDEN
#define NOINLINE      __declspec(noinline)
#else
#define PUBLIC
#define HIDDEN
#define NOINLINE
#endif

#endif
