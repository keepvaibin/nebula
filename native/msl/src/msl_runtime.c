/* MSL's C library behaviour where the game relies on it and the host's differs (nebula_prelude.h maps the names):
   16-bit wchar_t strings and MSL's formatters (ports of MSL_C/printf.c and wprintf.c with the console's 32-bit long),
   MSL's floating-point class codes, and the "C" locale's character maps. Compiled into the game module with its
   flags. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <locale.h>

#include "msl_ctype_tables.h"

/* The host's own formatter supplies float digits here (the prelude maps the game's calls to MSL's). */
#undef sprintf
#undef snprintf
#undef vsprintf
#undef vsnprintf

static struct _loc_ctype_cmpt sCtypeC = {
    "C", nebula_msl_ctype_map, nebula_msl_upper_map, nebula_msl_lower_map, 0, 0, 0, 0, 0,
};

struct __locale _current_locale = {0, "C", 0, &sCtypeC, 0, 0, 0};

int nebula_msl_fpclassifyf(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    switch (bits & 0x7F800000u) {
    case 0x7F800000u:
        return (bits & 0x7FFFFFu) ? 1 : 2;
    case 0:
        return (bits & 0x7FFFFFu) ? 5 : 3;
    }
    return 4;
}

int nebula_msl_fpclassifyd(double x) {
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    switch (bits & 0x7FF0000000000000ull) {
    case 0x7FF0000000000000ull:
        return (bits & 0xFFFFFFFFFFFFFull) ? 1 : 2;
    case 0:
        return (bits & 0xFFFFFFFFFFFFFull) ? 5 : 3;
    }
    return 4;
}

size_t nebula_msl_wcslen(const wchar_t* s) {
    const wchar_t* p = s;
    while (*p) {
        ++p;
    }
    return (size_t)(p - s);
}

int nebula_msl_wcscmp(const wchar_t* a, const wchar_t* b) {
    wchar_t c1, c2;
    while ((c1 = *a++) == (c2 = *b++)) {
        if (!c1) {
            return 0;
        }
    }
    return c1 - c2;
}

wchar_t* nebula_msl_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) {
        dst[i] = src[i];
    }
    for (; i < n; ++i) {
        dst[i] = 0;
    }
    return dst;
}

wchar_t* nebula_msl_wcschr(const wchar_t* s, wchar_t c) {
    for (;; ++s) {
        if (*s == c) {
            return (wchar_t*)s;
        }
        if (!*s) {
            return 0;
        }
    }
}

/* The formatter: parse_format, long2str, longlong2str and __pformatter/__wpformatter as MSL has them (msl_format.inc,
   once for wide and once for narrow characters). */

enum { left_justification, right_justification, zero_fill };
enum { only_minus, sign_always, space_holder };
enum {
    normal_argument,
    char_argument,
    short_argument,
    long_argument,
    long_long_argument,
    wchar_argument,
    intmax_argument,
    size_t_argument,
    ptrdiff_argument,
    long_double_argument
};

#define BAD_CONVERSION 0xFFFF

typedef struct {
    unsigned char justification_options;
    unsigned char sign_options;
    unsigned char precision_specified;
    unsigned char alternate_form;
    unsigned char argument_options;
    unsigned char pointer;
    unsigned int conversion_char;
    int field_width;
    int precision;
} print_format;

typedef struct {
    wchar_t* str;
    size_t max;
    size_t written;
} out_ctrl_w;

typedef struct {
    char* str;
    size_t max;
    size_t written;
} out_ctrl_n;

#define FMT_CHAR wchar_t
#define FMT_L(x) L##x
#define FMT_NAME(n) n##_w
#define FMT_WIDE 1
#define FMT_STRLEN nebula_msl_wcslen
#define FMT_STRCHR nebula_msl_wcschr
#include "msl_format.inc"
#undef FMT_CHAR
#undef FMT_L
#undef FMT_NAME
#undef FMT_WIDE
#undef FMT_STRLEN
#undef FMT_STRCHR

#define FMT_CHAR char
#define FMT_L(x) x
#define FMT_NAME(n) n##_n
#define FMT_WIDE 0
#define FMT_STRLEN strlen
#define FMT_STRCHR strchr
#include "msl_format.inc"

int nebula_msl_vswprintf(wchar_t* s, size_t n, const wchar_t* format, va_list arg) {
    out_ctrl_w o;
    va_list ap;
    int count;
    if (n == 0) {
        return -1;
    }
    o.str = s;
    o.max = n;
    o.written = 0;
    va_copy(ap, arg);
    count = formatter_w(&o, format, &ap);
    va_end(ap);
    if (count >= 0) {
        if ((size_t)count < n) {
            s[count] = 0;
        } else {
            s[n - 1] = 0;
            count = -1;
        }
    }
    return count;
}

int nebula_msl_swprintf(wchar_t* s, size_t n, const wchar_t* format, ...) {
    va_list ap;
    int count;
    va_start(ap, format);
    count = nebula_msl_vswprintf(s, n, format, ap);
    va_end(ap);
    return count;
}

/* MSL's vsnprintf: the count the whole output takes, the buffer holding what fits and a NUL. sprintf and vsprintf
   are it with no limit. */
int nebula_msl_vsnprintf(char* s, size_t n, const char* format, va_list arg) {
    out_ctrl_n o;
    va_list ap;
    int end;
    o.str = s;
    o.max = s != 0 ? n : 0;
    o.written = 0;
    va_copy(ap, arg);
    end = formatter_n(&o, format, &ap);
    va_end(ap);
    if (s != 0) {
        if (end >= 0 && (size_t)end < n) {
            s[end] = '\0';
        } else if (n > 0) {
            s[n - 1] = '\0';
        }
    }
    return end;
}

int nebula_msl_snprintf(char* s, size_t n, const char* format, ...) {
    va_list ap;
    int end;
    va_start(ap, format);
    end = nebula_msl_vsnprintf(s, n, format, ap);
    va_end(ap);
    return end;
}

int nebula_msl_vsprintf(char* s, const char* format, va_list arg) {
    return nebula_msl_vsnprintf(s, 0xFFFFFFFFu, format, arg);
}

int nebula_msl_sprintf(char* s, const char* format, ...) {
    va_list ap;
    int end;
    va_start(ap, format);
    end = nebula_msl_vsnprintf(s, 0xFFFFFFFFu, format, ap);
    va_end(ap);
    return end;
}
