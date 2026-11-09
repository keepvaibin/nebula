// MR::FormattedName, MR::Text16 and the name helpers built on them: names far longer than their inline room; and
// MR::GrowBuffer, the room of the achievement pop-up queue, past the 40 entries its old fixed array held.
// StringUtil.cpp is linked with the few game functions it calls given below.
#include "Game/Util/Array.hpp"
#include "Game/Util/DueRing.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/TextString.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

NebulaArena* NebulaArena::sRoot = nullptr;

NebulaArena* NebulaArena::findFromRoot(void*) {
    return nullptr;
}

void* operator new[](size_t size, NebulaArena*, int) {
    return malloc(size);
}

void* NebulaArena::alloc(u32 size, int, NebulaArena*) {
    return malloc(size);
}

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

    if (sFailures != 0) {
        printf("%d formatted name test failures\n", sFailures);
        return 1;
    }
    printf("formatted name tests passed\n");
    return 0;
}
