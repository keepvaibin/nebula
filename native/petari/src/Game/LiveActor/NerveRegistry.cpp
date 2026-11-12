#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NebulaArena.hpp"

#include <cstring>

// Every nerve's record (NEW_NERVE), for mods: a name per nerve, and the nerves plugins hook. The records lie between two
// markers: on Windows "nebnrv$a" < "nebnrv$m" < "nebnrv$z", with zeros the linker may
// pad between object files (a record whose name is null); on ELF the linker names the section's ends.
namespace {
#if defined(_WIN32)
#pragma section("nebnrv$a", read)
#pragma section("nebnrv$z", read)
    __declspec(allocate("nebnrv$a")) NEBULA_NERVE_KEEP const nebula::NerveRecord sFirst = {};
    __declspec(allocate("nebnrv$z")) NEBULA_NERVE_KEEP const nebula::NerveRecord sLast = {};
    const nebula::NerveRecord* hidden(const nebula::NerveRecord* pRecord) {
        __asm__("" : "+r"(pRecord));
        return pRecord;
    }
    const nebula::NerveRecord* begin() {
        return hidden(&sFirst) + 1;
    }
    const nebula::NerveRecord* end() {
        return hidden(&sLast);
    }
#elif defined(__APPLE__)
}  // namespace
extern "C" {
    extern const nebula::NerveRecord __start_nebnrv[] __asm__("section$start$__DATA$nebnrv");
    extern const nebula::NerveRecord __stop_nebnrv[] __asm__("section$end$__DATA$nebnrv");
}
namespace {
    const nebula::NerveRecord* begin() {
        return __start_nebnrv;
    }
    const nebula::NerveRecord* end() {
        return __stop_nebnrv;
    }
#else
}  // namespace
extern "C" {
    extern const nebula::NerveRecord __start_nebnrv[] __attribute__((visibility("hidden")));
    extern const nebula::NerveRecord __stop_nebnrv[] __attribute__((visibility("hidden")));
}
namespace {
    const nebula::NerveRecord* begin() {
        return __start_nebnrv;
    }
    const nebula::NerveRecord* end() {
        return __stop_nebnrv;
    }
#endif

    // One record per nerve, and its name, built once (indexNerves) in the root arena: they last the run.
    const nebula::NerveRecord** sRecords = nullptr;
    const char** sKeys = nullptr;
    s32 sCount = 0;
    bool sIndexed = false;

    constexpr u16 kCounting = 0xFFFFu;

    s32 length(const char* pText) {
        s32 n = 0;
        while (pText[n] != '\0') {
            ++n;
        }
        return n;
    }

    bool startsWith(const char* pText, const char* pStart, s32 startLength) {
        for (s32 i = 0; i < startLength; ++i) {
            if (pText[i] != pStart[i]) {
                return false;
            }
        }
        return true;
    }

    // The nerve's name as mods use it: after "Nrv" ("KuriboNrvWalk", "HostTypeNrvDash"); else without a leading
    // "HostType" or class name; else whole. (tools/modding/nerves_list.py names them the same way.)
    const char* shortName(const nebula::NerveRecord* pRecord) {
        const char* const pName = pRecord->mName;
        const s32 nameLength = length(pName);
        for (s32 i = 0; i + 3 < nameLength; ++i) {
            if (pName[i] == 'N' && pName[i + 1] == 'r' && pName[i + 2] == 'v') {
                return pName + i + 3;
            }
        }
        const s32 hostLength = 8;
        if (nameLength > hostLength && startsWith(pName, "HostType", hostLength)) {
            return pName + hostLength;
        }
        const s32 classLength = length(pRecord->mClass);
        if (nameLength > classLength && startsWith(pName, pRecord->mClass, classLength)) {
            return pName + classLength;
        }
        return pName;
    }
}  // namespace

namespace nebula {
    bool (*gNerveHook)(Spine* pSpine, const Nerve* pNerve, s32 phase) = nullptr;

    void indexNerves() {
        if (sIndexed) {
            return;
        }
        sIndexed = true;
        // A nerve of a header has a record in each file including it, all naming the one nerve: the first counts.
        s32 count = 0;
        s32 textBytes = 0;
        for (const NerveRecord* pRecord = begin(); pRecord < end(); ++pRecord) {
            if (pRecord->mName == nullptr || pRecord->mNerve->mNebulaRecord != 0) {
                continue;
            }
            pRecord->mNerve->mNebulaRecord = kCounting;
            ++count;
            textBytes += length(pRecord->mClass) + 1 + length(shortName(pRecord)) + 1;
        }
        if (count >= static_cast< s32 >(kCounting)) {
            count = kCounting - 1;
        }
        sRecords = new (NebulaArena::root(), 0) const NerveRecord*[count > 0 ? count : 1];
        sKeys = new (NebulaArena::root(), 0) const char*[count > 0 ? count : 1];
        char* pText = new (NebulaArena::root(), 0) char[textBytes > 0 ? textBytes : 1];
        for (const NerveRecord* pRecord = begin(); pRecord < end() && sCount < count; ++pRecord) {
            if (pRecord->mName == nullptr || pRecord->mNerve->mNebulaRecord != kCounting) {
                continue;
            }
            sRecords[sCount] = pRecord;
            sKeys[sCount] = pText;
            const s32 classLength = length(pRecord->mClass);
            memcpy(pText, pRecord->mClass, classLength);
            pText[classLength] = '.';
            const char* const pShort = shortName(pRecord);
            const s32 shortLength = length(pShort);
            memcpy(pText + classLength + 1, pShort, shortLength + 1);
            pText += classLength + 1 + shortLength + 1;
            ++sCount;
            pRecord->mNerve->mNebulaRecord = static_cast< u16 >(sCount);
        }
    }

    s32 nerveCount() {
        indexNerves();
        return sCount;
    }

    const NerveRecord* nerveAt(s32 index) {
        indexNerves();
        return index >= 0 && index < sCount ? sRecords[index] : nullptr;
    }

    const char* nerveKeyAt(s32 index) {
        indexNerves();
        return index >= 0 && index < sCount ? sKeys[index] : nullptr;
    }

    const NerveRecord* nerveRecordOf(const Nerve* pNerve) {
        indexNerves();
        if (pNerve == nullptr || pNerve->mNebulaRecord == 0 || pNerve->mNebulaRecord > sCount) {
            return nullptr;
        }
        return sRecords[pNerve->mNebulaRecord - 1];
    }

    const char* nerveKeyOf(const Nerve* pNerve) {
        indexNerves();
        if (pNerve == nullptr || pNerve->mNebulaRecord == 0 || pNerve->mNebulaRecord > sCount) {
            return nullptr;
        }
        return sKeys[pNerve->mNebulaRecord - 1];
    }

    const NerveRecord* findNerve(const char* pKey) {
        indexNerves();
        if (pKey == nullptr) {
            return nullptr;
        }
        for (s32 i = 0; i < sCount; ++i) {
            if (strcmp(sKeys[i], pKey) == 0) {
                return sRecords[i];
            }
        }
        return nullptr;
    }
}  // namespace nebula
