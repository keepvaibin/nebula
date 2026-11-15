/* The game's message tags as words in brackets: the one table every reader and writer of mod text uses. The game
   module's mod API (nebula_text_tags.h), the LumaScript runtime (lumart) and the docs read this table; LumaLink's
   cook (nebula-import cook/message_merge.rs) is tested against it, word by word.

   A tag in the game's text: 0x1A, a unit holding its length in bytes (from the 0x1A) in the low byte and its group in
   the high byte, its tag number, then its parameter units (MessageEditorMessageTag). The groups are the ones the game's
   text drawing knows (CustomTagProcessor, ReplaceTagProcessor); a tag of a group no processor knows is skipped.

   "[[" is one "[". `[tag G T u...]` writes any tag: group, tag number and its parameter units, each decimal or 0x hex.
   Brackets that name no tag stay as written. Header-only C, no library calls. */
#ifndef LUMA_TEXT_TAGS_H
#define LUMA_TEXT_TAGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What follows a tag's word, and what it becomes. */
enum LumaTextArg {
    LUMA_TEXT_ARG_NONE = 0,  /* no word, no parameter */
    LUMA_TEXT_ARG_UNIT = 1,  /* a number: one parameter unit */
    LUMA_TEXT_ARG_TAG = 2,   /* a number: the tag number itself */
    LUMA_TEXT_ARG_NAME = 3,  /* a name of printable ASCII, no brackets: one unit a character */
    LUMA_TEXT_ARG_HIGH = 4,  /* a number: one unit, the value in its high byte (the tag's first byte parameter) */
    LUMA_TEXT_ARG_ZERO = 5,  /* no word; one parameter unit 0 */
    LUMA_TEXT_ARG_INDEX = 6  /* two numbers: the tag number, then which of the game's arguments (units 0, 0, 0, n) */
};

typedef struct LumaTextTag {
    const char* word;   /* lowercase; matched without case */
    uint8_t group;
    uint16_t tag;       /* unused for LUMA_TEXT_ARG_TAG */
    uint8_t arg;        /* LumaTextArg */
    uint16_t most;      /* the largest number the argument takes */
    const char* means;  /* one line for the docs */
} LumaTextTag;

/* The most parameter units a tag holds: its length is a byte. */
#define LUMA_TEXT_TAG_UNITS_MOST 124u

static const LumaTextTag luma_text_tags[] = {
    {"wait", 1, 0, LUMA_TEXT_ARG_UNIT, 0xFFFF, "stops the text for n frames"},
    {"page", 1, 1, LUMA_TEXT_ARG_NONE, 0, "ends the box; the rest comes on the next press"},
    {"middle", 1, 2, LUMA_TEXT_ARG_NONE, 0, "centres the text in the box from top to bottom"},
    {"center", 1, 3, LUMA_TEXT_ARG_NONE, 0, "centres the line"},
    {"sound", 2, 0, LUMA_TEXT_ARG_NAME, LUMA_TEXT_TAG_UNITS_MOST, "plays the system sound NAME when the text reaches it"},
    {"icon", 3, 0, LUMA_TEXT_ARG_TAG, 0xFFFF, "picture n of the game's picture font (buttons, stars, coins)"},
    {"small", 4, 0, LUMA_TEXT_ARG_NONE, 0, "text three quarters of its size from here"},
    {"normal", 4, 1, LUMA_TEXT_ARG_NONE, 0, "text at its own size from here"},
    {"large", 4, 2, LUMA_TEXT_ARG_NONE, 0, "text one and a half times its size from here"},
    {"player", 5, 0, LUMA_TEXT_ARG_ZERO, 0, "the player's name (Mario or Luigi)"},
    {"player_name", 5, 0, LUMA_TEXT_ARG_HIGH, 0xFF, "the player's name message n (System_PlayerName0n, 1n for Luigi)"},
    {"number", 6, 0, LUMA_TEXT_ARG_INDEX, 0xFFFF, "the game's number argument i; tag t 5 to 9 pads it to 2 to 6 digits"},
    {"string", 7, 0, LUMA_TEXT_ARG_INDEX, 0xFFFF, "the game's text argument i"},
    {"race_time", 9, 5, LUMA_TEXT_ARG_NONE, 0, "the time of the race being run"},
    {"best_time", 9, 0, LUMA_TEXT_ARG_TAG, 0xFFFF, "the best time of race n"},
    {"font", 10, 0, LUMA_TEXT_ARG_NAME, LUMA_TEXT_TAG_UNITS_MOST, "the text in the game's font NAME from here"},
    {"color", 255, 0, LUMA_TEXT_ARG_HIGH, 0xFF, "the game's text colour n from here; 0 goes back to the text's own"},
};

#define LUMA_TEXT_TAG_COUNT (sizeof(luma_text_tags) / sizeof(luma_text_tags[0]))

/* A number word: decimal, or 0x and hex digits; at most 0xFFFF. */
static inline int luma_text_number(const char* w, size_t n, uint32_t* v) {
    uint32_t value = 0, base = 10;
    size_t i = 0;
    if (n > 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i >= n) return 0;
    for (; i < n; i++) {
        const char c = w[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
        value = value * base + d;
        if (value > 0xFFFFu) return 0;
    }
    *v = value;
    return 1;
}

static inline int luma_text_same(const char* w, size_t n, const char* word) {
    size_t i = 0;
    for (; i < n && word[i] != 0; i++) {
        const char c = (w[i] >= 'A' && w[i] <= 'Z') ? (char)(w[i] - 'A' + 'a') : w[i];
        if (c != word[i]) return 0;
    }
    return i == n && word[i] == 0;
}

static inline int luma_text_name_char(uint32_t c) {
    return c >= 0x21 && c < 0x7F && c != '[' && c != ']';
}

/* The bracket words at s (s[0] is '[', n bytes from there) as the game's units in out (room for
   3 + LUMA_TEXT_TAG_UNITS_MOST): "[[" is one '['. Returns the units written and the bytes read in *used; 0 when the
   brackets name no tag. Never more units than bytes read. */
static inline uint32_t luma_text_tag_encode(const char* s, size_t n, size_t* used, uint16_t* out) {
    const char* word[2 + LUMA_TEXT_TAG_UNITS_MOST];
    size_t length[2 + LUMA_TEXT_TAG_UNITS_MOST];
    uint32_t words = 0, group = 0, id = 0, v = 0, count = 0;
    size_t end = 1, i;
    if (n >= 2 && s[1] == '[') {
        out[0] = '[';
        *used = 2;
        return 1;
    }
    while (end < n && s[end] != '[' && s[end] != ']') end++;
    if (end >= n || s[end] != ']') return 0;
    for (i = 1; i < end;) {
        size_t start;
        while (i < end && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= end) break;
        start = i;
        while (i < end && s[i] != ' ' && s[i] != '\t') i++;
        if (words == 2 + LUMA_TEXT_TAG_UNITS_MOST) return 0;
        word[words] = s + start;
        length[words] = i - start;
        words++;
    }
    if (words == 0) return 0;
    if (luma_text_same(word[0], length[0], "tag")) {
        uint32_t k;
        if (words < 3 || words - 3 > LUMA_TEXT_TAG_UNITS_MOST - 1 || !luma_text_number(word[1], length[1], &group) ||
            group > 0xFF || !luma_text_number(word[2], length[2], &id)) {
            return 0;
        }
        for (k = 3; k < words; k++) {
            if (!luma_text_number(word[k], length[k], &v)) return 0;
            out[3 + count++] = (uint16_t)v;
        }
    } else {
        const LumaTextTag* t = 0;
        size_t r;
        for (r = 0; r < LUMA_TEXT_TAG_COUNT; r++) {
            if (luma_text_same(word[0], length[0], luma_text_tags[r].word)) t = &luma_text_tags[r];
        }
        if (t == 0) return 0;
        group = t->group;
        id = t->tag;
        switch (t->arg) {
        case LUMA_TEXT_ARG_NONE:
            if (words != 1) return 0;
            break;
        case LUMA_TEXT_ARG_ZERO:
            if (words != 1) return 0;
            out[3 + count++] = 0;
            break;
        case LUMA_TEXT_ARG_NAME: {
            size_t k;
            if (words != 2 || length[1] > t->most) return 0;
            for (k = 0; k < length[1]; k++) {
                if (!luma_text_name_char((uint8_t)word[1][k])) return 0;
                out[3 + count++] = (uint8_t)word[1][k];
            }
            break;
        }
        case LUMA_TEXT_ARG_INDEX:
            if (words != 3 || !luma_text_number(word[1], length[1], &id) || !luma_text_number(word[2], length[2], &v) ||
                v > t->most) {
                return 0;
            }
            out[3] = 0, out[4] = 0, out[5] = 0, out[6] = (uint16_t)v;
            count = 4;
            break;
        default:
            if (words != 2 || !luma_text_number(word[1], length[1], &v) || v > t->most) return 0;
            if (t->arg == LUMA_TEXT_ARG_TAG) {
                id = v;
            } else {
                out[3 + count++] = (uint16_t)(t->arg == LUMA_TEXT_ARG_HIGH ? v << 8 : v);
            }
        }
    }
    out[0] = 0x1A;
    out[1] = (uint16_t)((6u + 2u * count) | (group << 8));
    out[2] = (uint16_t)id;
    *used = end + 1;
    return 3 + count;
}

/* The table's row whose tag (group, tag number, parameter units) this is, or null: then the tag is written
   `[tag G T u...]`. A row of LUMA_TEXT_ARG_TAG takes any tag number its row of the same group does not name. */
static inline const LumaTextTag* luma_text_tag_row(uint32_t group, uint32_t id, const uint16_t* p, uint32_t n) {
    const LumaTextTag* by_tag = 0;
    size_t r;
    uint32_t k;
    for (r = 0; r < LUMA_TEXT_TAG_COUNT; r++) {
        const LumaTextTag* t = &luma_text_tags[r];
        if (t->group != group) continue;
        switch (t->arg) {
        case LUMA_TEXT_ARG_NONE:
            if (t->tag == id && n == 0) return t;
            break;
        case LUMA_TEXT_ARG_ZERO:
            if (t->tag == id && n == 1 && p[0] == 0) return t;
            break;
        case LUMA_TEXT_ARG_UNIT:
            if (t->tag == id && n == 1 && p[0] <= t->most) return t;
            break;
        case LUMA_TEXT_ARG_HIGH:
            if (t->tag == id && n == 1 && (p[0] & 0xFF) == 0 && (p[0] >> 8) <= t->most) return t;
            break;
        case LUMA_TEXT_ARG_NAME:
            if (t->tag == id && n != 0 && n <= t->most) {
                for (k = 0; k < n && luma_text_name_char(p[k]); k++) {
                }
                if (k == n) return t;
            }
            break;
        case LUMA_TEXT_ARG_TAG:
            if (n == 0 && id <= t->most) by_tag = t;
            break;
        case LUMA_TEXT_ARG_INDEX:
            if (n == 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] <= t->most) return t;
            break;
        }
    }
    return by_tag;
}

#ifdef __cplusplus
}
#endif

#endif /* LUMA_TEXT_TAGS_H */
