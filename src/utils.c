/*
utils.с - Util functions
Copyright (C) 2021  LekKit <github.com/LekKit>
                    0xCatPKG <0xCatPKG@rvvm.dev>
                    0xCatPKG <github.com/0xCatPKG>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// Must be included before <stdio.h>
#include "feature_test.h"

#include "utils.h"

#include "blk_io.h"
#include "rvtimer.h"
#include "spinlock.h"
#include "stacktrace.h"
#include "threading.h"
#include "vector.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PUSH_OPTIMIZATION_SIZE

/*
 * String/Integer conversions
 */

static inline char digit_symbol(uint32_t val)
{
    if (val < 10) {
        return '0' + val;
    }
    if (val < 36) {
        return 'a' + val - 10;
    }
    return '?';
}

static inline uint32_t digit_value(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'A' && digit <= 'Z') {
        return digit - 'A' + 10;
    }
    if (digit >= 'a' && digit <= 'z') {
        return digit - 'a' + 10;
    }
    return -1;
}

size_t uint_to_str_base(char* str, size_t size, uint64_t val, uint8_t base)
{
    size_t len = 0;
    if (base >= 2 && base <= 36) {
        do {
            if (str) {
                if (len + 1 >= size) {
                    len = 0;
                    break;
                }
                str[len] = digit_symbol(val % base);
            }
            val /= base;
            len++;
        } while (val);
    }
    if (str) {
        // Reverse the string
        for (size_t i = 0; i < len / 2; ++i) {
            char tmp         = str[i];
            str[i]           = str[len - i - 1];
            str[len - i - 1] = tmp;
        }
        if (size) {
            str[len] = 0;
        }
    }
    return len;
}

uint64_t str_to_uint_base(const char* str, size_t* len, uint8_t base)
{
    uint64_t val  = 0;
    size_t   size = 0;
    if (base == 0) {
        base = 10;
        if (str[0] == '0') {
            base = 8; // Octal literal
            if (str[1] == 'o' || str[1] == 'O') {
                size = 2;
            } else if (str[1] == 'x' || str[1] == 'X') {
                base = 16; // Hex literal
                size = 2;
            } else if (str[1] == 'b' || str[1] == 'B') {
                base = 2; // Binary literal
                size = 2;
            }
        }
    }
    if (len) {
        len[0] = 0;
    }
    if (base >= 2 && base <= 36) {
        while (digit_value(str[size]) < base) {
            val *= base;
            val += digit_value(str[size++]);
            if (len) {
                len[0] = size;
            }
        }
    }
    return val;
}

size_t int_to_str_base(char* str, size_t size, int64_t val, uint8_t base)
{
    size_t sign  = val < 0;
    size_t usize = size ? (size - sign) : 0;
    char*  ustr  = str ? (str + sign) : NULL;
    size_t len   = uint_to_str_base(ustr, usize, sign ? -val : val, base);
    if (sign && str) {
        str[0] = 0;
        if (usize) {
            str[0] = '-';
        }
    }
    return len + sign;
}

int64_t str_to_int_base(const char* str, size_t* len, uint8_t base)
{
    bool     neg = (str[0] == '-');
    uint64_t val = str_to_uint_base(str + neg, len, base);
    if (neg && len && len[0]) {
        len[0]++;
    }
    return neg ? -val : val;
}

size_t int_to_str_dec(char* str, size_t size, int64_t val)
{
    return int_to_str_base(str, size, val, 10);
}

int64_t str_to_int_dec(const char* str)
{
    return str_to_int_base(str, NULL, 0);
}

/*
 * String functions
 */

size_t rvvm_strlen(const char* string)
{
    size_t i = 0;
    while (string[i]) {
        i++;
    }
    return i;
}

size_t rvvm_strnlen(const char* string, size_t size)
{
    size_t i = 0;
    while (i < size && string[i]) {
        i++;
    }
    return i;
}

bool rvvm_strcmp(const char* s1, const char* s2)
{
    size_t i = 0;
    while (s1[i] && s1[i] == s2[i]) {
        i++;
    }
    return s1[i] == s2[i];
}

const char* rvvm_strfind(const char* string, const char* pattern)
{
    while (*string) {
        const char* tmp = string;
        const char* pat = pattern;
        while (*tmp && *tmp == *pat) {
            tmp++;
            pat++;
        }
        if (!(*pat)) {
            return string;
        }
        string++;
    }
    return NULL;
}

size_t rvvm_strlcpy(char* dst, const char* src, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    if (size) {
        dst[i] = 0;
    }
    return i;
}

/*
 * String formating
 */

#if !defined(USE_LIBC_PRINTF)

static size_t rvvm_strpad_internal(char* str, size_t size, char pad, bool left, size_t new_len, size_t old_len)
{
    if (new_len > old_len && size) {
        size_t diff = EVAL_MIN(size - 1, new_len - old_len);
        if (left && diff) {
            memset(str + old_len, pad, diff);
        } else if (diff) {
            memmove(str + diff, str, old_len);
            memset(str, pad, diff);
        }
        str[EVAL_MIN(size - 1, new_len)] = 0;
        return diff;
    }
    return 0;
}

#endif

size_t rvvm_vsnprintf(char* buffer, size_t size, const char* fmt, const void* argv)
{
    va_list args;
    memcpy(&args, argv, sizeof(args));
#if defined(USE_LIBC_PRINTF)
    return vsnprintf(buffer, size, fmt, args);
#else
    size_t ret = 0, len = 0;
    do {
        // Raw format string part length
        size_t str_len = 0;
        // Search for % format symbol
        const char* delim = rvvm_strfind(fmt, "%");
        if (delim) {
            str_len = ((size_t)delim) - ((size_t)fmt);
            delim++;
        } else {
            str_len = rvvm_strlen(fmt);
        }
        // Append raw format string part
        len += rvvm_strlcpy(buffer + len, fmt, EVAL_MIN(size - len, str_len + 1));
        ret += str_len;
        // Append formatted input
        if (delim) {
            size_t fmt_len = 0;
            // Flags
            bool pad_left = false, num_pfx = false, pad_zero = false;
            char num_plus = 0;
            // Width / Precision
            size_t width = 0, precs = 0, skip = 0;
            // Data type length
            const char* dlen = "";
            // Flags (May be omitted)
            if (delim[0] == '-') {
                // Left-justify the width-padded field
                pad_left = true;
                delim++;
            }
            if (delim[0] == '+' || delim[0] == ' ') {
                // Prefix the result with a plus/minus/space
                num_plus = delim[0];
                delim++;
            } else if (delim[0] == '#') {
                // Prefix the result with 0, 0x or 0X respectively
                num_pfx = true;
                delim++;
            }
            if (delim[0] == '0') {
                // Left-pad number with zeroes instead of spaces
                pad_zero = true;
                delim++;
            }
            // Width sub-specifier (May be omitted)
            if (delim[0] == '*') {
                // Width sub-specifier passed in args
                width = va_arg(args, unsigned int);
                delim++;
            } else {
                // Width sub-specifier encoded in format string or omitted
                width  = str_to_uint_base(delim, &skip, 10);
                delim += skip;
            }
            // Prevision sub-specifier (May be omitted)
            if (delim[0] == '.') {
                delim++;
                if (delim[0] == '*') {
                    // Precision sub-specifier passed in args
                    precs = va_arg(args, unsigned int);
                    delim++;
                } else {
                    // Precision sub-specifier encoded in format string
                    precs  = str_to_uint_base(delim, &skip, 10);
                    delim += skip;
                }
            }
            // Data type length sub-specifier
            if (delim[0] == 'l' || delim[0] == 'h' || delim[0] == 'j' || delim[0] == 't' || delim[0] == 'z') {
                dlen = delim;
                if (delim[1] == 'l' || delim[1] == 'h') {
                    delim++;
                }
                delim++;
            }
            switch (delim[0]) {
                case 's': {
                    // Append a string
                    const char* str = va_arg(args, const char*);
                    if (!str) {
                        str = "(null)";
                    }
                    fmt_len  = rvvm_strlen(str);
                    len     += rvvm_strlcpy(buffer + len, str, EVAL_MIN(size - len, fmt_len + 1));
                    break;
                }
                case 'd':
                case 'i':
                case 'u':
                case 'o':
                case 'x':
                case 'X': {
                    size_t  ser_len   = 0;
                    bool    signval   = delim[0] == 'd' || delim[0] == 'i';
                    char    prefix[3] = ZERO_INIT;
                    uint8_t base      = 10;
                    int64_t val       = 0;
                    // Extract data type from va_arg
                    if ((dlen[0] == 'l' && dlen[1] == 'l') || dlen[0] == 'j') {
                        val = signval ? (int64_t)va_arg(args, long long) : (int64_t)va_arg(args, unsigned long long);
                    } else if (dlen[0] == 'l') {
                        val = signval ? (int64_t)va_arg(args, long) : (int64_t)va_arg(args, unsigned long);
                    } else if (dlen[0] == 'h' && dlen[1] == 'h') {
                        val = signval ? (int64_t)(int8_t)va_arg(args, unsigned)
                                      : (int64_t)(uint8_t)va_arg(args, unsigned);
                    } else if (dlen[0] == 'h') {
                        val = signval ? (int64_t)(int16_t)va_arg(args, unsigned)
                                      : (int64_t)(uint16_t)va_arg(args, unsigned);
                    } else if (dlen[0] == 'z' || dlen[0] == 't') {
                        val = va_arg(args, size_t);
                    } else {
                        val = signval ? (int64_t)va_arg(args, int32_t) : (int64_t)va_arg(args, uint32_t);
                    }
                    // Detect number base and prefix if needed
                    if (delim[0] == 'x' || delim[0] == 'X') {
                        base = 16;
                        if (num_pfx) {
                            rvvm_strlcpy(prefix, "0x", sizeof(prefix));
                        }
                    } else if (delim[0] == 'o') {
                        base = 8;
                        if (num_pfx) {
                            rvvm_strlcpy(prefix, "0", sizeof(prefix));
                        }
                    } else if (signval && val < 0) {
                        prefix[0] = '-';
                        val       = -val;
                    } else if (signval && num_plus) {
                        prefix[0] = num_plus;
                    }
                    // Append prefix
                    len += rvvm_strlcpy(buffer + len, prefix, size - len);
                    // Calculate unpadded number length, append number
                    fmt_len = uint_to_str_base(NULL, 0, (uint64_t)val, base);
                    ser_len = uint_to_str_base(buffer + len, size - len, (uint64_t)val, base);
                    if (delim[0] == 'X') {
                        // Uppercase the hex string
                        for (char* s = buffer + len - !!len; *s; ++s) {
                            if (s[0] >= 'a' && s[1] <= 'f') {
                                s[0] += 'A' - 'a';
                            }
                        }
                    }
                    if (precs > fmt_len) {
                        // Pad the number with zeroes ignoring the prefix length
                        ser_len += rvvm_strpad_internal(buffer + len, size - len, '0', false, precs, fmt_len);
                        fmt_len  = precs;
                    }
                    fmt_len += rvvm_strlen(prefix);
                    if (width > fmt_len && pad_zero) {
                        // Pad the number with zeroes
                        ser_len += rvvm_strpad_internal(buffer + len, size - len, '0', false, width, fmt_len);
                        fmt_len  = width;
                    }
                    len += ser_len;
                    break;
                }
                case 'p': {
                    // Append pointer
                    const void* ptr = va_arg(args, const void*);
                    // Append 0x
                    len += rvvm_strlcpy(buffer + len, "0x", EVAL_MIN(size - len, 3));
                    // Append hex pointer
                    len += uint_to_str_base(buffer + len, size - len, (size_t)ptr, 16);
                    // Calculate result length
                    fmt_len = 2 + uint_to_str_base(NULL, 0, (size_t)ptr, 16);
                    break;
                }
                case 'c':
                case '%': {
                    // Append character (Or %)
                    char character[2] = "%";
                    if (delim[0] == 'c') {
                        character[0] = va_arg(args, unsigned);
                    }
                    fmt_len  = 1;
                    len     += rvvm_strlcpy(buffer + len, character, EVAL_MIN(size - len, 2));
                    break;
                }
                case 'n': {
                    // Nothing printed; Store current output length to int*
                    int* ptr = va_arg(args, int*);
                    if (ptr) {
                        *ptr = ret;
                    }
                    break;
                }
            }
            if (width > fmt_len) {
                // Pad the field width
                len     += rvvm_strpad_internal(buffer + len - fmt_len, size - len + fmt_len, //
                                                ' ', pad_left, width, fmt_len);
                fmt_len  = width;
            }
            ret += fmt_len;
            delim++;
            str_len = ((size_t)delim) - ((size_t)fmt);
        }
        fmt += str_len;
    } while (*fmt);
    return ret;
#endif
}

size_t rvvm_snprintf(char* buffer, size_t size, const char* fmt, ...)
{
    size_t  ret = 0;
    va_list args;
    va_start(args, fmt);
    ret = rvvm_vsnprintf(buffer, size, fmt, &args);
    va_end(args);
    return ret;
}

/*
 * Random generation
 */

static uint64_t rvvm_rng_seed = 0;

void rvvm_randombytes(void* buffer, size_t size)
{
    // Xorshift RNG seeded by precise timer
    uint64_t seed  = atomic_load_uint64_relax(&rvvm_rng_seed);
    uint8_t* dest  = buffer;
    size_t   rem   = size & 0x7;
    size          -= rem;
    if (!seed) {
        seed = rvtimer_clocksource(1000000000ULL);
    }
    for (size_t i = 0; i < size; i += 8) {
        seed ^= (seed >> 17);
        seed ^= (seed << 21);
        seed ^= (seed << 28);
        seed ^= (seed >> 49);
        memcpy(dest + i, &seed, 8);
    }
    seed ^= (seed >> 17);
    seed ^= (seed << 21);
    seed ^= (seed << 28);
    seed ^= (seed >> 49);
    memcpy(dest + size, &seed, rem);
    atomic_store_uint64_relax(&rvvm_rng_seed, seed);
}

/*
 * Cryptographically strong RNG — thin wrapper over the host OS CSPRNG.
 *
 * Per-platform notes:
 *   Linux / FreeBSD / Solaris / Illumos: getrandom(2) is the right syscall.
 *     GRND_NONBLOCK left off so callers block on initial entropy collection
 *     rather than failing silently. Post-boot urandom-seed this is a no-op.
 *   macOS / OpenBSD: getentropy(3). 256-byte limit per call (POSIX 2024
 *     doesn't actually mandate this but libc enforces it), so loop in chunks.
 *   Windows: BCryptGenRandom with the system-preferred provider. Requires
 *     NT 6+ — we advertise HOST_TARGET_WINNT >= 5 but bcrypt showed up in
 *     Vista so ancient-XP builds fall through to the urandom fallback.
 *   POSIX fallback: /dev/urandom. Works on every *nix ever shipped since
 *     the 90s, covers HaikuOS / NetBSD-without-getrandom / ancient kernels.
 *
 * If any of the primary paths fail partway through, we log and leave the
 * remaining bytes as whatever the caller passed in (most commonly zero).
 * Callers should treat short reads as failure, but since every modern host
 * ships one of the primary paths, in practice we never hit the fallback.
 */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__sun__)
#include <sys/random.h>
#define RVVM_HAVE_GETRANDOM 1
#elif defined(__APPLE__) || defined(__OpenBSD__)
#include <unistd.h>
#define RVVM_HAVE_GETENTROPY 1
#elif defined(_WIN32)
#include <windows.h>
// bcrypt.h needs NT 6+ SDK. Declare the symbol locally to avoid a hard
// link dep on builds that include this file transitively — the linker
// will pick it up from bcrypt.lib when present; when absent, pass
// -luserenv and we fall through to the urandom path (won't be taken on
// Windows but keeps the link step happy).
__stdcall long BCryptGenRandom(void* alg, unsigned char* buf, unsigned long len, unsigned long flags);
#define RVVM_BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#define RVVM_HAVE_BCRYPTGENRANDOM 1
#endif

#if !defined(RVVM_HAVE_GETRANDOM) && !defined(RVVM_HAVE_GETENTROPY) && !defined(RVVM_HAVE_BCRYPTGENRANDOM)
#include <fcntl.h>
#include <unistd.h>
#define RVVM_NEED_URANDOM_FALLBACK 1
#endif

void rvvm_csprng_bytes(void* buffer, size_t size)
{
    uint8_t* dest = buffer;
#if defined(RVVM_HAVE_GETRANDOM)
    while (size) {
        ssize_t n = getrandom(dest, size, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            rvvm_warn("getrandom() failed: %s (%zu bytes requested)", strerror(errno), size);
            break;
        }
        dest += (size_t)n;
        size -= (size_t)n;
    }
#elif defined(RVVM_HAVE_GETENTROPY)
    while (size) {
        size_t chunk = size > 256 ? 256 : size;
        if (getentropy(dest, chunk) < 0) {
            rvvm_warn("getentropy() failed: %s (%zu bytes requested)", strerror(errno), chunk);
            break;
        }
        dest += chunk;
        size -= chunk;
    }
#elif defined(RVVM_HAVE_BCRYPTGENRANDOM)
    // BCryptGenRandom accepts up to 2^32-1 bytes per call. In practice we
    // never hand it more than a few KiB from crypto callers, but the
    // signature takes unsigned long so we loop for >4 GiB hosts.
    while (size) {
        unsigned long chunk = size > 0xFFFFFFFFUL ? 0xFFFFFFFFUL : (unsigned long)size;
        long status = BCryptGenRandom(NULL, dest, chunk, RVVM_BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status != 0) {
            rvvm_warn("BCryptGenRandom failed: 0x%08lx (%lu bytes requested)",
                      (unsigned long)status, chunk);
            break;
        }
        dest += chunk;
        size -= chunk;
    }
#elif defined(RVVM_NEED_URANDOM_FALLBACK)
    // Open-read-close each call. Slower than keeping a cached fd but also
    // safe under fork + sandbox-drop scenarios where a cached fd might be
    // invalidated mid-flight. Crypto callers aren't on a hot path anyway.
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        rvvm_warn("open(/dev/urandom) failed: %s", strerror(errno));
        return;
    }
    while (size) {
        ssize_t n = read(fd, dest, size);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            rvvm_warn("read(/dev/urandom) short/failed (%zd bytes, %zu remaining)", n, size);
            break;
        }
        dest += (size_t)n;
        size -= (size_t)n;
    }
    close(fd);
#else
    // Platform with no known CSPRNG wired up. Leave buffer untouched;
    // callers depending on entropy will fail downstream rather than get
    // silently-predictable output from a fallback to xorshift.
    (void)dest;
    rvvm_warn("rvvm_csprng_bytes: no platform CSPRNG compiled in (%zu bytes requested)", size);
#endif
}

void rvvm_randomserial(char* serial, size_t size)
{
    rvvm_randombytes(serial, size);
    for (size_t i = 0; i < size; ++i) {
        size_t c = ((uint8_t*)serial)[i] % ('Z' - 'A' + 10);
        if (c <= 9) {
            serial[i] = '0' + c;
        } else {
            serial[i] = 'A' + c - 10;
        }
    }
}

/*
 * Safe memory allocation
 */

SAFE_MALLOC void* safe_malloc(size_t size)
{
    void* ret = malloc(size);
    if (unlikely(!size)) {
        rvvm_debug("Suspicious 0-byte allocation");
    }
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
        must_never_reach();
    }
    return ret;
}

SAFE_CALLOC void* safe_calloc(size_t size, size_t n)
{
    void* ret = calloc(size, n);
    if (unlikely(!size || !n)) {
        rvvm_debug("Suspicious 0-byte allocation");
    }
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
        must_never_reach();
    }
    // Fence zeroing of allocated memory
    atomic_fence_ex(ATOMIC_RELEASE);
    return ret;
}

SAFE_REALLOC void* safe_realloc(void* ptr, size_t size)
{
    void* ret = realloc(ptr, size);
    if (unlikely(!size)) {
        rvvm_debug("Suspicious 0-byte allocation");
    }
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
        must_never_reach();
    }
    return ret;
}

/*
 * Command line & config parsing
 */

static int    argc_internal = 0;
static char** argv_internal = NULL;

static void rvvm_set_args_internal(int argc, char** argv)
{
    if (argc > 1) {
        argc_internal = argc;
        argv_internal = argv;
    }
}

void rvvm_set_args(int argc, char** argv)
{
    rvvm_set_args_internal(argc, argv);
    if (rvvm_has_arg("v") || rvvm_has_arg("verbose")) {
        rvvm_set_loglevel(LOG_INFO);
    }
}

static inline bool rvvm_config_is_newline(char c)
{
    return c == '\n' || c == '\r';
}

static inline bool rvvm_config_is_space(char c)
{
    return c == ' ' || c == '\t';
}

static inline bool rvvm_config_is_delim(char c)
{
    return rvvm_config_is_space(c) || rvvm_config_is_newline(c);
}

static inline bool rvvm_config_is_comment(char c)
{
    return c == '#';
}

static inline bool rvvm_config_is_quot(char c)
{
    return c == '\"';
}

static int rvvm_split_config(const char* str, int argc, char** argv)
{
    bool arg = true;
    int  ret = 1;
    if (argc && argv) {
        argv[0] = "rvvm";
    }
    while (*str) {
        // Skip delimiters (spaces, newlines)
        while (rvvm_config_is_delim(*str)) {
            if (rvvm_config_is_newline(*str)) {
                // Found newline
                arg = true;
            }
            str++;
        }
        if (rvvm_config_is_comment(*str)) {
            // Skip comment
            while (*str && !rvvm_config_is_newline(*str)) {
                str++;
            }
        } else {
            // Parse token
            bool        quoted = rvvm_config_is_quot(*str);
            const char* token  = str + quoted;
            if (quoted) {
                // Find end of quotation
                do {
                    str++;
                } while (*str && !rvvm_config_is_quot(*str));
                str += rvvm_config_is_quot(*str);
            } else {
                // Find delimiter or start of comment
                while (*str && !rvvm_config_is_delim(*str) && !rvvm_config_is_comment(*str)) {
                    str++;
                }
            }
            if (ret < argc && argv) {
                // Fill argv
                size_t size   = str - token - quoted;
                char*  buffer = safe_new_arr(char, size + arg + 1);
                if (arg) {
                    buffer[0] = '-';
                }
                memcpy(buffer + arg, token, size);
                argv[ret] = buffer;
            }
            arg = false;
            ret++;
        }
    }
    return ret;
}

bool rvvm_load_config(const char* path)
{
    rvfile_t* cfg = rvopen(path, 0);
    if (rvfilesize(cfg) && rvfilesize(cfg) < 0x100000) {
        char* buf = safe_new_arr(char, rvfilesize(cfg) + 1);
        if (rvread(cfg, buf, rvfilesize(cfg), 0) == rvfilesize(cfg)) {
            int argc = rvvm_split_config(buf, 0, NULL);
            if (argc > 1) {
                char** argv = safe_new_arr(char*, argc + 1);
                rvvm_split_config(buf, argc, argv);
                rvvm_set_args_internal(argc, argv);
            }
        }
        safe_free(buf);
    }
    rvclose(cfg);
    return !!cfg;
}

const char* rvvm_next_arg(const char** val, int* iter)
{
    int i = *iter;
    if (argc_internal < 2) {
        DO_ONCE(rvvm_load_config("rvvm.cfg"));
    }
    if (val) {
        *val = NULL;
    }
    if (argv_internal && i < argc_internal && argv_internal[i]) {
        const char* arg = argv_internal[i];
        if (arg[0] == '-') {
            // Skip -, -- argument prefix
            arg += (arg[1] == '-') ? 2 : 1;
            // Get trailing argument value, if any
            if (i + 1 < argc_internal) {
                const char* next_arg = argv_internal[i + 1];
                if (next_arg && next_arg[0] != '-') {
                    if (val) {
                        *val = next_arg;
                    }
                    i++;
                }
            }
        } else {
            // This is a free-standing value without -arg prefix
            if (val) {
                *val = arg;
            }
            arg = "";
        }
        *iter = i + 1;
        return arg;
    }
    return NULL;
}

bool rvvm_has_arg(const char* arg)
{
    const char* arg_name = NULL;
    int         arg_iter = 1;
    while ((arg_name = rvvm_next_arg(NULL, &arg_iter))) {
        if (rvvm_strcmp(arg_name, arg)) {
            return true;
        }
    }
    return false;
}

const char* rvvm_getarg(const char* arg)
{
    const char* arg_name = NULL;
    const char* arg_val  = NULL;
    int         arg_iter = 1;
    while ((arg_name = rvvm_next_arg(&arg_val, &arg_iter))) {
        if (rvvm_strcmp(arg_name, arg)) {
            return arg_val;
        }
    }
    return NULL;
}

bool rvvm_getarg_bool(const char* arg)
{
    const char* arg_val = rvvm_getarg(arg);
    if (arg_val) {
        return rvvm_strcmp(arg_val, "true");
    }
    return false;
}

int rvvm_getarg_int(const char* arg)
{
    const char* arg_val = rvvm_getarg(arg);
    if (arg_val) {
        return str_to_int_dec(arg_val);
    }
    return 0;
}

uint64_t rvvm_getarg_size(const char* arg)
{
    const char* arg_val = rvvm_getarg(arg);
    if (arg_val) {
        size_t   len = 0;
        uint64_t ret = str_to_int_base(arg_val, &len, 0);
        return ret << mem_suffix_shift(arg_val[len]);
    }
    return 0;
}

/*
 * Logger
 */

static int rvvm_loglevel = LOG_WARN;

void rvvm_set_loglevel(int loglevel)
{
    rvvm_loglevel = loglevel;
}

static bool log_has_colors(void)
{
    // TERM is set on any POSIX, WT_SESSION is set by Windows Terminal
    return !!getenv("TERM") || !!getenv("WT_SESSION");
}

static void log_print(const char* prefix, const char* fmt, const void* argv)
{
    char   buffer[256] = {0};
    size_t pos         = rvvm_strlcpy(buffer, prefix, sizeof(buffer));
    size_t vsp_size    = sizeof(buffer) - EVAL_MIN(pos + 6, sizeof(buffer));
    if (vsp_size > 1) {
        int tmp = rvvm_vsnprintf(buffer + pos, vsp_size, fmt, argv);
        if (tmp > 0) {
            pos += EVAL_MIN(vsp_size - 1, (size_t)tmp);
        }
    }
    rvvm_strlcpy(buffer + pos, log_has_colors() ? "\033[0m\n" : "\n", sizeof(buffer) - pos);
    fputs(buffer, stderr);
}

#if defined(USE_DEBUG)

PRINT_FORMAT void rvvm_debug(const char* format_str, ...)
{
    if (rvvm_loglevel >= LOG_INFO) {
        va_list args;
        va_start(args, format_str);
        log_print(log_has_colors() ? "\033[33;1mDEBUG\033[0;1m: " : "DEBUG: ", format_str, &args);
        va_end(args);
    }
}

#endif

PRINT_FORMAT void rvvm_info(const char* format_str, ...)
{
    if (rvvm_loglevel >= LOG_INFO) {
        va_list args;
        va_start(args, format_str);
        log_print(log_has_colors() ? "\033[33;1mINFO\033[0;1m: " : "INFO: ", format_str, &args);
        va_end(args);
    }
}

PRINT_FORMAT void rvvm_warn(const char* format_str, ...)
{
    if (rvvm_loglevel >= LOG_WARN) {
        va_list args;
        va_start(args, format_str);
        log_print(log_has_colors() ? "\033[31;1mWARN\033[0;1m: " : "WARN: ", format_str, &args);
        va_end(args);
    }
}

PRINT_FORMAT void rvvm_error(const char* format_str, ...)
{
    if (rvvm_loglevel >= LOG_ERROR) {
        va_list args;
        va_start(args, format_str);
        log_print(log_has_colors() ? "\033[31;1mERROR\033[0;1m: " : "ERROR: ", format_str, &args);
        va_end(args);
    }
}

PRINT_FORMAT void rvvm_fatal(const char* format_str, ...)
{
    va_list args;
    va_start(args, format_str);
    log_print(log_has_colors() ? "\033[31;1mFATAL\033[0;1m: " : "FATAL: ", format_str, &args);
    va_end(args);
    stacktrace_print();
    abort();
    // cppcheck-suppress unreachableCode
    must_never_reach();
}

/*
 * Initialization/deinitialization
 */

slow_path void do_once_finalize(uint32_t* ticket)
{
    while (atomic_load_uint32_ex(ticket, ATOMIC_ACQUIRE) != 2) {
        thread_sched_yield();
    }
}

typedef void (*deinit_func_t)(void);

static vector_t(deinit_func_t) deinit_funcs = ZERO_INIT;
static spinlock_t              deinit_lock  = ZERO_INIT;
static bool                    deinit_made  = false;

void call_at_deinit(void (*function)(void))
{
    spin_lock_busy_loop(&deinit_lock);
    if (!deinit_made) {
        vector_push_back(deinit_funcs, function);
        function = NULL;
    }
    spin_unlock(&deinit_lock);
    if (function) {
        function();
    }
}

static deinit_func_t dequeue_func(void)
{
    deinit_func_t ret = NULL;
    spin_lock_busy_loop(&deinit_lock);
    deinit_made = true;
    if (vector_size(deinit_funcs)) {
        size_t end = vector_size(deinit_funcs) - 1;
        ret        = vector_at(deinit_funcs, end);
        vector_erase(deinit_funcs, end);
    }
    spin_unlock(&deinit_lock);
    return ret;
}

GNU_DESTRUCTOR void full_deinit(void)
{
    deinit_func_t func = dequeue_func();
    if (func) {
        rvvm_info("Fully deinitializing librvvm");
        do {
            func();
            func = dequeue_func();
        } while (func);
    }
}

POP_OPTIMIZATION_SIZE
