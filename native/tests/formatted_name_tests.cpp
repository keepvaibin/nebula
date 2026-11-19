// MR::FormattedName, MR::Text16 and the name helpers built on them: names far longer than their inline room; and
// MR::GrowBuffer, the room of the achievement pop-up queue, past the 40 entries its old fixed array held; and
// MR::utf8ToText16 and nebula_utf8.h, the one set of UTF-8 rules of the game's 16-bit text.
// StringUtil.cpp is linked with the few game functions it calls given below.
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/DueRing.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/TextString.hpp"
#include <nebula_utf8.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

NebulaArena* NebulaArena::sRoot = nullptr;

NebulaArena* NebulaArena::findFromRoot(void*) {
    return nullptr;
}

// The global new[], so the delete[] the code under test uses matches it (the address sanitizer checks the pair).
void* operator new[](size_t size, NebulaArena*, int) {
    return ::operator new[](size);
}

void* NebulaArena::alloc(u32 size, int, NebulaArena*) {
    return ::operator new[](size);
}

// StringUtil.cpp's time strings fill a message's tags; no test here reaches them.
namespace ReplaceTagFunction {
    u32 ReplaceArgs(char16_t*, s32, const char16_t*, u32, ...) {
        NEBULA_PANIC("the test reached the message system (ReplaceArgs), which it leaves out");
    }
}  // namespace ReplaceTagFunction

namespace MR {
    const char16_t* getGameMessageDirect(const char*) {
        return u"";
    }

    u32 getRaceBestTime(int) {
        return 0;
    }

    u32 getRaceCurrentTime() {
        return 0;
    }
}  // namespace MR

namespace {
    int sFailures;

    void check(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    bool same(const MR::FormattedName& rName, const std::string& rWant) {
        return rWant == rName.c_str();
    }

    const char16_t* formatText16(MR::Text16& rDst, const char16_t* pFormat, ...) {
        va_list args;
        va_start(args, pFormat);
        const char16_t* const pText = rDst.format(pFormat, args);
        va_end(args);
        return pText;
    }
}  // namespace

int main() {
    // A short name stays in itself.
    {
        MR::FormattedName name("%s/%d", "abc", 5);
        check(same(name, "abc/5"), "short name");
    }

    // The inline room is 256 bytes with the terminator: lengths around it, and far past it.
    for (size_t length : {0u, 1u, 254u, 255u, 256u, 257u, 600u, 5000u, 100000u}) {
        const std::string text(length, 'x');
        MR::FormattedName name("%s", text.c_str());
        check(strlen(name.c_str()) == length && same(name, text), "name of the exact length");
    }

    // Text built from a long part and fixed parts, as the game's object names are.
    {
        const std::string place(1000, 'g');
        MR::FormattedName name("%s_%d", place.c_str(), 7);
        check(strlen(name.c_str()) == 1002 && same(name, place + "_7"), "long name with a number");
    }

    // Formatting again: short after long, long after short, long after long.
    {
        const std::string big(700, 'b');
        MR::FormattedName name("%s", big.c_str());
        name.format("ok");
        check(same(name, "ok"), "short after long");
        name.format("%s!", big.c_str());
        check(same(name, big + "!"), "long after short");
        name.format("%s?", big.c_str());
        check(same(name, big + "?"), "long after long");
    }

    // room(): storage of any size for a caller that fills the text itself.
    {
        MR::FormattedName name;
        check(same(name, ""), "a new name is empty");
        char* const pSmall = name.room(10);
        strcpy(pSmall, "small");
        check(same(name, "small"), "room inline");
        char* const pBig = name.room(1000);
        check(pBig[0] == '\0', "room starts empty");
        memset(pBig, 'r', 999);
        pBig[999] = '\0';
        check(strlen(name.c_str()) == 999, "room of 1000 bytes");
    }

    // addFilePrefix and removeExtensionString with names past the inline room.
    {
        const std::string parent(600, 'd');
        MR::FormattedName path;
        MR::addFilePrefix(path, "/child.arc", parent.c_str());
        check(same(path, parent + "/child.arc"), "addFilePrefix with a long parent");

        MR::FormattedName stem;
        MR::removeExtensionString(stem, (parent + "/child.arc").c_str());
        check(same(stem, parent + "/child"), "removeExtensionString on a long path");
        MR::removeExtensionString(stem, (parent + ".dir/child").c_str());
        check(same(stem, parent + ".dir/child"), "removeExtensionString: a dot before the last slash");
    }

    // internName: one pointer for one text, whatever its length; null is the empty name.
    {
        const std::string longName(900, 'n');
        const char* const pFirst = MR::internName(longName.c_str());
        const char* const pSecond = MR::internName(std::string(longName).c_str());
        check(pFirst == pSecond && longName == pFirst, "internName of a long name");
        check(MR::internName((longName + "2").c_str()) != pFirst, "internName tells names apart");
        check(strcmp(MR::internName(nullptr), "") == 0, "internName of null");
    }

    // Text16: formatted text of any length, and room that grows with the first units kept.
    {
        MR::Text16 text;
        check(text.capacity() == 128, "Text16 starts with 128 units");
        std::u16string big(1000, u'q');
        const char16_t* const pText = formatText16(text, u"[%ls]", big.c_str());
        check(MR::textLength(pText) == 1002 && pText[0] == u'[' && pText[1001] == u']', "Text16 format of a long text");
        check(text.capacity() >= 1003, "Text16 grew to fit");

        MR::Text16 grown;
        char16_t* pUnits = grown.room(8);
        for (int i = 0; i < 7; i++) {
            pUnits[i] = static_cast< char16_t >(u'a' + i);
        }
        pUnits[7] = u'\0';
        pUnits = grown.grow(8, 5000);
        check(grown.capacity() >= 5000 && pUnits[0] == u'a' && pUnits[6] == u'g' && pUnits[7] == u'\0', "Text16 grow keeps the text");
    }

    // GrowBuffer: a queue of 300 waiting entries (a static starts zeroed) keeps every entry across its growths, and
    // takes entries off the front as the pop-up does.
    {
        struct Waiting {
            const void* mEntry;
            s32 mBatch;
            const void* mIcon;
        };
        static MR::GrowBuffer< Waiting > sQueue;
        s32 queued = 0;
        for (s32 i = 0; i < 300; i++) {
            Waiting* const pQueue = sQueue.room(static_cast< u32 >(queued) + 1);
            pQueue[queued].mEntry = nullptr;
            pQueue[queued].mBatch = i;
            pQueue[queued].mIcon = nullptr;
            queued++;
        }
        bool intact = sQueue.mRoom >= 300;
        for (s32 i = 0; i < queued; i++) {
            intact = intact && sQueue[i].mBatch == i;
        }
        check(intact, "GrowBuffer keeps 300 entries across its growths");
        for (s32 i = 1; i < queued; i++) {
            sQueue[i - 1] = sQueue[i];
        }
        queued--;
        check(queued == 299 && sQueue[0].mBatch == 1 && sQueue[298].mBatch == 299, "taking the first entry shifts the rest");
    }

    // DueRing (the star piece get sounds, once 16 slots): 100 steps waiting at once come off in order; a ring that
    // wrapped and then fills grows without losing the order; a step is due once the clock reaches it.
    {
        static MR::DueRing sRing;
        check(!sRing.isDue(1000) && sRing.count() == 0, "an empty ring has nothing due");
        for (u32 i = 0; i < 100; i++) {
            sRing.push(i + 16);
        }
        check(sRing.count() == 100 && sRing.mItems.mRoom >= 100, "100 waiting steps are all kept");
        check(!sRing.isDue(15) && sRing.isDue(16), "the first step is due when the clock reaches it");
        bool inOrder = true;
        for (u32 i = 0; i < 100; i++) {
            inOrder = inOrder && sRing.isDue(i + 16) && (i == 0 || !sRing.isDue(i + 15));
            sRing.pop();
        }
        check(inOrder && sRing.count() == 0, "100 steps come off in order");

        // The head in the middle of the room, then more than the room holds: the part that wrapped keeps its place.
        sRing.clear();
        for (u32 i = 0; i < 10; i++) {
            sRing.push(i);
        }
        for (u32 i = 0; i < 7; i++) {
            sRing.pop();
        }
        for (u32 i = 10; i < 70; i++) {
            sRing.push(i);
        }
        bool wrapped = sRing.count() == 63;
        for (u32 i = 7; i < 70; i++) {
            wrapped = wrapped && sRing.isDue(i) && (i == 7 || !sRing.isDue(i - 1));
            sRing.pop();
        }
        check(wrapped && sRing.count() == 0, "a wrapped ring that grows keeps its order");
    }

    // MR::utf8ToText16 (P-209): every length of sequence, a pair past U+FFFF, and each bad form as U+FFFD.
    {
        struct Case {
            const char* mUtf8;
            std::u16string mWant;
            const char* mWhat;
        };
        const Case cases[] = {
            {"", u"", "empty text"},
            {"Az", u"Az", "one-byte sequences"},
            {"\xC3\xA9", u"é", "a two-byte sequence"},
            {"\xE3\x81\x82", u"あ", "a three-byte sequence"},
            {"\xF0\x9F\x98\x80!", u"\xD83D\xDE00!", "a four-byte sequence becomes a surrogate pair"},
            {"\xF4\x8F\xBF\xBF", u"\xDBFF\xDFFF", "the last code of Unicode"},
            {"\x80" "A", u"\xFFFD" "A", "a stray continuation byte"},
            {"\xE3\x81", u"\xFFFD", "a sequence cut short by the end"},
            {"\xE3" "A", u"\xFFFD" "A", "a sequence cut short by a lead byte"},
            {"\xED\xA0\x80", u"\xFFFD", "a surrogate's own code"},
            {"\xF4\x90\x80\x80", u"\xFFFD", "a code past Unicode"},
            {"\xC2\x80\xE0\xA0\x80", u"\u0080ࠀ", "the least code of two and three bytes"},
            {"\xC0\x80" "A", u"\xFFFD" "A", "a two-byte form of 0"},
            {"\xE0\x80\xAF", u"\xFFFD", "a three-byte form of a one-byte code"},
            {"\xF0\x8F\xBF\xBF", u"\xFFFD", "a four-byte form of a three-byte code"},
            // The lead takes three continuation bytes as a four-byte one does; the fifth is then a stray byte.
            {"\xF8\x88\x80\x80\x80", u"\xFFFD\xFFFD", "a five-byte lead"},
        };
        for (const Case& rCase : cases) {
            const u32 units = MR::utf8ToText16(nullptr, rCase.mUtf8);
            std::u16string out(units, u'?');
            const u32 written = MR::utf8ToText16(&out[0], rCase.mUtf8);
            check(units == rCase.mWant.size() + 1 && written == units && out.back() == u'\0' &&
                      out.substr(0, units - 1) == rCase.mWant,
                  rCase.mWhat);
        }

        // Past the inline room of 128 units, whole.
        std::string longText;
        for (u32 i = 0; i < 300; i++) {
            longText += "\xE3\x81\x82";
        }
        MR::Text16 text;
        const char16_t* const pText = MR::setTextFromUtf8(text, longText.c_str());
        bool whole = pText == text.c_str() && pText[300] == u'\0';
        for (u32 i = 0; i < 300; i++) {
            whole = whole && pText[i] == u'あ';
        }
        check(whole, "a UTF-8 text longer than Text16's inline room");

        // nebula_utf8::next within a bounded text (NebulaText's lines, the mods' text, P-320): the bound ends a
        // sequence as the text's end does, and a whole sequence before it reads as it is.
        const u8 bytes[] = {0xE3, 0x81, 0x82, 0xE3, 0x81, 0x82};
        const u8* p = bytes;
        const u32 first = nebula_utf8::next(p, bytes + 5);
        const u32 cut = nebula_utf8::next(p, bytes + 5);
        check(first == 0x3042u && cut == nebula_utf8::kReplacement && p == bytes + 5,
              "a sequence cut short by the text's bound, not read past it");
        char16_t pair[2] = {};
        check(nebula_utf8::put_utf16(0x1F600u, pair) == 2u && pair[0] == 0xD83Du && pair[1] == 0xDE00u &&
                  nebula_utf8::put_utf16(0x41u, nullptr) == 1u,
              "a code as UTF-16 units");
    }

    if (sFailures != 0) {
        printf("%d formatted name test failures\n", sFailures);
        return 1;
    }
    printf("formatted name tests passed\n");
    return 0;
}
