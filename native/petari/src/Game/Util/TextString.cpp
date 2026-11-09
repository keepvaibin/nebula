#include "Game/Util/TextString.hpp"

namespace {
    struct Writer {
        char16_t* mDst;
        size_t mLength;
        size_t mCount;

        void put(char16_t c) {
            if (mCount + 1 < mLength) {
                mDst[mCount] = c;
            }
            mCount++;
        }
    };

    void pad(Writer& rOut, char16_t c, int count) {
        for (; count > 0; count--) {
            rOut.put(c);
        }
    }

    void putNumber(Writer& rOut, unsigned int value, bool minus, unsigned int base, bool upper, bool zero, bool left, int width) {
        char digits[16];  // a u32 in base 10 or 16 (the formats %d %u %x): at most 10 digits
        int count = 0;
        do {
            const unsigned int digit = value % base;
            digits[count++] = static_cast< char >(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
            value /= base;
        } while (value != 0);

        const int fill = width - count - (minus ? 1 : 0);
        if (!left && !zero) {
            pad(rOut, u' ', fill);
        }
        if (minus) {
            rOut.put(u'-');
        }
        if (!left && zero) {
            pad(rOut, u'0', fill);
        }
        while (count > 0) {
            rOut.put(static_cast< char16_t >(digits[--count]));
        }
        if (left) {
            pad(rOut, u' ', fill);
        }
    }
}  // namespace

namespace MR {
    size_t textLength(const char16_t* pText) {
        const char16_t* p = pText;
        while (*p != 0) {
            p++;
        }
        return static_cast< size_t >(p - pText);
    }

    char16_t* copyText(char16_t* pDst, const char16_t* pSrc, size_t count) {
        size_t i = 0;
        for (; i < count && pSrc[i] != 0; i++) {
            pDst[i] = pSrc[i];
        }
        for (; i < count; i++) {
            pDst[i] = 0;
        }
        return pDst;
    }

    int formatTextV(char16_t* pDst, size_t length, const char16_t* pFormat, va_list args) {
        if (length == 0) {
            return -1;
        }

        Writer out = {pDst, length, 0};
        for (const char16_t* p = pFormat; *p != 0; p++) {
            if (*p != u'%') {
                out.put(*p);
                continue;
            }

            const char16_t* const start = p++;
            bool zero = false;
            bool left = false;
            for (;; p++) {
                if (*p == u'0') {
                    zero = true;
                } else if (*p == u'-') {
                    left = true;
                } else {
                    break;
                }
            }
            int width = 0;
            while (*p >= u'0' && *p <= u'9') {
                width = width * 10 + (*p++ - u'0');
            }
            const bool longForm = *p == u'l';
            if (longForm) {
                p++;
            }

            switch (*p) {
            case u'%':
                out.put(u'%');
                break;
            case u'c': {
                const char16_t c = static_cast< char16_t >(va_arg(args, int));
                if (c != 0) {
                    out.put(c);
                }
                break;
            }
            case u'd':
            case u'i': {
                const int value = va_arg(args, int);
                const bool minus = value < 0;
                putNumber(out, minus ? 0u - static_cast< unsigned int >(value) : static_cast< unsigned int >(value), minus, 10, false, zero,
                          left, width);
                break;
            }
            case u'u':
                putNumber(out, va_arg(args, unsigned int), false, 10, false, zero, left, width);
                break;
            case u'x':
            case u'X':
                putNumber(out, va_arg(args, unsigned int), false, 16, *p == u'X', zero, left, width);
                break;
            case u's':
                if (longForm) {
                    const char16_t* pText = va_arg(args, const char16_t*);
                    const int count = pText != nullptr ? static_cast< int >(textLength(pText)) : 0;
                    if (!left) {
                        pad(out, u' ', width - count);
                    }
                    for (int i = 0; i < count; i++) {
                        out.put(pText[i]);
                    }
                    if (left) {
                        pad(out, u' ', width - count);
                    }
                } else {
                    const char* pText = va_arg(args, const char*);
                    int count = 0;
                    while (pText != nullptr && pText[count] != 0) {
                        count++;
                    }
                    if (!left) {
                        pad(out, u' ', width - count);
                    }
                    for (int i = 0; i < count; i++) {
                        out.put(static_cast< char16_t >(static_cast< unsigned char >(pText[i])));
                    }
                    if (left) {
                        pad(out, u' ', width - count);
                    }
                }
                break;
            default:
                // Not a conversion: the rest of the format as it stands ends the text.
                for (const char16_t* q = start; *q != 0; q++) {
                    out.put(*q);
                }
                for (p = start; p[1] != 0; p++) {
                }
                break;
            }
        }

        if (out.mCount < length) {
            pDst[out.mCount] = 0;
            return static_cast< int >(out.mCount);
        }

        pDst[length - 1] = 0;
        return -1;
    }

    int formatText(char16_t* pDst, size_t length, const char16_t* pFormat, ...) {
        va_list args;
        va_start(args, pFormat);
        const int count = formatTextV(pDst, length, pFormat, args);
        va_end(args);
        return count;
    }
}  // namespace MR
