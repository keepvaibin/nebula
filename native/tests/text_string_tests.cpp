// MR::formatText and the text string helpers.
#include "Game/Util/TextString.hpp"

#include <cstdio>
#include <string>

namespace {
    int sFailures;

    void expect(const char16_t* pText, const char* pWant, int count, int wantCount, const char* pWhat) {
        std::string got;
        for (const char16_t* p = pText; *p != 0; p++) {
            got.push_back(static_cast< char >(*p));
        }
        if (got != pWant || count != wantCount) {
            printf("FAIL %s: \"%s\" count %d (want \"%s\", %d)\n", pWhat, got.c_str(), count, pWant, wantCount);
            sFailures++;
        }
    }
}  // namespace

int main() {
    char16_t buffer[64];
    int n = MR::formatText(buffer, 64, u"%d/%d", 3, -12);
    expect(buffer, "3/-12", n, 5, "numbers");
    n = MR::formatText(buffer, 64, u"%03d %06d %2d", 7, 1234, 123);
    expect(buffer, "007 001234 123", n, 14, "zero fill and width");
    n = MR::formatText(buffer, 64, u"%-4d|", 3);
    expect(buffer, "3   |", n, 5, "left justification");
    n = MR::formatText(buffer, 64, u"%x/%X/%u", 0xABC, 0xABC, 42u);
    expect(buffer, "abc/ABC/42", n, 10, "hex and unsigned");
    n = MR::formatText(buffer, 64, u"%s %ls %%", "narrow", u"text");
    expect(buffer, "narrow text %", n, 13, "strings");

    // The game's own pattern (PowerStarList): the destination is also the first argument.
    n = MR::formatText(buffer, 64, u"Star");
    n = MR::formatText(buffer, 64, u"%ls%s%ls", buffer, "\n", u"Name");
    expect(buffer, "Star\nName", n, 9, "destination as an argument");

    // Too long: the first length - 1 characters, the terminator, and -1.
    n = MR::formatText(buffer, 4, u"%d", 123456);
    expect(buffer, "123", n, -1, "truncation");

    // Anything else after a %: the rest of the format ends the text.
    n = MR::formatText(buffer, 64, u"a%qb%d", 1);
    expect(buffer, "a%qb%d", n, 6, "not a conversion");
    n = MR::formatText(buffer, 64, u"100%");
    expect(buffer, "100%", n, 4, "a % at the end");

    // %c of 0 writes nothing.
    n = MR::formatText(buffer, 64, u"[%c]", 0);
    expect(buffer, "[]", n, 2, "%c 0");

    if (MR::textLength(u"abc") != 3) {
        printf("FAIL textLength\n");
        sFailures++;
    }
    MR::copyText(buffer, u"xy", 5);
    if (buffer[0] != u'x' || buffer[2] != 0 || buffer[4] != 0) {
        printf("FAIL copyText pads with zeros\n");
        sFailures++;
    }

    // UTF-8 as the game's text (P-160): two- and three-byte characters, a surrogate pair for a four-byte one, one
    // U+FFFD for each broken sequence (the Encoding Standard's decoder), never more units than bytes.
    {
        const auto same = [](const char16_t* pGot, size_t count, const char16_t* pWant, size_t wantCount) {
            bool ok = count == wantCount && pGot[count] == 0;
            for (size_t i = 0; ok && i < count; i++) {
                ok = pGot[i] == pWant[i];
            }
            return ok;
        };
        char16_t units[32];
        // A, e acute, hiragana a, an emoji, Z.
        size_t count = MR::utf8ToText(units, "A\xC3\xA9\xE3\x81\x82\xF0\x9F\x98\x80Z");
        const char16_t mixed[] = {u'A', 0xE9, 0x3042, 0xD83D, 0xDE00, u'Z'};
        bool ok = same(units, count, mixed, 6) && MR::utf8ToText(nullptr, "A\xC3\xA9\xE3\x81\x82\xF0\x9F\x98\x80Z") == 6;
        // A cut sequence, a letter, a stray continuation byte.
        count = MR::utf8ToText(units, "\xE3\x81Z\x80");
        const char16_t cut[] = {0xFFFD, u'Z', 0xFFFD};
        ok = ok && same(units, count, cut, 3);
        // An overlong slash, a surrogate's own code, past U+10FFFF, a lead that cannot start: one mark per byte, as
        // their second bytes are out of range.
        count = MR::utf8ToText(units, "\xC0\xAF\xED\xA0\x80\xF4\x90\x80\x80\xFF");
        const char16_t bad[] = {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD};
        ok = ok && same(units, count, bad, 10);
        // The last characters of each range.
        count = MR::utf8ToText(units, "\xDF\xBF\xEF\xBF\xBF\xF4\x8F\xBF\xBF");
        const char16_t ends[] = {0x7FF, 0xFFFF, 0xDBFF, 0xDFFF};
        ok = ok && same(units, count, ends, 4);
        if (!ok) {
            printf("FAIL utf8ToText\n");
            sFailures++;
        }
    }

    if (sFailures == 0) {
        printf("Text string tests passed\n");
    }
    return sFailures != 0;
}
