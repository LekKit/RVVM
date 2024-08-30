/*
dlib.c - Dynamic library loader
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

#include "dlib.h"

#ifndef DLIB_DISABLED

#if defined(_WIN32) && !defined(UNDER_CE)
#include <windows.h>
#define DLIB_WIN32_IMPL
#define DLIB_FILE_EXT ".dll"

#elif defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)
#include <dlfcn.h>
#define DLIB_POSIX_IMPL
#ifdef __APPLE__
#define DLIB_FILE_EXT ".dylib"
#else
#define DLIB_FILE_EXT ".so"
#endif
#endif

#endif

#ifndef DLIB_FILE_EXT
#define DLIB_FILE_EXT ""
#endif

// RVVM internal headers come after system headers because of safe_free()
#include "utils.h"

struct dlib_ctx {
#if defined(DLIB_WIN32_IMPL)
    HMODULE handle;
#elif defined(DLIB_POSIX_IMPL)
    void* handle;
#endif
    uint32_t flags;
};

static dlib_ctx_t* dlib_open_internal(const char* lib_name, uint32_t flags)
{
#if defined(DLIB_WIN32_IMPL)
    size_t name_len = rvvm_strlen(lib_name) + 1;
    wchar_t* u16_name = safe_new_arr(wchar_t, name_len);

    // Try to get module from already loaded modules
    MultiByteToWideChar(CP_UTF8, 0, lib_name, -1, u16_name, name_len);
    HMODULE handle = GetModuleHandleW(u16_name);
    if (handle) {
        // Prevent unloading an existing module
        flags &= ~DLIB_MAY_UNLOAD;
    } else {
        handle = LoadLibraryExW(u16_name, NULL, 0);
    }
    free(u16_name);
    if (handle == NULL) return NULL;
    dlib_ctx_t* lib = safe_new_obj(dlib_ctx_t);
    lib->handle = handle;
    lib->flags = flags;
    return lib;
#elif defined(DLIB_POSIX_IMPL)
    void* handle = dlopen(lib_name, RTLD_LAZY | RTLD_GLOBAL);
    if (handle == NULL) return NULL;
    dlib_ctx_t* lib = safe_new_obj(dlib_ctx_t);
    lib->handle = handle;
    lib->flags = flags;
    return lib;
#else
    UNUSED(lib_name);
    UNUSED(flags);
    return NULL;
#endif
}

static dlib_ctx_t* dlib_open_named(const char* prefix, const char* lib_name, const char* suffix, uint32_t flags)
{
    char name[256] = {0};
    size_t off = rvvm_strlcpy(name, prefix, sizeof(name));
    off += rvvm_strlcpy(name + off, lib_name, sizeof(name) - off);
    rvvm_strlcpy(name + off, suffix, sizeof(name) - off);
    return dlib_open_internal(name, flags);
}

dlib_ctx_t* dlib_open(const char* lib_name, uint32_t flags)
{
    if ((flags & DLIB_NAME_PROBE) && !rvvm_strfind(lib_name, "/")) {
        dlib_ctx_t* lib = dlib_open_named("lib", lib_name, DLIB_FILE_EXT, flags);
        if (lib) return lib;
        lib = dlib_open_named("", lib_name, DLIB_FILE_EXT, flags);
        if (lib) return lib;
    }
    return dlib_open_internal(lib_name, flags);
}

void dlib_close(dlib_ctx_t* lib)
{
    // Silently ignore load error
    if (lib && (lib->flags & DLIB_MAY_UNLOAD)) {
        rvvm_info("Unloading a library");
#if defined(DLIB_WIN32_IMPL)
        FreeLibrary(lib->handle);
#elif defined(DLIB_POSIX_IMPL)
        dlclose(lib->handle);
#endif
    }
    free(lib);
}

void* dlib_resolve(dlib_ctx_t* lib, const char* symbol_name)
{
    // Silently propagate load error
    if (lib == NULL) return NULL;
    void* ret = NULL;
#if defined(DLIB_WIN32_IMPL)
    ret = (void*)GetProcAddress(lib->handle, symbol_name);
#elif defined(DLIB_POSIX_IMPL)
    ret = dlsym(lib->handle, symbol_name);
#else
    UNUSED(symbol_name);
#endif
    return ret;
}

void* dlib_get_symbol(const char* lib_name, const char* symbol_name)
{
    dlib_ctx_t* lib = dlib_open(lib_name, 0);
    void* symbol = dlib_resolve(lib, symbol_name);
    dlib_close(lib);
    return symbol;
}

bool dlib_load_weak(const char* lib_name)
{
    dlib_ctx_t* lib = dlib_open(lib_name, DLIB_NAME_PROBE);
    dlib_close(lib);
    return lib != NULL;
}
