/*
utils.h - Util functions
Copyright (C) 2021  LekKit <github.com/LekKit>
                    0xCatPKG <0xCatPKG@rvvm.dev>
                    0xCatPKG <github.com/0xCatPKG>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_UTILS_H
#define RVVM_UTILS_H

#include "atomics.h" // IWYU pragma: keep
#include "rvvmlib.h"

#include <stdlib.h>

/*
 * String & numeric helpers
 */

// Evaluate max/min value
#define EVAL_MAX(a, b)         ((a) > (b) ? (a) : (b))
#define EVAL_MIN(a, b)         ((a) < (b) ? (a) : (b))

// Compute length of a static array
#define STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

// Align size up (To power of two!)
static inline size_t align_size_up(size_t x, size_t align)
{
    return (x + (align - 1)) & ~(align - 1);
}

// Align size down (To power of two!)
static inline size_t align_size_down(size_t x, size_t align)
{
    return x & ~(align - 1);
}

// Portable strtol/ltostr replacement
size_t uint_to_str_base(char* str, size_t size, uint64_t val, uint8_t base);
size_t int_to_str_base(char* str, size_t size, int64_t val, uint8_t base);
size_t int_to_str_dec(char* str, size_t size, int64_t val);


PUBLIC uint64_t str_to_uint_base(const char* str, size_t* len, uint8_t base);
int64_t         str_to_int_base(const char* str, size_t* len, uint8_t base);
int64_t         str_to_int_dec(const char* str);

// Portable & safer string.h replacement
size_t rvvm_strlen(const char* string);
size_t rvvm_strnlen(const char* string, size_t size);

PUBLIC bool rvvm_strcmp(const char* s1, const char* s2);
const char* rvvm_strfind(const char* string, const char* pattern);

size_t rvvm_strlcpy(char* dst, const char* src, size_t size);

// Portable vsnprintf() replacement
size_t rvvm_vsnprintf(char* buffer, size_t size, const char* fmt, const void* argv);
size_t rvvm_snprintf(char* buffer, size_t size, const char* fmt, ...);

static inline size_t mem_suffix_shift(char suffix)
{
    switch (suffix) {
        case 'k':
        case 'K':
            return 10;
        case 'm':
        case 'M':
            return 20;
        case 'g':
        case 'G':
            return 30;
        case 'T':
            return 40;
        default:
            return 0;
    }
}

// Generate random bytes — xorshift64 seeded by monotonic time. Fast and
// unpredictable to non-adversarial callers. NOT suitable for cryptographic
// use: the 64-bit state is trivially recoverable from a single 64-bit output.
// Use {@link rvvm_csprng_bytes} when output may reach an attacker-visible
// crypto boundary (TLS keying, entropy seeding, etc.).
void rvvm_randombytes(void* buffer, size_t size);

// Generate cryptographically strong random bytes from the host OS CSPRNG.
// Linux: getrandom(2). macOS/BSD: getentropy. Windows: BCryptGenRandom.
// Falls back to /dev/urandom on older POSIX. Output is suitable for the
// Zkr seed CSR (ratified virtual entropy source, >=256-bit security) and
// any other crypto-adjacent use.
//
// Significantly slower than rvvm_randombytes — expect a syscall per call.
// Don't use on hot paths (per-packet TCP ISN generation etc.); the fast
// PRNG is the right choice there.
void rvvm_csprng_bytes(void* buffer, size_t size);

// Generate random serial number (0-9, A-Z)
void rvvm_randomserial(char* serial, size_t size);

/*
 * Safe memory allocation
 */

#if GNU_ATTRIBUTE(__returns_nonnull__) && GNU_ATTRIBUTE(__warn_unused_result__) /**/                                   \
    && GNU_ATTRIBUTE(__malloc__) && GNU_ATTRIBUTE(__alloc_size__)
#define SAFE_MALLOC  __attribute__((__returns_nonnull__, __warn_unused_result__, __malloc__, __alloc_size__(1)))
#define SAFE_CALLOC  __attribute__((__returns_nonnull__, __warn_unused_result__, __malloc__, __alloc_size__(1, 2)))
#define SAFE_REALLOC __attribute__((__returns_nonnull__, __warn_unused_result__, __alloc_size__(2)))
#else
#define SAFE_MALLOC  GNU_DUMMY_ATTRIBUTE
#define SAFE_CALLOC  GNU_DUMMY_ATTRIBUTE
#define SAFE_REALLOC GNU_DUMMY_ATTRIBUTE
#endif

// These never return NULL
PUBLIC SAFE_MALLOC void*  safe_malloc(size_t size);
PUBLIC SAFE_CALLOC void*  safe_calloc(size_t size, size_t n);
PUBLIC SAFE_REALLOC void* safe_realloc(void* ptr, size_t size);

// Safe object allocation with type checking & zeroing
#define safe_new_arr(type, size) ((type*)safe_calloc(size, sizeof(type)))
#define safe_new_obj(type)       safe_new_arr(type, 1)

// Free and poison the pointer
#define safe_free(ptr)                                                                                                 \
    do {                                                                                                               \
        free(ptr);                                                                                                     \
        (ptr) = NULL;                                                                                                  \
    } while (0)

/*
 * Unicode handling
 */

static inline size_t utf8_decode_code_point(const char* str, size_t size, uint32_t* code_point)
{
    const uint8_t* str_u8 = (const uint8_t*)str;

    if (size >= 1 && str_u8[0] < 0x80UL) {
        // ASCII character
        *code_point = str_u8[0];
        return 1;
    } else if (size >= 1 && str_u8[0] < 0xC2UL) {
        // Illegal UTF8 sequence (Character starts with continuation/reserved byte)
        *code_point = 0;
        return 0;
    } else if (size >= 2 && str_u8[0] < 0xE0UL) {
        // 2-byte code point
        *code_point = ((str_u8[0] & 0x1FUL) << 6) | (str_u8[1] & 0x3FUL);
        return 2;
    } else if (size >= 3 && str_u8[0] < 0xF0UL) {
        // 3-byte code point
        *code_point = ((str_u8[0] & 0x0FUL) << 12) | ((str_u8[1] & 0x1FUL) << 6) | (str_u8[2] & 0x3FUL);
        return 3;
    } else if (size >= 4 && str_u8[0] < 0xF6U) {
        // 4-byte code point
        *code_point  = ((str_u8[0] & 0x07UL) << 18) | ((str_u8[1] & 0x0FUL) << 12);
        *code_point |= ((str_u8[2] & 0x1FUL) << 6) | (str_u8[3] & 0x3FUL);
        return 4;
    }

    *code_point = 0;
    return 0;
}

static inline size_t utf8_encode_code_point(char* str, size_t size, uint32_t code_point)
{
    uint8_t* str_u8 = (uint8_t*)str;

    if (size >= 1 && code_point < 0x80UL) {
        // ASCII character
        str_u8[0] = code_point;
        return 1;
    } else if (size >= 2 && code_point < 0x800UL) {
        // 2-byte code point
        str_u8[0] = (0xC0UL | (code_point >> 6));
        str_u8[1] = (0x80UL | (code_point & 0x3FUL));
        return 2;
    } else if (size >= 3 && code_point < 0x10000UL) {
        // 3-byte code point
        str_u8[0] = (0xE0UL | (code_point >> 12));
        str_u8[1] = (0x80UL | ((code_point >> 6) & 0x3FUL));
        str_u8[2] = (0x80UL | (code_point & 0x3F));
        return 3;
    } else if (size >= 4 && code_point < 0x200000UL) {
        // 4-byte code point
        str_u8[0] = (0xF0UL | (code_point >> 18));
        str_u8[1] = (0x80UL | ((code_point >> 12) & 0x3FUL));
        str_u8[2] = (0x80UL | ((code_point >> 6) & 0x3FUL));
        str_u8[3] = (0x80UL | (code_point & 0x3FUL));
        return 4;
    }

    return 0;
}

static inline size_t utf16_decode_code_point(const uint16_t* str, size_t size, uint32_t* code_point)
{
    const uint16_t* str_u16 = str;

    if (size >= 1 && (str_u16[0] >= 0xE000UL || str_u16[0] < 0xD800UL)) {
        // Single code unit
        *code_point = str_u16[0];
        return 1;
    } else if (size >= 2 && str_u16[0] >= 0xD800UL && str_u16[0] < 0xE000UL) {
        // Surrogate pair encoding
        *code_point = (str_u16[0] & 0x3FFUL) << 10 | (str_u16[1] & 0x3FFUL);
        return 2;
    }

    *code_point = 0;
    return 0;
}

static inline size_t utf16_encode_code_point(uint16_t* str, size_t size, uint32_t code_point)
{
    uint16_t* str_u16 = str;

    if (size >= 1 && (code_point < 0xD800UL || (code_point >= 0xE000UL && code_point < 0x10000UL))) {
        // Single code unit
        str_u16[0] = code_point;
        return 1;
    } else if (size >= 2 && code_point <= 0x110000UL) {
        // Surrogate pair encoding
        code_point -= 0x10000UL;
        str_u16[0]  = (0xD800UL | (code_point >> 10));
        str_u16[1]  = (0xDC00UL | (code_point & 0x3FFUL));
        return 2;
    }

    return 0;
}

static inline uint16_t* utf8_to_utf16(const char* str_u8)
{
    size_t    size_u16 = 16;
    size_t    pos_u16  = 0;
    size_t    size_u8  = 0;
    size_t    pos_u8   = 0;
    uint16_t* str_u16  = safe_new_arr(uint16_t, size_u16 + 1);
    while (str_u8 && str_u8[size_u8]) {
        size_u8++;
    }
    while (pos_u8 < size_u8) {
        uint32_t code = 0;
        size_t   b_u8 = utf8_decode_code_point(str_u8 + pos_u8, size_u8 - pos_u8, &code);
        if (b_u8) {
            size_t b_u16 = utf16_encode_code_point(str_u16 + pos_u16, size_u16 - pos_u16, code);
            if (b_u16) {
                pos_u16 += b_u16;
                pos_u8  += b_u8;
            } else if (size_u16 - pos_u16 < 2) {
                size_u16 += size_u16 >> 2;
                str_u16   = (uint16_t*)safe_realloc(str_u16, (size_u16 + 1) * sizeof(uint16_t));
            } else {
                // Failed to encode UTF-16
                safe_free(str_u16);
                return NULL;
            }
        } else {
            // Invalid UTF-8 input
            safe_free(str_u16);
            return NULL;
        }
    }
    str_u16[pos_u16] = 0;
    return str_u16;
}

static inline char* utf16_to_utf8(const uint16_t* str_u16)
{
    size_t size_u8  = 16;
    size_t pos_u8   = 0;
    size_t size_u16 = 0;
    size_t pos_u16  = 0;
    char*  str_u8   = safe_new_arr(char, size_u8 + 1);
    while (str_u16 && str_u16[size_u16]) {
        size_u16++;
    }
    while (pos_u16 < size_u16) {
        uint32_t code  = 0;
        size_t   b_u16 = utf16_decode_code_point(str_u16 + pos_u16, size_u16 - pos_u16, &code);
        if (b_u16) {
            size_t b_u8 = utf8_encode_code_point(str_u8 + pos_u8, size_u8 - pos_u8, code);
            if (b_u8) {
                pos_u16 += b_u16;
                pos_u8  += b_u8;
            } else if (size_u8 - pos_u8 < 4) {
                size_u8 += size_u8 >> 2;
                str_u8   = (char*)safe_realloc(str_u8, size_u8 + 1);
            } else {
                // Failed to encode UTF-8
                safe_free(str_u8);
                return NULL;
            }
        } else {
            // Invalid UTF-16 input
            safe_free(str_u8);
            return NULL;
        }
    }
    str_u8[pos_u8] = 0;
    return str_u8;
}

/*
 * Command line & config parsing
 */

// Set command line arguments
PUBLIC void rvvm_set_args(int argc, char** argv);

// Load config file
PUBLIC bool rvvm_load_config(const char* path);

// Iterate over arguments in form of <-arg> [val], or <val> ("" is returned in such case)
PUBLIC const char* rvvm_next_arg(const char** val, int* iter);

// Check if argument is present on the command line or in the config
PUBLIC bool rvvm_has_arg(const char* arg);

// Get argument value
PUBLIC const char* rvvm_getarg(const char* arg);
PUBLIC bool        rvvm_getarg_bool(const char* arg);
PUBLIC int         rvvm_getarg_int(const char* arg);
PUBLIC uint64_t    rvvm_getarg_size(const char* arg);

/*
 * Logger
 */

#define LOG_NONE  0
#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3

PUBLIC void rvvm_set_loglevel(int loglevel);

#if GNU_ATTRIBUTE(__format__)
#define PRINT_FORMAT __attribute__((__format__(printf, 1, 2)))
#else
#define PRINT_FORMAT GNU_DUMMY_ATTRIBUTE
#endif

#if defined(USE_DEBUG)

// Debug logger enabled at build time
PRINT_FORMAT void rvvm_debug(const char* format_str, ...);

#else

// Debug logs optimized out, but still performs compile-time format / unused arguments checking
static PRINT_FORMAT forceinline void rvvm_debug(const char* format_str, ...)
{
    UNUSED(format_str);
}

#endif

// Logging functions (controlled by loglevel)
PUBLIC PRINT_FORMAT void rvvm_info(const char* format_str, ...);
PUBLIC PRINT_FORMAT void rvvm_warn(const char* format_str, ...);
PUBLIC PRINT_FORMAT void rvvm_error(const char* format_str, ...);
PUBLIC PRINT_FORMAT void rvvm_fatal(const char* format_str, ...); // Aborts the process

/*
 * Initialization/deinitialization
 */

slow_path void do_once_finalize(uint32_t* ticket);

/*
 * Run scoped statement once per library lifetime (In a thread-safe way)
 *
 * DO_ONCE_SCOPED {
 *     if (!feature_avail()) {
 *         break;
 *     }
 *
 *     global_feature_init();
 * }
 */
#define DO_ONCE_SCOPED                                                                                                 \
    static uint32_t MACRO_IDENT(do_once_ticket) = 0;                                  /**/                             \
    POST_STMT_NAMED (unlikely(atomic_load_uint32(&MACRO_IDENT(do_once_ticket)) != 2), /**/                             \
                     do_once_finalize(&MACRO_IDENT(do_once_ticket)), ticket_stmt)     /**/                             \
        POST_STMT_NAMED (atomic_cas_uint32(&MACRO_IDENT(do_once_ticket), 0, 1),       /**/                             \
                         atomic_store_uint32(&MACRO_IDENT(do_once_ticket), 2), claim_stmt)

/*
 * Run an expression once per library lifetime (In a thread-safe way)
 *
 * DO_ONCE(rvvm_warn("This will only be printed once upon reaching!"));
 */
#define DO_ONCE(expr_once)                                                                                             \
    do {                                                                                                               \
        DO_ONCE_SCOPED {                                                                                               \
            expr_once;                                                                                                 \
        }                                                                                                              \
    } while (0)

// Register a callback to be ran at deinitialization (exit or library unload)
void call_at_deinit(void (*function)(void));

// Perform manual deinitialization
GNU_DESTRUCTOR void full_deinit(void);

#endif
