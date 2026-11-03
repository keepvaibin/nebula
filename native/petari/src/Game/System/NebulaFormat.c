#include "nebula_format.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    char* pDst;
    size_t capacity;
    size_t count;
} Out;

static void put(Out* pOut, char c) {
    if (pOut->count + 1 < pOut->capacity) {
        pOut->pDst[pOut->count] = c;
    }
    pOut->count++;
}

static void pad(Out* pOut, char c, int count) {
    for (; count > 0; --count) {
        put(pOut, c);
    }
}

/* prefix, then digits padded by the flags; `body` already holds any precision's zeros. */
static void putField(Out* pOut, const char* pPrefix, const char* pBody, int bodyLength, int width, int left, int zero) {
    const int prefixLength = (int)strlen(pPrefix);
    const int fill = width - prefixLength - bodyLength;
    if (!left && !zero) {
        pad(pOut, ' ', fill);
    }
    for (int i = 0; i < prefixLength; ++i) {
        put(pOut, pPrefix[i]);
    }
    if (!left && zero) {
        pad(pOut, '0', fill);
    }
    for (int i = 0; i < bodyLength; ++i) {
        put(pOut, pBody[i]);
    }
    if (left) {
        pad(pOut, ' ', fill);
    }
}

static void putInteger(Out* pOut, unsigned long long value, unsigned base, int upper, int negative, int plus, int space, int alt, int width, int precision, int left, int zero) {
    const char* const digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char text[128];
    int at = (int)sizeof(text);
    for (unsigned long long v = value; v != 0; v /= base) {
        text[--at] = digits[v % base];
    }
    if (precision < 0) {
        precision = 1;
    } else {
        zero = 0;
    }
    if (precision > 100) {
        precision = 100;
    }
    while ((int)sizeof(text) - at < precision) {
        text[--at] = '0';
    }
    char prefix[4];
    int p = 0;
    if (negative) {
        prefix[p++] = '-';
    } else if (plus) {
        prefix[p++] = '+';
    } else if (space) {
        prefix[p++] = ' ';
    }
    if (alt && value != 0 && base == 16) {
        prefix[p++] = '0';
        prefix[p++] = upper ? 'X' : 'x';
    } else if (alt && base == 8 && text[at] != '0') {
        text[--at] = '0';
    }
    prefix[p] = '\0';
    putField(pOut, prefix, text + at, (int)sizeof(text) - at, width, left, zero);
}

/* Fixed notation, rounded half to even on the exact value's first digits a double can hold. */
static void putFixed(Out* pOut, double value, int precision, int plus, int space, int width, int left, int zero) {
    char prefix[2] = {0, 0};
    const uint64_t bits = __builtin_bit_cast(uint64_t, value);
    if (bits >> 63) {
        prefix[0] = '-';
        value = -value;
    } else if (plus) {
        prefix[0] = '+';
    } else if (space) {
        prefix[0] = ' ';
    }
    if (value != value) {
        putField(pOut, prefix, "nan", 3, width, left, 0);
        return;
    }
    if (value == __builtin_inf()) {
        putField(pOut, prefix, "inf", 3, width, left, 0);
        return;
    }
    if (precision < 0) {
        precision = 6;
    }
    if (precision > 40) {
        precision = 40;
    }
    char text[400];
    int used = 0;
    /* The digits of the whole part, from the top. */
    double scale = 1.0;
    while (value / scale >= 10.0 && used < 320) {
        scale *= 10.0;
        ++used;
    }
    double whole = __builtin_floor(value);
    double fraction = value - whole;
    used = 0;
    for (double s = scale; s >= 1.0; s /= 10.0) {
        const int d = (int)(whole / s);
        text[used++] = (char)('0' + (d > 9 ? 9 : d));
        whole -= (double)d * s;
    }
    const int point = used;
    for (int i = 0; i < precision; ++i) {
        fraction *= 10.0;
        const int d = (int)fraction;
        text[used++] = (char)('0' + d);
        fraction -= d;
    }
    int up = fraction > 0.5;
    if (fraction == 0.5) {
        up = (text[used - 1] - '0') & 1;
    }
    if (up) {
        int i = used - 1;
        for (; i >= 0 && text[i] == '9'; --i) {
            text[i] = '0';
        }
        if (i >= 0) {
            text[i]++;
        } else {
            memmove(text + 1, text, (size_t)used);
            text[0] = '1';
            ++used;
        }
    }
    const int shift = used - point - precision;
    char field[408];
    int n = 0;
    for (int i = 0; i < point + shift; ++i) {
        field[n++] = text[i];
    }
    if (precision > 0) {
        field[n++] = '.';
        for (int i = point + shift; i < used; ++i) {
            field[n++] = text[i];
        }
    }
    putField(pOut, prefix, field, n, width, left, zero);
}

int nebula_vsnprintf(char* pDst, size_t length, const char* pFormat, va_list args) {
    Out out = {pDst, length, 0};
    for (const char* p = pFormat; *p != '\0'; ++p) {
        if (*p != '%') {
            put(&out, *p);
            continue;
        }
        ++p;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
        for (;; ++p) {
            if (*p == '-') {
                left = 1;
            } else if (*p == '+') {
                plus = 1;
            } else if (*p == ' ') {
                space = 1;
            } else if (*p == '#') {
                alt = 1;
            } else if (*p == '0') {
                zero = 1;
            } else {
                break;
            }
        }
        int width = 0;
        if (*p == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            ++p;
        } else {
            for (; *p >= '0' && *p <= '9'; ++p) {
                width = width * 10 + (*p - '0');
            }
        }
        int precision = -1;
        if (*p == '.') {
            ++p;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(args, int);
                ++p;
            } else {
                for (; *p >= '0' && *p <= '9'; ++p) {
                    precision = precision * 10 + (*p - '0');
                }
            }
        }
        int longs = 0;
        int shorts = 0;
        int sized = 0;
        for (;; ++p) {
            if (*p == 'l') {
                ++longs;
            } else if (*p == 'h') {
                ++shorts;
            } else if (*p == 'z' || *p == 't' || *p == 'j') {
                sized = 1;
            } else if (*p != 'L') {
                break;
            }
        }
        const char c = *p;
        if (c == '\0') {
            break;
        }
        switch (c) {
        case 'd':
        case 'i': {
            long long v;
            if (longs >= 2 || sized) {
                v = va_arg(args, long long);
            } else if (longs == 1) {
                v = va_arg(args, long);
            } else {
                v = va_arg(args, int);
                if (shorts == 1) {
                    v = (short)v;
                } else if (shorts >= 2) {
                    v = (signed char)v;
                }
            }
            putInteger(&out, v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v, 10, 0, v < 0, plus, space, 0, width, precision, left, zero);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v;
            if (longs >= 2 || sized) {
                v = va_arg(args, unsigned long long);
            } else if (longs == 1) {
                v = va_arg(args, unsigned long);
            } else {
                v = va_arg(args, unsigned);
                if (shorts == 1) {
                    v = (unsigned short)v;
                } else if (shorts >= 2) {
                    v = (unsigned char)v;
                }
            }
            putInteger(&out, v, c == 'u' ? 10u : c == 'o' ? 8u : 16u, c == 'X', 0, 0, 0, alt, width, precision, left, zero);
            break;
        }
        case 'p':
            putInteger(&out, (unsigned long long)(uintptr_t)va_arg(args, void*), 16, 0, 0, 0, 0, 1, width, -1, left, 0);
            break;
        case 'c': {
            const char one = (char)va_arg(args, int);
            putField(&out, "", &one, 1, width, left, 0);
            break;
        }
        case 's':
            if (longs != 0) {
                const wchar_t* pWide = va_arg(args, const wchar_t*);
                int n = 0;
                while (pWide != NULL && pWide[n] != 0 && (precision < 0 || n < precision)) {
                    ++n;
                }
                pad(&out, ' ', left ? 0 : width - n);
                for (int i = 0; i < n; ++i) {
                    put(&out, pWide[i] < 0x80 ? (char)pWide[i] : '?');
                }
                pad(&out, ' ', left ? width - n : 0);
            } else {
                const char* pText = va_arg(args, const char*);
                int n = 0;
                while (pText != NULL && (precision < 0 || n < precision) && pText[n] != '\0') {
                    ++n;
                }
                putField(&out, "", pText != NULL ? pText : "", n, width, left, 0);
            }
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            putFixed(&out, va_arg(args, double), precision, plus, space, width, left, zero);
            break;
        case '%':
            put(&out, '%');
            break;
        default:
            put(&out, '%');
            put(&out, c);
            break;
        }
    }
    if (length != 0) {
        pDst[out.count < length ? out.count : length - 1] = '\0';
    }
    return (int)out.count;
}

int nebula_snprintf(char* pDst, size_t length, const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    const int count = nebula_vsnprintf(pDst, length, pFormat, args);
    va_end(args);
    return count;
}

int nebula_vsprintf(char* pDst, const char* pFormat, va_list args) {
    return nebula_vsnprintf(pDst, INT_MAX, pFormat, args);
}

int nebula_sprintf(char* pDst, const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    const int count = nebula_vsnprintf(pDst, INT_MAX, pFormat, args);
    va_end(args);
    return count;
}
