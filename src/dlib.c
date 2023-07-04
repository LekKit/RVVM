/*
dlib.c - Dynamic library related stuff implementation
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

#include <stdio.h>
#include <string.h>

#include "dlib.h"
#include "utils.h"

#ifdef _WIN32
#define DLIB_WIN32_IMPL
#include <windows.h>
#include <libloaderapi.h>
#define DLIB_NAME_CONVENTION_PATTERN_0 "%s.dll"
#elif defined(__APPLE__)
#define DLIB_NAME_CONVENTION_PATTERN_1 "%s.dylib"
#define DLIB_NAME_CONVENTION_PATTERN_0 "lib%s.dylib"
#else
#define DLIB_NAME_CONVENTION_PATTERN_1 "%s.so"
#define DLIB_NAME_CONVENTION_PATTERN_0 "lib%s.so"
#endif

#ifndef DLIB_WIN32_IMPL
#define DLIB_POSIX_IMPL
#include <dlfcn.h>
#ifndef RTLD_NODELETE
#define RTLD_NODELETE 0
#endif
#endif

#if !defined(DLIB_DISABLED) && (defined(DLIB_WIN32_IMPL) || defined(DLIB_POSIX_IMPL))

dlib_ctx_t* dlib_open(const char* path, uint16_t flags)
{
    int oflags = 0;
    void* dl_handle = NULL;
    if (unlikely(rvvm_strlen(path) >= 127))
    {
        rvvm_error("dlib_open failed: requested dlib name is too long (%d >= 127)", (uint32_t)rvvm_strlen(path));
        return NULL;
    }

#ifdef DLIB_WIN32_IMPL
    // Convert library path to utf-16
    wchar_t wchar_name[128];
    memset(wchar_name, 0, 128*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, path, rvvm_strlen(path), wchar_name, rvvm_strlen(path)+1);

    // Try to get module from already loaded modules
    HMODULE win_handle = GetModuleHandleW((LPWSTR)wchar_name);

    if (!win_handle)
    {
        if (flags & DLIB_NOLOAD)
        {
            oflags |= LOAD_LIBRARY_AS_DATAFILE;
        }

        win_handle = LoadLibraryExW((LPWSTR)wchar_name, NULL, (DWORD)oflags);

        // Try to use OS' library filename convention pattern for lookup
        if (flags & DLIB_NAME_PROBE)
        {
            if (!rvvm_strfind(path, ".") && !rvvm_strfind(path, "/"))
            {
                if (!win_handle)
                {
                    char patterned_name[128];
                    if (likely(snprintf(patterned_name, 128, DLIB_NAME_CONVENTION_PATTERN_0, path) <= 128))
                    {
                        MultiByteToWideChar(CP_UTF8, 0, path, rvvm_strlen(patterned_name), wchar_name, rvvm_strlen(patterned_name)+1);
                        win_handle = LoadLibraryExW((LPWSTR)patterned_name, NULL, (DWORD)oflags);
                    }
                }
            }
        }

        if (!win_handle) {
            rvvm_error("dlib_open failed");
            return NULL;
        }

        dl_handle = (void*)win_handle;
    }

#elif defined(DLIB_POSIX_IMPL)
    
    oflags = RTLD_LAZY;
    if (flags & DLIB_NODELETE) {
        oflags |= RTLD_NODELETE;
    }
    if (flags & DLIB_NOLOAD) {
        oflags |= RTLD_NOLOAD;
    }
    
    dl_handle = dlopen(path, oflags);

    // Try to use OS' library filename convention pattern for lookup
    if (flags & DLIB_NAME_PROBE)
    {
        if (!rvvm_strfind(path, ".") && !rvvm_strfind(path, "/"))
        {
            char patterned_name[128];
            if (!dl_handle)
            {
                if (likely(snprintf(patterned_name, 128, DLIB_NAME_CONVENTION_PATTERN_0, path) <= 128))
                {
                    rvvm_info("dlib_open failed: %s, trying name \"%s\"", dlerror(), patterned_name);
                    dl_handle = dlopen(patterned_name, oflags);
                }
            }
            if (!dl_handle)
            {
                if (likely(snprintf(patterned_name, 128, DLIB_NAME_CONVENTION_PATTERN_1, path) <= 128))
                {
                    rvvm_info("dlib_open failed: %s, trying name \"%s\"", dlerror(), patterned_name);
                    dl_handle = dlopen(patterned_name, oflags);
                }
            }
        }
    }

    if (!dl_handle) {

        rvvm_error("dlib_open failed: %s", dlerror());
        return NULL;
    }

#else

    rvvm_error("dlib_open: dynamic library loading not supported on selected operating system");
    return NULL;

#endif

    dlib_ctx_t* handle = safe_new_obj(dlib_ctx_t);
    handle->library_handle = dl_handle;
    rvvm_strlcpy(handle->library_path, path, 128);
    handle->flags = flags;

    rvvm_info("dlib_open: loaded %s", path);

    return handle;
}

void dlib_close(dlib_ctx_t* handle)
{
    if (!handle)
    {
        rvvm_error("dlib_close: Invalid dynamic library handle");
        return;
    }

    rvvm_info("dlib_close: closing %s", handle->library_path);

#ifdef DLIB_WIN32_IMPL
    // If DLIB_NODELETE specified just drop module handle as HMODULE just a pointer and looks like it's not deallocated in FreeLibrary
    if (!(handle->flags & DLIB_NODELETE))
    {
        if (!FreeLibrary((HMODULE)handle->library_handle))
        {
            rvvm_error("dlib_close failed");
        }
    }
#elif defined(DLIB_POSIX_IMPL)
    // If DLIB_NODELETE is specified but OS don't support RTLD_NODELETE as valid dlopen flag do not close library for possible future use
    if ((handle->flags & DLIB_NODELETE) && !!RTLD_NODELETE)
    {
        if (dlclose(handle->library_handle))
        {
            rvvm_error("dlib_close failed: %s", dlerror());
        }
    }
#else
    rvvm_error("dlib_close: dynamic library loading not supported on selected operating system");
    return;
#endif
    free(handle);
}

void* dlib_resolve(dlib_ctx_t* handle, const char* symbol_name)
{
    if (!handle)
    {
        rvvm_error("dlib_resolve: Invalid dynamic library handle");
        return NULL;
    }
    void* fn_ptr = NULL;

    rvvm_info("dlib_resolve: %s: symbol %s", handle->library_path, symbol_name);

#ifdef DLIB_WIN32_IMPL
    fn_ptr = (void*)GetProcAddress((HMODULE)handle->library_handle, (LPSTR)symbol_name);
    if (!fn_ptr)
    {
        rvvm_error("dlib_resolve failed");
        return NULL;
    }
#elif defined(DLIB_POSIX_IMPL)
    fn_ptr = dlsym(handle->library_handle, symbol_name);
    if (!fn_ptr)
    {
        rvvm_error("dlib_resolve: %s", dlerror());
        return NULL;
    }
#else
    rvvm_error("dlib_resolve: dynamic library loading not supported on selected operating system");
    return NULL;
#endif
    return fn_ptr;
}

dlib_ctx_t* dlib_reopen(dlib_ctx_t* handle, uint16_t flags)
{
    if (!handle)
    {
        rvvm_error("dlib_reopen: Invalid dynamic library handle");
        return NULL;
    }
    char library_path[128];
    memset(library_path, 0, 128);
    rvvm_strlcpy(handle->library_path, library_path, 128);
    dlib_close(handle);
    return dlib_open(library_path, flags);
}

#else

dlib_ctx_t* dlib_open(const char* path, uint16_t flags)
{
    UNUSED(path);
    UNUSED(flags);
    rvvm_error("dlib_open: Dynamic library loading support disabled in that build");
    return NULL;
}

dlib_ctx_t* dlib_reopen(dlib_ctx_t* handle, uint16_t flags)
{
    UNUSED(handle);
    UNUSED(flags);
    rvvm_error("dlib_reopen: Dynamic library loading support disabled in that build");
    return NULL;
}

void dlib_close(dlib_ctx_t* handle)
{
    UNUSED(handle);
    rvvm_error("dlib_close: Dynamic library loading support disabled in that build");
}

void* dlib_resolve(dlib_ctx_t* handle, const char* symbol_name)
{
    UNUSED(handle);
    UNUSED(symbol_name);
    rvvm_error("dlib_resolve: Dynamic library loading support disabled in that build");
    return NULL;
}

#endif