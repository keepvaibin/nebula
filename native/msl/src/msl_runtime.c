/* MSL's C library behaviour where the game relies on it and the host's differs (nebula_prelude.h maps the names):
   16-bit wchar_t strings and MSL's wide formatter (a port of MSL_C/wprintf.c with the console's 32-bit long),
   MSL's floating-point class codes, and the "C" locale's character maps. Compiled into the game module with its
   flags. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <locale.h>

#include "msl_ctype_tables.h"

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

/* The formatter: parse_format, long2str, longlong2str and __wpformatter as MSL has them. */

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
    wchar_t conversion_char;
    int field_width;
    int precision;
} print_format;

typedef struct {
    wchar_t* str;
    size_t max;
    size_t written;
} out_ctrl;

static int is_digit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

static const wchar_t* parse_format(const wchar_t* s, va_list* ap, print_format* out) {
    print_format f;
    wchar_t c;
    int flag;
    f.justification_options = right_justification;
    f.sign_options = only_minus;
    f.precision_specified = 0;
    f.alternate_form = 0;
    f.argument_options = normal_argument;
    f.pointer = 0;
    f.field_width = 0;
    f.precision = 0;

    if ((c = *++s) == L'%') {
        f.conversion_char = c;
        *out = f;
        return s + 1;
    }

    for (;;) {
        flag = 1;
        switch (c) {
        case L'-':
            f.justification_options = left_justification;
            break;
        case L'+':
            f.sign_options = sign_always;
            break;
        case L' ':
            if (f.sign_options != sign_always) {
                f.sign_options = space_holder;
            }
            break;
        case L'#':
            f.alternate_form = 1;
            break;
        case L'0':
            if (f.justification_options != left_justification) {
                f.justification_options = zero_fill;
            }
            break;
        default:
            flag = 0;
            break;
        }
        if (!flag) {
            break;
        }
        c = *++s;
    }

    if (c == L'*') {
        if ((f.field_width = va_arg(*ap, int)) < 0) {
            f.justification_options = left_justification;
            f.field_width = -f.field_width;
        }
        c = *++s;
    } else {
        while (is_digit(c)) {
            f.field_width = f.field_width * 10 + (c - L'0');
            c = *++s;
        }
    }

    if (f.field_width > 509) {
        f.conversion_char = BAD_CONVERSION;
        *out = f;
        return s + 1;
    }

    if (c == L'.') {
        f.precision_specified = 1;
        if ((c = *++s) == L'*') {
            if ((f.precision = va_arg(*ap, int)) < 0) {
                f.precision_specified = 0;
            }
            c = *++s;
        } else {
            while (is_digit(c)) {
                f.precision = f.precision * 10 + (c - L'0');
                c = *++s;
            }
        }
    }

    flag = 1;
    switch (c) {
    case L'h':
        f.argument_options = short_argument;
        if (s[1] == L'h') {
            f.argument_options = char_argument;
            c = *++s;
        }
        break;
    case L'l':
        f.argument_options = long_argument;
        if (s[1] == L'l') {
            f.argument_options = long_long_argument;
            c = *++s;
        }
        break;
    case L'L':
        f.argument_options = long_double_argument;
        break;
    case L'j':
        f.argument_options = intmax_argument;
        break;
    case L't':
        f.argument_options = ptrdiff_argument;
        break;
    case L'z':
        f.argument_options = size_t_argument;
        break;
    default:
        flag = 0;
        break;
    }
    if (flag) {
        c = *++s;
    }

    f.conversion_char = c;
    switch (c) {
    case L'd':
    case L'i':
    case L'u':
    case L'o':
    case L'x':
    case L'X':
        if (f.argument_options == long_double_argument) {
            f.argument_options = long_long_argument;
        }
        if (!f.precision_specified) {
            f.precision = 1;
        } else if (f.justification_options == zero_fill) {
            f.justification_options = right_justification;
        }
        break;
    case L'f':
    case L'F':
        if (f.argument_options == short_argument || f.argument_options == intmax_argument ||
            f.argument_options == size_t_argument || f.argument_options == ptrdiff_argument ||
            f.argument_options == long_long_argument) {
            f.conversion_char = BAD_CONVERSION;
            break;
        }
        if (!f.precision_specified) {
            f.precision = 6;
        }
        break;
    case L'a':
    case L'A':
        if (!f.precision_specified) {
            f.precision = 0xD;
        }
        if (f.argument_options == short_argument || f.argument_options == intmax_argument ||
            f.argument_options == size_t_argument || f.argument_options == ptrdiff_argument ||
            f.argument_options == long_long_argument || f.argument_options == char_argument) {
            f.conversion_char = BAD_CONVERSION;
        }
        break;
    case L'g':
    case L'G':
        if (!f.precision) {
            f.precision = 1;
        }
        /* fall through, as in MSL */
    case L'e':
    case L'E':
        if (f.argument_options == short_argument || f.argument_options == intmax_argument ||
            f.argument_options == size_t_argument || f.argument_options == ptrdiff_argument ||
            f.argument_options == long_long_argument || f.argument_options == char_argument) {
            f.conversion_char = BAD_CONVERSION;
            break;
        }
        if (!f.precision_specified) {
            f.precision = 6;
        }
        break;
    case L'p':
        f.pointer = 1;
        f.argument_options = long_argument;
        f.alternate_form = 1;
        f.conversion_char = L'x';
        f.precision = 8;
        break;
    case L'c':
        if (f.argument_options == long_argument) {
            f.argument_options = wchar_argument;
        } else if (f.precision_specified || f.argument_options != normal_argument) {
            f.conversion_char = BAD_CONVERSION;
        }
        break;
    case L's':
        if (f.argument_options == long_argument) {
            f.argument_options = wchar_argument;
        } else if (f.argument_options != normal_argument) {
            f.conversion_char = BAD_CONVERSION;
        }
        break;
    case L'n':
        if (f.argument_options == long_double_argument) {
            f.argument_options = long_long_argument;
        }
        break;
    default:
        f.conversion_char = BAD_CONVERSION;
        break;
    }

    *out = f;
    return s + 1;
}

/* Digits right-aligned ending at buff[-1] (a NUL there); returns the first, or 0 past 509 characters. */
static wchar_t* int2str(uint64_t magnitude, int minus, unsigned base, wchar_t* buff, print_format f) {
    wchar_t* p = buff;
    int digits = 0;
    *--p = 0;

    if (!magnitude && !f.precision && !(f.alternate_form && f.conversion_char == L'o')) {
        return p;
    }

    do {
        unsigned n = (unsigned)(magnitude % base);
        magnitude /= base;
        if (n < 10) {
            n += L'0';
        } else {
            n -= 10;
            n += (f.conversion_char == L'x') ? L'a' : L'A';
        }
        *--p = (wchar_t)n;
        ++digits;
    } while (magnitude != 0);

    if (base == 8 && f.alternate_form && *p != L'0') {
        *--p = L'0';
        ++digits;
    }

    if (f.justification_options == zero_fill) {
        f.precision = f.field_width;
        if (minus || f.sign_options != only_minus) {
            --f.precision;
        }
        if (base == 16 && f.alternate_form) {
            f.precision -= 2;
        }
    }

    if (buff - p + f.precision > 509) {
        return 0;
    }

    while (digits < f.precision) {
        *--p = L'0';
        ++digits;
    }

    if (base == 16 && f.alternate_form) {
        *--p = f.conversion_char;
        *--p = L'0';
    }

    if (minus) {
        *--p = L'-';
    } else if (f.sign_options == sign_always) {
        *--p = L'+';
    } else if (f.sign_options == space_holder) {
        *--p = L' ';
    }
    return p;
}

/* The console's long is 32 bits: l-sized arguments are read and printed as 32-bit values. */
static wchar_t* num2str(int is_signed, int64_t value, int wide, wchar_t* buff, print_format f) {
    unsigned base = 10;
    int minus = 0;
    uint64_t magnitude;

    switch (f.conversion_char) {
    case L'o':
        base = 8;
        f.sign_options = only_minus;
        break;
    case L'u':
        f.sign_options = only_minus;
        break;
    case L'x':
    case L'X':
        base = 16;
        f.sign_options = only_minus;
        break;
    default:
        break;
    }

    if (wide) {
        magnitude = (uint64_t)value;
        if (is_signed && value < 0) {
            minus = 1;
            magnitude = 0 - (uint64_t)value;
        }
    } else {
        const int32_t v = (int32_t)value;
        magnitude = (uint32_t)v;
        if (is_signed && v < 0) {
            minus = 1;
            magnitude = 0u - (uint32_t)v;
        }
    }
    return int2str(magnitude, minus, base, buff, f);
}

static int write_out(out_ctrl* o, const wchar_t* src, size_t n) {
    size_t room = o->written + n <= o->max ? n : o->max - o->written;
    memmove(o->str + o->written, src, room * sizeof(wchar_t));
    o->written += room;
    return 1;
}

/* Floats are not in the game's wide formats; the host's narrow formatter supplies the digits. */
static wchar_t* float2str(long double value, wchar_t* buff, print_format f) {
    char spec[16], text[512];
    int k = 0, n, i;
    spec[k++] = '%';
    if (f.sign_options == sign_always) {
        spec[k++] = '+';
    } else if (f.sign_options == space_holder) {
        spec[k++] = ' ';
    }
    if (f.alternate_form) {
        spec[k++] = '#';
    }
    spec[k++] = '.';
    spec[k++] = '*';
    spec[k++] = 'L';
    spec[k++] = (char)f.conversion_char;
    spec[k] = 0;
    n = snprintf(text, sizeof(text), spec, f.precision, value);
    if (n < 0 || n >= (int)sizeof(text) || n > 509) {
        return 0;
    }
    buff -= n + 1;
    for (i = 0; i <= n; ++i) {
        buff[i] = (unsigned char)text[i];
    }
    return buff;
}

static int wpformatter(out_ctrl* o, const wchar_t* format, va_list* ap) {
    int num_chars, chars_written = 0, field_width;
    const wchar_t* format_ptr = format;
    const wchar_t* curr_format;
    print_format f;
    wchar_t buff[512];
    wchar_t* buff_ptr;
    wchar_t fill_char;

    while (*format_ptr) {
        if (!(curr_format = nebula_msl_wcschr(format_ptr, L'%'))) {
            num_chars = (int)nebula_msl_wcslen(format_ptr);
            chars_written += num_chars;
            if (num_chars) {
                write_out(o, format_ptr, (size_t)num_chars);
            }
            break;
        }

        num_chars = (int)(curr_format - format_ptr);
        chars_written += num_chars;
        if (num_chars) {
            write_out(o, format_ptr, (size_t)num_chars);
        }

        format_ptr = parse_format(curr_format, ap, &f);

        switch (f.conversion_char) {
        case L'd':
        case L'i':
        case L'o':
        case L'u':
        case L'x':
        case L'X': {
            const int is_signed = f.conversion_char == L'd' || f.conversion_char == L'i';
            int64_t value;
            int wide = 0;
            if (f.argument_options == long_long_argument || f.argument_options == intmax_argument) {
                value = va_arg(*ap, long long);
                wide = 1;
            } else if (f.argument_options == size_t_argument) {
                value = (int64_t)va_arg(*ap, size_t);
            } else if (f.argument_options == ptrdiff_argument) {
                value = (int64_t)va_arg(*ap, ptrdiff_t);
            } else if (f.pointer) {
                /* %p: a native pointer, printed in full (the console's was 32 bits). */
                value = (int64_t)(uintptr_t)va_arg(*ap, void*);
                wide = 1;
            } else {
                value = is_signed ? (int64_t)va_arg(*ap, int) : (int64_t)va_arg(*ap, unsigned int);
            }
            if (f.argument_options == short_argument) {
                value = is_signed ? (int64_t)(short)value : (int64_t)(unsigned short)value;
            }
            if (!(buff_ptr = num2str(is_signed, value, wide, buff + 512, f))) {
                goto conversion_error;
            }
            num_chars = (int)(buff + 512 - 1 - buff_ptr);
            break;
        }

        case L'f':
        case L'F':
        case L'e':
        case L'E':
        case L'g':
        case L'G':
        case L'a':
        case L'A': {
            long double value = f.argument_options == long_double_argument ? va_arg(*ap, long double)
                                                                           : (long double)va_arg(*ap, double);
            if (!(buff_ptr = float2str(value, buff + 512, f))) {
                goto conversion_error;
            }
            num_chars = (int)(buff + 512 - 1 - buff_ptr);
            break;
        }

        case L's':
            if (f.argument_options == wchar_argument) {
                buff_ptr = va_arg(*ap, wchar_t*);
                if (buff_ptr == 0) {
                    static wchar_t empty[1];
                    buff_ptr = empty;
                }
                if (f.alternate_form) {
                    num_chars = (unsigned char)*buff_ptr++;
                    if (f.precision_specified && num_chars > f.precision) {
                        num_chars = f.precision;
                    }
                } else if (f.precision_specified) {
                    int k = 0;
                    while (k < f.precision && buff_ptr[k]) {
                        ++k;
                    }
                    num_chars = k;
                } else {
                    num_chars = (int)nebula_msl_wcslen(buff_ptr);
                }
            } else {
                const char* str = va_arg(*ap, const char*);
                int k;
                if (str == 0) {
                    str = "";
                }
                if (f.alternate_form) {
                    num_chars = (unsigned char)*str++;
                    if (f.precision_specified && num_chars > f.precision) {
                        num_chars = f.precision;
                    }
                } else if (f.precision_specified) {
                    const char* end = (const char*)memchr(str, 0, (size_t)f.precision);
                    num_chars = end ? (int)(end - str) : f.precision;
                } else {
                    num_chars = (int)strlen(str);
                }
                /* MSL's mbstowcs in the "C" locale: each byte widened, stopping at a NUL. */
                if (num_chars > 512) {
                    num_chars = 512;
                }
                for (k = 0; k < num_chars && str[k]; ++k) {
                    buff[k] = (unsigned char)str[k];
                }
                num_chars = k;
                buff_ptr = buff;
            }
            break;

        case L'n': {
            void* dst = va_arg(*ap, void*);
            switch (f.argument_options) {
            case normal_argument:
            case long_argument:
                *(int*)dst = chars_written;
                break;
            case short_argument:
                *(short*)dst = (short)chars_written;
                break;
            case intmax_argument:
            case long_long_argument:
                *(long long*)dst = chars_written;
                break;
            case size_t_argument:
                *(size_t*)dst = (size_t)chars_written;
                break;
            case ptrdiff_argument:
                *(ptrdiff_t*)dst = chars_written;
                break;
            default:
                break;
            }
            continue;
        }

        case L'c':
            buff_ptr = buff;
            if (f.argument_options == wchar_argument) {
                *buff_ptr = (wchar_t)va_arg(*ap, int);
                num_chars = 1;
            } else {
                /* MSL's mbtowc: a NUL character converts to nothing. */
                const char ch = (char)va_arg(*ap, int);
                *buff_ptr = (unsigned char)ch;
                num_chars = ch ? 1 : 0;
            }
            break;

        case L'%':
            buff_ptr = buff;
            *buff_ptr = L'%';
            num_chars = 1;
            break;

        default:
        conversion_error:
            /* The rest of the format, unconverted, ends the output. */
            num_chars = (int)nebula_msl_wcslen(curr_format);
            chars_written += num_chars;
            if (num_chars) {
                write_out(o, curr_format, (size_t)num_chars);
            }
            return chars_written;
        }

        field_width = num_chars;
        if (f.justification_options != left_justification) {
            fill_char = (f.justification_options == zero_fill) ? L'0' : L' ';
            if ((*buff_ptr == L'+' || *buff_ptr == L'-' || *buff_ptr == L' ') && fill_char == L'0') {
                write_out(o, buff_ptr, 1);
                ++buff_ptr;
                num_chars--;
            }
            while (field_width < f.field_width) {
                write_out(o, &fill_char, 1);
                ++field_width;
            }
        }
        if (num_chars) {
            write_out(o, buff_ptr, (size_t)num_chars);
        }
        if (f.justification_options == left_justification) {
            const wchar_t blank = L' ';
            while (field_width < f.field_width) {
                write_out(o, &blank, 1);
                ++field_width;
            }
        }
        chars_written += field_width;
    }
    return chars_written;
}

int nebula_msl_vswprintf(wchar_t* s, size_t n, const wchar_t* format, va_list arg) {
    out_ctrl o;
    va_list ap;
    int count;
    if (n == 0) {
        return -1;
    }
    o.str = s;
    o.max = n;
    o.written = 0;
    va_copy(ap, arg);
    count = wpformatter(&o, format, &ap);
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
