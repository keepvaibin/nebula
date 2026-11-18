#include "Game/Util/TextString.hpp"

namespace MR {
    size_t utf8ToText(char16_t* pDst, const char* pSrc) {
        const unsigned char* p = reinterpret_cast< const unsigned char* >(pSrc);
        size_t length = 0;
        const auto put = [&](unsigned int unit) {
            if (pDst != nullptr) {
                pDst[length] = static_cast< char16_t >(unit);
            }
            length++;
        };
        while (*p != 0) {
            const unsigned int lead = *p++;
            // The Encoding Standard's UTF-8 decoder: the bytes a lead takes and the range of its second byte (no
            // overlong form, no surrogate's own code, nothing past U+10FFFF).
            unsigned int more = 0;
            unsigned int low = 0x80;
            unsigned int high = 0xBF;
            unsigned int c = lead;
            if (lead >= 0xC2 && lead <= 0xDF) {
                more = 1;
                c = lead & 0x1F;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                more = 2;
                c = lead & 0x0F;
                low = lead == 0xE0 ? 0xA0 : 0x80;
                high = lead == 0xED ? 0x9F : 0xBF;
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                more = 3;
                c = lead & 0x07;
                low = lead == 0xF0 ? 0x90 : 0x80;
                high = lead == 0xF4 ? 0x8F : 0xBF;
            } else if (lead >= 0x80) {
                put(0xFFFD);
                continue;
            }
            // A sequence cut short is one U+FFFD; the byte that cut it starts the next character.
            for (; more > 0; more--) {
                if (*p < low || *p > high) {
                    c = 0xFFFD;
                    break;
                }
                c = (c << 6) | (*p++ & 0x3F);
                low = 0x80;
                high = 0xBF;
            }
            if (c >= 0x10000) {
                put(0xD800 + ((c - 0x10000) >> 10));
                put(0xDC00 + ((c - 0x10000) & 0x3FF));
            } else {
                put(c);
            }
        }
        if (pDst != nullptr) {
            pDst[length] = 0;
        }
        return length;
    }
}  // namespace MR

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
