/* A mod's text (UTF-8) as the game's message text (UTF-16 units), with the game's message tags written in brackets.
   The words, their groups and arguments are luma_text_tags.h's table, the one LumaScript's runtime and LumaLink's cook
   use too; "[[" is one "[", and brackets that name no tag stay as written. The UTF-8 is read by the game's one set of
   rules (nebula_utf8.h): a bad form is U+FFFD.

   Header-only, no library calls: the game module (NebulaMods) and the host's tests use it. */
#ifndef NEBULA_TEXT_TAGS_H
#define NEBULA_TEXT_TAGS_H

#include <stdint.h>

#include "luma_text_tags.h"
#include "nebula_utf8.h"

#ifdef __cplusplus

namespace nebula_text {

/* The most parameter units a tag holds: its length is a byte. */
constexpr uint32_t kTagUnitsMax = 124u;

namespace detail {

struct Writer {
    char16_t* out;
    uint32_t room;  // units out holds, with the 0
    uint32_t count;
    void put(uint32_t unit) {
        if (out != nullptr && count + 1u < room) {
            out[count] = static_cast< char16_t >(unit);
        }
        count++;
    }
};

}  // namespace detail

/* pUtf8 as the game's text into pOut (room units with the 0; cut to fit), returning the units the whole text needs
   without its 0. pOut null measures. */
inline uint32_t encode(const char* pUtf8, char16_t* pOut, uint32_t room) {
    detail::Writer writer = {pOut, room, 0};
    uint32_t length = 0;
    while (pUtf8[length] != 0) {
        length++;
    }
    for (uint32_t i = 0; i < length;) {
        if (pUtf8[i] == '[') {
            uint16_t units[3u + LUMA_TEXT_TAG_UNITS_MOST];
            size_t used = 0;
            const uint32_t count = luma_text_tag_encode(pUtf8 + i, length - i, &used, units);
            if (count != 0) {
                for (uint32_t k = 0; k < count; k++) {
                    writer.put(units[k]);
                }
                i += static_cast< uint32_t >(used);
                continue;
            }
        }
        // The game's one set of UTF-8 rules (nebula_utf8.h): a bad form is U+FFFD.
        const uint8_t* const pText = reinterpret_cast< const uint8_t* >(pUtf8);
        const uint8_t* p = pText + i;
        char16_t units[2];
        const uint32_t count = nebula_utf8::put_utf16(nebula_utf8::next(p, pText + length), units);
        i = static_cast< uint32_t >(p - pText);
        for (uint32_t k = 0; k < count; k++) {
            writer.put(units[k]);
        }
    }
    if (pOut != nullptr && room != 0) {
        pOut[writer.count < room ? writer.count : room - 1u] = 0;
    }
    return writer.count;
}

}  // namespace nebula_text

#endif  // __cplusplus

#endif  // NEBULA_TEXT_TAGS_H
