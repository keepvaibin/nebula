/* A mod's text (UTF-8) as the game's message text (UTF-16 units), with the game's message tags written in brackets:

     [wait 30]       stop the text for 30 frames          (group 1, tag 0)
     [page]          end the box; the rest comes on the next press (group 1, tag 1)
     [center]        the line centred                     (group 1, tag 3)
     [sound SE_NAME] a system sound as the text reaches it (group 2, tag 0)
     [icon 7]        picture 7 of the game's picture font (buttons, stars, coins...) (group 3, tag n)
     [small] [normal] [large]   the text's size from here (group 4, tags 0, 1, 2)
     [player]        the player's name (Mario or Luigi)   (group 5, tag 0)
     [color 1]       the game's text colour 1 from here; [color 0] goes back to the text's own (group 255, tag 0)
     [tag G T u...]  any tag: group, tag and its parameter units (16-bit, decimal or 0x), as the game's text holds them

   "[[" is one "[". Anything else in brackets stays as it is written. The tags are the ones the game's own messages use
   (CustomTagProcessor, ReplaceTagProcessor); a tag no processor knows is skipped by the game.

   A tag in the game's text: 0x1A, a unit holding its length in bytes (from the 0x1A) and its group as two bytes in that
   order in memory, its tag number, then its parameters (MessageEditorMessageTag; message.rs cooks the disc's that way). Every
   platform Nebula runs on is little-endian, so the length is the unit's low byte.

   Header-only, no library calls: the game module (NebulaMods) and the host's tests use it. */
#ifndef NEBULA_TEXT_TAGS_H
#define NEBULA_TEXT_TAGS_H

#include <stdint.h>

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

inline bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

inline bool same(const char* a, uint32_t length, const char* word) {
    uint32_t i = 0;
    for (; i < length && word[i] != 0; i++) {
        const char c = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast< char >(a[i] - 'A' + 'a') : a[i];
        if (c != word[i]) {
            return false;
        }
    }
    return i == length && word[i] == 0;
}

/* A number of a tag: decimal or 0x hex, at most 0xFFFF. */
inline bool number(const char* text, uint32_t length, uint32_t* pValue) {
    if (length == 0) {
        return false;
    }
    uint32_t value = 0;
    uint32_t i = 0;
    uint32_t base = 10;
    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        i = 2;
    }
    for (; i < length; i++) {
        const char c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast< uint32_t >(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast< uint32_t >(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast< uint32_t >(c - 'A' + 10);
        } else {
            return false;
        }
        value = value * base + digit;
        if (value > 0xFFFFu) {
            return false;
        }
    }
    *pValue = value;
    return true;
}

/* A tag's words between the brackets, split at spaces: at most 2 + kTagUnitsMax. */
struct Words {
    const char* at[2u + kTagUnitsMax];
    uint32_t length[2u + kTagUnitsMax];
    uint32_t count;
};

inline bool split(const char* text, uint32_t length, Words* pWords) {
    pWords->count = 0;
    uint32_t i = 0;
    while (i < length) {
        while (i < length && isSpace(text[i])) {
            i++;
        }
        if (i == length) {
            break;
        }
        const uint32_t start = i;
        while (i < length && !isSpace(text[i])) {
            i++;
        }
        if (pWords->count == 2u + kTagUnitsMax) {
            return false;
        }
        pWords->at[pWords->count] = text + start;
        pWords->length[pWords->count] = i - start;
        pWords->count++;
    }
    return pWords->count != 0;
}

/* The tag the words name, written; false (nothing written) when they name none. */
inline bool tag(const Words& words, Writer* pWriter) {
    uint32_t group = 0;
    uint32_t id = 0;
    uint32_t units[kTagUnitsMax];
    uint32_t unitCount = 0;
    const char* name = words.at[0];
    const uint32_t nameLength = words.length[0];
    const uint32_t args = words.count - 1u;
    uint32_t value = 0;
    if (same(name, nameLength, "wait") && args == 1 && number(words.at[1], words.length[1], &value)) {
        group = 1;
        units[unitCount++] = value;
    } else if (same(name, nameLength, "page") && args == 0) {
        group = 1;
        id = 1;
    } else if (same(name, nameLength, "center") && args == 0) {
        group = 1;
        id = 3;
    } else if (same(name, nameLength, "sound") && args == 1 && words.length[1] <= kTagUnitsMax) {
        group = 2;
        for (uint32_t i = 0; i < words.length[1]; i++) {
            const uint8_t c = static_cast< uint8_t >(words.at[1][i]);
            if (c >= 0x80) {
                return false;
            }
            units[unitCount++] = c;
        }
    } else if (same(name, nameLength, "icon") && args == 1 && number(words.at[1], words.length[1], &value)) {
        group = 3;
        id = value;
    } else if (same(name, nameLength, "small") && args == 0) {
        group = 4;
    } else if (same(name, nameLength, "normal") && args == 0) {
        group = 4;
        id = 1;
    } else if (same(name, nameLength, "large") && args == 0) {
        group = 4;
        id = 2;
    } else if (same(name, nameLength, "player") && args == 0) {
        group = 5;
        units[unitCount++] = 0;
    } else if (same(name, nameLength, "color") && args == 1 && number(words.at[1], words.length[1], &value) && value <= 0xFF) {
        group = 255;
        units[unitCount++] = value << 8;  // the tag's first byte parameter (getParam8(0)): the unit's high byte
    } else if (same(name, nameLength, "tag") && args >= 2 && number(words.at[1], words.length[1], &group) && group <= 0xFF &&
               number(words.at[2], words.length[2], &id)) {
        for (uint32_t i = 3; i < words.count; i++) {
            if (!number(words.at[i], words.length[i], &units[unitCount++])) {
                return false;
            }
        }
    } else {
        return false;
    }
    pWriter->put(0x1Au);
    pWriter->put((6u + 2u * unitCount) | (group << 8));
    pWriter->put(id);
    for (uint32_t i = 0; i < unitCount; i++) {
        pWriter->put(units[i]);
    }
    return true;
}

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
            if (i + 1 < length && pUtf8[i + 1] == '[') {
                writer.put('[');
                i += 2;
                continue;
            }
            uint32_t end = i + 1;
            while (end < length && pUtf8[end] != ']' && pUtf8[end] != '[') {
                end++;
            }
            detail::Words words;
            if (end < length && pUtf8[end] == ']' && detail::split(pUtf8 + i + 1, end - i - 1, &words) && detail::tag(words, &writer)) {
                i = end + 1;
                continue;
            }
        }
        const uint8_t lead = static_cast< uint8_t >(pUtf8[i]);
        const uint32_t extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
        uint32_t code = extra == 0 ? lead : extra == 1 ? lead & 0x1Fu : extra == 2 ? lead & 0x0Fu : lead & 0x07u;
        for (uint32_t c = 1; c <= extra && i + c < length; c++) {
            code = (code << 6) | (static_cast< uint8_t >(pUtf8[i + c]) & 0x3Fu);
        }
        i += extra + 1;
        if (code >= 0x10000u) {
            code -= 0x10000u;
            writer.put(0xD800u | (code >> 10));
            writer.put(0xDC00u | (code & 0x3FFu));
        } else {
            writer.put(code);
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
