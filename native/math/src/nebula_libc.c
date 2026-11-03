/* The module's memory and string functions, from no system library. Built with -fno-builtin: a loop here must not
   become a call to itself. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef uint64_t __attribute__((may_alias, aligned(1))) Word;

void* memcpy(void* pDst, const void* pSrc, size_t size) {
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    while (size >= 8) {
        *(Word*)d = *(const Word*)s;
        d += 8;
        s += 8;
        size -= 8;
    }
    while (size != 0) {
        *d++ = *s++;
        --size;
    }
    return pDst;
}

void* memmove(void* pDst, const void* pSrc, size_t size) {
    unsigned char* d = (unsigned char*)pDst;
    const unsigned char* s = (const unsigned char*)pSrc;
    if ((uintptr_t)d - (uintptr_t)s >= size) {
        return memcpy(pDst, pSrc, size);
    }
    d += size;
    s += size;
    while (size >= 8) {
        d -= 8;
        s -= 8;
        *(Word*)d = *(const Word*)s;
        size -= 8;
    }
    while (size != 0) {
        *--d = *--s;
        --size;
    }
    return pDst;
}

void* memset(void* pDst, int value, size_t size) {
    unsigned char* d = (unsigned char*)pDst;
    const uint64_t word = (uint64_t)(unsigned char)value * 0x0101010101010101ull;
    while (size >= 8) {
        *(Word*)d = word;
        d += 8;
        size -= 8;
    }
    while (size != 0) {
        *d++ = (unsigned char)value;
        --size;
    }
    return pDst;
}

int memcmp(const void* pA, const void* pB, size_t size) {
    const unsigned char* a = (const unsigned char*)pA;
    const unsigned char* b = (const unsigned char*)pB;
    for (; size != 0; --size, ++a, ++b) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
    }
    return 0;
}

size_t strlen(const char* p) {
    const char* s = p;
    while (*s != '\0') {
        ++s;
    }
    return (size_t)(s - p);
}

size_t wcslen(const wchar_t* p) {
    const wchar_t* s = p;
    while (*s != 0) {
        ++s;
    }
    return (size_t)(s - p);
}

int strcmp(const char* pA, const char* pB) {
    while (*pA != '\0' && *pA == *pB) {
        ++pA;
        ++pB;
    }
    return (int)(unsigned char)*pA - (int)(unsigned char)*pB;
}

int strncmp(const char* pA, const char* pB, size_t size) {
    for (; size != 0; --size, ++pA, ++pB) {
        if (*pA != *pB || *pA == '\0') {
            return (int)(unsigned char)*pA - (int)(unsigned char)*pB;
        }
    }
    return 0;
}

char* strcpy(char* pDst, const char* pSrc) {
    char* d = pDst;
    while ((*d++ = *pSrc++) != '\0') {
    }
    return pDst;
}

char* strncpy(char* pDst, const char* pSrc, size_t size) {
    size_t i = 0;
    for (; i < size && pSrc[i] != '\0'; ++i) {
        pDst[i] = pSrc[i];
    }
    for (; i < size; ++i) {
        pDst[i] = '\0';
    }
    return pDst;
}

char* strcat(char* pDst, const char* pSrc) {
    strcpy(pDst + strlen(pDst), pSrc);
    return pDst;
}

char* strchr(const char* p, int c) {
    for (;; ++p) {
        if (*p == (char)c) {
            return (char*)p;
        }
        if (*p == '\0') {
            return NULL;
        }
    }
}

char* strrchr(const char* p, int c) {
    const char* found = NULL;
    for (;; ++p) {
        if (*p == (char)c) {
            found = p;
        }
        if (*p == '\0') {
            return (char*)found;
        }
    }
}

char* strstr(const char* p, const char* pNeedle) {
    const size_t n = strlen(pNeedle);
    if (n == 0) {
        return (char*)p;
    }
    for (; *p != '\0'; ++p) {
        if (*p == *pNeedle && strncmp(p, pNeedle, n) == 0) {
            return (char*)p;
        }
    }
    return NULL;
}

static int digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;
}

/* The digits after any spaces and sign, base 0 by their prefix; saturates at the type's range through `limit`. */
static unsigned long long scanInteger(const char* p, char** ppEnd, int base, int* pNegative, unsigned long long limit) {
    const char* const start = p;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
        ++p;
    }
    *pNegative = 0;
    if (*p == '-' || *p == '+') {
        *pNegative = *p == '-';
        ++p;
    }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digitValue(p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    unsigned long long value = 0;
    int any = 0;
    for (;; ++p) {
        const int d = digitValue(*p);
        if (d >= base) {
            break;
        }
        any = 1;
        value = value > (limit - (unsigned long long)d) / (unsigned long long)base ? limit : value * (unsigned long long)base + (unsigned long long)d;
    }
    if (ppEnd != NULL) {
        *ppEnd = (char*)(any ? p : start);
    }
    return value;
}

long strtol(const char* p, char** ppEnd, int base) {
    int negative;
    const unsigned long long v = scanInteger(p, ppEnd, base, &negative, (unsigned long long)LONG_MAX + 1u);
    if (!negative && v > (unsigned long long)LONG_MAX) {
        return LONG_MAX;
    }
    return negative ? (long)(0ull - v) : (long)v;
}

unsigned long strtoul(const char* p, char** ppEnd, int base) {
    int negative;
    const unsigned long long v = scanInteger(p, ppEnd, base, &negative, ULONG_MAX);
    return negative ? (unsigned long)(0ull - v) : (unsigned long)v;
}

int atoi(const char* p) {
    return (int)strtol(p, NULL, 10);
}
