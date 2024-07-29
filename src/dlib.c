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
#include "utils.h"

#ifndef DLIB_DISABLED

#ifdef _WIN32
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

struct dlib_ctx {
#if defined(DLIB_WIN32_IMPL)
    HMODULE handle;
#elif defined(DLIB_POSIX_IMPL)
    void* handle;
#endif
    uint32_t flags;
};

static dlib_ctx_t* dlib_open_internal(const char* path, uint32_t flags)
{
#if defined(DLIB_WIN32_IMPL)
    size_t path_len = rvvm_strlen(path) + 1;
    wchar_t* u16_path = safe_new_arr(wchar_t, path_len);

    // Try to get module from already loaded modules
    MultiByteToWideChar(CP_UTF8, 0, path, -1, u16_path, path_len);
    HMODULE handle = GetModuleHandleW(u16_path);
    if (handle) {
        // Prevent unloading an existing module
        flags &= ~DLIB_MAY_UNLOAD;
    } else {
        handle = LoadLibraryExW(u16_path, NULL, 0);
    }
    free(u16_path);
    if (handle == NULL) return NULL;
    dlib_ctx_t* lib = safe_new_obj(dlib_ctx_t);
    lib->handle = handle;
    lib->flags = flags;
    return lib;
#elif defined(DLIB_POSIX_IMPL)
    void* handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
    if (handle == NULL) return NULL;
    dlib_ctx_t* lib = safe_new_obj(dlib_ctx_t);
    lib->handle = handle;
    lib->flags = flags;
    return lib;
#else
    DO_ONCE(rvvm_warn("Dynamic library loading is not supported"));
    UNUSED(path);
    UNUSED(flags);
    return NULL;
#endif
}


dlib_ctx_t* dlib_open(const char* path, uint32_t flags)
{
    dlib_ctx_t* lib = NULL;
    if ((flags & DLIB_NAME_PROBE) && !rvvm_strfind(path, "/")) {
        size_t name_len = rvvm_strlen("lib") + rvvm_strlen(path) + rvvm_strlen(DLIB_FILE_EXT) + 1;
        char* name = safe_new_arr(char, name_len);
        size_t off = rvvm_strlcpy(name, "lib", name_len);
        off += rvvm_strlcpy(name + off, path, name_len - off);
        rvvm_strlcpy(name + off, DLIB_FILE_EXT, name_len - off);

        lib = dlib_open_internal(name, flags);
        if (lib == NULL) lib = dlib_open_internal(name + rvvm_strlen("lib"), flags);
    }
    if (lib == NULL) lib = dlib_open_internal(path, flags);
    return lib;
}

void dlib_close(dlib_ctx_t* lib)
{
    // Silently ignore load error
    if (lib == NULL) return;
    if (lib->flags & DLIB_MAY_UNLOAD) {
        rvvm_info("Unloading a library");
#ifdef DLIB_WIN32_IMPL
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
#ifdef DLIB_WIN32_IMPL
    ret = (void*)GetProcAddress(lib->handle, symbol_name);
#elif defined(DLIB_POSIX_IMPL)
    ret = dlsym(lib->handle, symbol_name);
#endif
    if (ret == NULL) rvvm_warn("Failed to resolve symbol %s!", symbol_name);
    return ret;
}

bool dlib_load_weak(const char* path)
{
    dlib_ctx_t* lib = dlib_open(path, DLIB_NAME_PROBE);
    dlib_close(lib);
    return lib != NULL;
}
