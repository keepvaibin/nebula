#include "nebula_format.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* The format again with each null string's conversion given a precision of 0; false if it has none. */
static int rewriteNullStrings(char* pOut, size_t outLength, const char* pFormat, va_list args) {
    int found = 0;
    size_t used = 0;
    for (const char* p = pFormat; *p != '\0'; p++) {
        if (used + 8 >= outLength) {
            return 0;
        }
        pOut[used++] = *p;
        if (*p != '%') {
            continue;
        }

        p++;
        while (*p != '\0' && strchr("-+ #0", *p) != NULL) {
            pOut[used++] = *p++;
        }
        while ((*p >= '0' && *p <= '9') || *p == '*') {
            if (*p == '*') {
                (void)va_arg(args, int);
            }
            pOut[used++] = *p++;
        }
        const char* const precision = p;
        if (*p == '.') {
            p++;
            while ((*p >= '0' && *p <= '9') || *p == '*') {
                if (*p == '*') {
                    (void)va_arg(args, int);
                }
                p++;
            }
        }
        const char* const lengthStart = p;
        while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't' || *p == 'L') {
            p++;
        }
        const int longs = (p - lengthStart >= 1 && lengthStart[0] == 'l') + (p - lengthStart >= 2 && lengthStart[1] == 'l');
        const int wide = longs == 2 || (p != lengthStart && (*lengthStart == 'z' || *lengthStart == 'j' || *lengthStart == 't'));

        int nullString = 0;
        switch (*p) {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            if (wide) {
                (void)va_arg(args, long long);
            } else if (longs == 1) {
                (void)va_arg(args, long);
            } else {
                (void)va_arg(args, int);
            }
            break;
        case 'c':
            (void)va_arg(args, int);
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            if (p != lengthStart && *lengthStart == 'L') {
                (void)va_arg(args, long double);
            } else {
                (void)va_arg(args, double);
            }
            break;
        case 's':
            nullString = va_arg(args, const char*) == NULL;
            break;
        case 'p':
        case 'n':
            (void)va_arg(args, void*);
            break;
        case '%':
            break;
        default:
            return 0;
        }

        if (nullString) {
            found = 1;
            pOut[used++] = '.';
            pOut[used++] = '0';
        } else {
            const size_t rest = (size_t)(p - precision);
            if (used + rest + 8 >= outLength) {
                return 0;
            }
            memcpy(pOut + used, precision, rest);
            used += rest;
        }
        pOut[used++] = *p;
    }
    pOut[used] = '\0';
    return found;
}

int nebula_vsnprintf(char* pDst, size_t length, const char* pFormat, va_list args) {
    /* Few strings are null, and few formats are long. */
    char rewritten[512];
    va_list scan;
    va_copy(scan, args);
    const int found = rewriteNullStrings(rewritten, sizeof(rewritten), pFormat, scan);
    va_end(scan);
    return vsnprintf(pDst, length, found ? rewritten : pFormat, args);
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
