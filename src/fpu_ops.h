/*
fpu_ops.h - FPU Rounding/Exceptions manipulation
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

#ifndef RVVM_FPU_OPS_H
#define RVVM_FPU_OPS_H

#include "compiler.h"

// Fix rounding modes even when -frounding-math is not present
#if CLANG_CHECK_VER(12, 0)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access (on)
#endif

#include <fenv.h>

#ifndef FE_ALL_EXCEPT
// Some targets (Emscripten, Windows CE) explicitly lack
// the ability to manipulate host FPU modes.
// These shims are provided to build & run on these targets.
#define HOST_NO_FENV
#endif

#ifndef FE_DIVBYZERO
#define FE_DIVBYZERO 0
#endif
#ifndef FE_INEXACT
#define FE_INEXACT 0
#endif
#ifndef FE_INVALID
#define FE_INVALID 0
#endif
#ifndef FE_OVERFLOW
#define FE_OVERFLOW 0
#endif
#ifndef FE_UNDERFLOW
#define FE_UNDERFLOW 0
#endif
#ifndef FE_ALL_EXCEPT
#define FE_ALL_EXCEPT 0
#endif
#ifndef FE_DOWNWARD
#define FE_DOWNWARD 0
#endif
#ifndef FE_TONEAREST
#define FE_TONEAREST 0
#endif
#ifndef FE_TOWARDZERO
#define FE_TOWARDZERO 0
#endif
#ifndef FE_UPWARD
#define FE_UPWARD 0
#endif
#ifndef FE_DFL_ENV
#define FE_DFL_ENV 0
#endif

static inline int fenv_bogus_op()
{
    return 0;
}

#ifdef HOST_NO_FENV
#define feclearexcept(x) fenv_bogus_op()
#define fegetexceptflag(f, x) fenv_bogus_op()
#define feraiseexcept(x) fenv_bogus_op()
#define fesetexceptflag(f, x) fenv_bogus_op()
#define fetestexcept(x) fenv_bogus_op()

#define fegetround() fenv_bogus_op()
#define fesetround(r) fenv_bogus_op()

#define fegetenv(x) fenv_bogus_op()
#define feholdexcept(f, x) fenv_bogus_op()
#define fesetenv(x) fenv_bogus_op()
#define feupdateenv(x) fenv_bogus_op()
#endif

#endif
