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

    // UTF-8 as the game's text (P-160): two- and three-byte characters, one U+FFFD for a four-byte one and for a broken
    // sequence, never more units than bytes.
    {
        char16_t units[32];
        const char* const pMixed = "A\xC3\xA9\xE3\x81\x82\xF0\x9F\x98\x80Z";  // A, e acute, hiragana a, an emoji, Z
        const size_t count = MR::utf8ToText(units, pMixed);
        const char16_t want[] = {u'A', 0xE9, 0x3042, 0xFFFD, u'Z', 0};
        bool same = count == 5;
        for (size_t i = 0; same && i < 6; i++) {
            same = units[i] == want[i];
        }
        // A cut sequence (each of its bytes one mark), a letter, a stray continuation byte.
        const size_t broken = MR::utf8ToText(units, "\xE3\x81Z\x80");
        if (!same || broken != 4 || units[0] != 0xFFFD || units[1] != 0xFFFD || units[2] != u'Z' || units[3] != 0xFFFD) {
            printf("FAIL utf8ToText\n");
            sFailures++;
        }
    }

    if (sFailures == 0) {
        printf("Text string tests passed\n");
    }
    return sFailures != 0;
}
