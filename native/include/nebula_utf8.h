/* UTF-8 read one code at a time, by the one set of rules every reader of the game's text keeps (ledger P-209, P-320):
   MR::utf8ToText16 (the game's own text), NebulaText's file and the mods' text tags (nebula_text_tags.h).

   A sequence cut short, a stray continuation byte, a lead byte UTF-8 has no use for (F8 and up), a longer form than
   its code needs, a code past Unicode or a surrogate's own code is read as U+FFFD. A sequence cut short takes the
   bytes up to the first that does not continue it, so the byte after it is read on its own.

   Header-only, no library calls: the game module and the host's tests use it. */
#ifndef NEBULA_UTF8_H
#define NEBULA_UTF8_H

#include <stdint.h>

#ifdef __cplusplus

namespace nebula_utf8 {

constexpr uint32_t kReplacement = 0xFFFDu;

/* The code that starts at p (p < end); p moves past the bytes it took. */
inline uint32_t next(const uint8_t*& p, const uint8_t* end) {
    const uint32_t lead = *p++;
    const int need = lead >= 0xF0u ? 3 : lead >= 0xE0u ? 2 : lead >= 0xC0u ? 1 : 0;
    uint32_t code = lead & (need == 3 ? 0x07u : need == 2 ? 0x0Fu : need == 1 ? 0x1Fu : 0x7Fu);
    int more = need;
    for (; more > 0 && p < end && (*p & 0xC0u) == 0x80u; more--) {
        code = (code << 6) | (*p++ & 0x3Fu);
    }
    // The least code each length may carry: a longer form than the code needs (C0 80 for 0) is refused.
    const uint32_t least = need == 3 ? 0x10000u : need == 2 ? 0x800u : need == 1 ? 0x80u : 0u;
    if (more != 0 || (lead >= 0x80u && lead < 0xC0u) || lead >= 0xF8u || code < least || code > 0x10FFFFu ||
        (code >= 0xD800u && code < 0xE000u)) {
        return kReplacement;
    }
    return code;
}

/* `code` as UTF-16 units into out (when not null): one, or past U+FFFF a surrogate pair. Returns how many. */
inline uint32_t put_utf16(uint32_t code, char16_t* out) {
    if (code >= 0x10000u) {
        if (out != nullptr) {
            out[0] = static_cast< char16_t >(0xD800u + ((code - 0x10000u) >> 10));
            out[1] = static_cast< char16_t >(0xDC00u + ((code - 0x10000u) & 0x3FFu));
        }
        return 2u;
    }
    if (out != nullptr) {
        out[0] = static_cast< char16_t >(code);
    }
    return 1u;
}

}  // namespace nebula_utf8

#endif  // __cplusplus

#endif  // NEBULA_UTF8_H
