#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f64, cNum_clampTo_f1, "clampTo.f1", 0.5);
NEBULA_TUNABLE_NAMED(f64, cNum_clampTo_f2, "clampTo.f2", 0.5);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f1, "store.f1", 2147483648.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f2, "store.f2", 2147483647.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f3, "store.f3", 4294967295.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f4, "store.f4", 32768.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f5, "store.f5", 32767.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f6, "store.f6", 65535.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f7, "store.f7", 128.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f8, "store.f8", 127.0);
NEBULA_TUNABLE_NAMED(f64, cNum_store_f9, "store.f9", 255.0);

// The records lie between two markers: on Windows the linker orders "nebtun$a" < "nebtun$m" < "nebtun$z" and may pad
// between object files with zeros (a record whose name is null is padding); on ELF the linker names the section's ends.
namespace {
#if defined(_WIN32)
#pragma section("nebtun$a", read)
#pragma section("nebtun$z", read)
    __declspec(allocate("nebtun$a")) NEBULA_TUNABLE_KEEP const nebula::Tunable sFirst = {};
    __declspec(allocate("nebtun$z")) NEBULA_TUNABLE_KEEP const nebula::Tunable sLast = {};
    // The walk goes from one marker to the other: through an empty asm the compiler no longer knows they are two objects,
    // so it can't fold the walk away as running past sFirst.
    const nebula::Tunable* hidden(const nebula::Tunable* pTunable) {
        __asm__("" : "+r"(pTunable));
        return pTunable;
    }
    const nebula::Tunable* begin() {
        return hidden(&sFirst) + 1;
    }
    const nebula::Tunable* end() {
        return hidden(&sLast);
    }
#elif defined(__APPLE__)
}  // namespace
// Mach-O: the linker names the section's ends too, by these names.
extern "C" {
    extern const nebula::Tunable __start_nebtun[] __asm__("section$start$__DATA$nebtun");
    extern const nebula::Tunable __stop_nebtun[] __asm__("section$end$__DATA$nebtun");
}
namespace {
    const nebula::Tunable* begin() {
        return __start_nebtun;
    }
    const nebula::Tunable* end() {
        return __stop_nebtun;
    }
#else
}  // namespace
extern "C" {
    extern const nebula::Tunable __start_nebtun[] __attribute__((visibility("hidden")));
    extern const nebula::Tunable __stop_nebtun[] __attribute__((visibility("hidden")));
}
namespace {
    const nebula::Tunable* begin() {
        return __start_nebtun;
    }
    const nebula::Tunable* end() {
        return __stop_nebtun;
    }
#endif

    // "Kuribo" names "Kuribo.cpp": the stem, then the extension's dot.
    bool isFile(const char* pFile, const char* pStem, s32 stemLength) {
        for (s32 i = 0; i < stemLength; ++i) {
            if (pFile[i] != pStem[i]) {
                return false;
            }
        }
        return pFile[stemLength] == '.';
    }

    bool isName(const char* pName, const char* pKeyName, s32 nameLength) {
        for (s32 i = 0; i < nameLength; ++i) {
            if (pName[i] != pKeyName[i]) {
                return false;
            }
        }
        return pName[nameLength] == '\0';
    }

    template <typename T>
    T clampTo(f64 value, f64 low, f64 high) {
        if (!(value >= low)) {
            return static_cast<T>(low);
        }
        if (value > high) {
            return static_cast<T>(high);
        }
        // Halves away from zero: 2.5 sets 3, -2.5 sets -3.
        return static_cast<T>(value < 0.0 ? value - cNum_clampTo_f1 : value + cNum_clampTo_f2);
    }

    void store(void* pAddress, u32 kind, u32 place, f64 value) {
        switch (kind) {
        case nebula::TunableKind_F32:
            static_cast<f32*>(pAddress)[place] = static_cast<f32>(value);
            break;
        case nebula::TunableKind_F64:
            static_cast<f64*>(pAddress)[place] = value;
            break;
        case nebula::TunableKind_S32:
            static_cast<s32*>(pAddress)[place] = clampTo<s32>(value, -cNum_store_f1, cNum_store_f2);
            break;
        case nebula::TunableKind_U32:
            static_cast<u32*>(pAddress)[place] = clampTo<u32>(value, 0.0, cNum_store_f3);
            break;
        case nebula::TunableKind_S16:
            static_cast<s16*>(pAddress)[place] = clampTo<s16>(value, -cNum_store_f4, cNum_store_f5);
            break;
        case nebula::TunableKind_U16:
            static_cast<u16*>(pAddress)[place] = clampTo<u16>(value, 0.0, cNum_store_f6);
            break;
        case nebula::TunableKind_S8:
            static_cast<s8*>(pAddress)[place] = clampTo<s8>(value, -cNum_store_f7, cNum_store_f8);
            break;
        case nebula::TunableKind_U8:
            static_cast<u8*>(pAddress)[place] = clampTo<u8>(value, 0.0, cNum_store_f9);
            break;
        case nebula::TunableKind_Bool:
            static_cast<bool*>(pAddress)[place] = value != 0.0;
            break;
        default:
            break;
        }
    }

    f64 load(const void* pAddress, u32 kind, u32 place) {
        switch (kind) {
        case nebula::TunableKind_F32:
            return static_cast<const f32*>(pAddress)[place];
        case nebula::TunableKind_F64:
            return static_cast<const f64*>(pAddress)[place];
        case nebula::TunableKind_S32:
            return static_cast<const s32*>(pAddress)[place];
        case nebula::TunableKind_U32:
            return static_cast<const u32*>(pAddress)[place];
        case nebula::TunableKind_S16:
            return static_cast<const s16*>(pAddress)[place];
        case nebula::TunableKind_U16:
            return static_cast<const u16*>(pAddress)[place];
        case nebula::TunableKind_S8:
            return static_cast<const s8*>(pAddress)[place];
        case nebula::TunableKind_U8:
            return static_cast<const u8*>(pAddress)[place];
        case nebula::TunableKind_Bool:
            return static_cast<const bool*>(pAddress)[place] ? 1.0 : 0.0;
        default:
            return 0.0;
        }
    }
    // "<stem>.<name>" or "<stem>.<name>[<place>]": the stem ends at the first dot (a name may hold dots: "exeWalk.1").
    struct Key {
        const char* mText;
        s32 mDot;
        s32 mNameEnd;
        s32 mPlace;  // -1: no bracket
    };

    bool parseKey(const char* pKey, Key* pOut) {
        if (pKey == nullptr) {
            return false;
        }
        s32 length = 0;
        s32 dot = -1;
        s32 bracket = -1;
        for (; pKey[length] != '\0'; ++length) {
            if (pKey[length] == '[' && bracket < 0) {
                bracket = length;
            } else if (pKey[length] == '.' && bracket < 0 && dot < 0) {
                dot = length;
            }
        }
        const s32 nameEnd = bracket >= 0 ? bracket : length;
        if (dot <= 0 || nameEnd <= dot + 1) {
            return false;
        }
        s32 place = -1;
        if (bracket >= 0) {
            place = 0;
            s32 i = bracket + 1;
            if (pKey[i] < '0' || pKey[i] > '9') {
                return false;
            }
            for (; pKey[i] >= '0' && pKey[i] <= '9'; ++i) {
                place = place * 10 + (pKey[i] - '0');
                if (place > 0xFFFF) {
                    return false;
                }
            }
            if (pKey[i] != ']' || pKey[i + 1] != '\0') {
                return false;
            }
        }
        *pOut = {pKey, dot, nameEnd, place};
        return true;
    }

    // The next record at or after pFrom the key names, and the place in it; end() when none is left. A list takes its
    // places; one number takes its own name, or place 0.
    const nebula::Tunable* findFrom(const nebula::Tunable* pFrom, const Key& rKey, u32* pAt) {
        const s32 at = rKey.mPlace < 0 ? 0 : rKey.mPlace;
        for (const nebula::Tunable* pTunable = pFrom; pTunable < end(); ++pTunable) {
            if (pTunable->mName == nullptr || !isFile(pTunable->mFile, rKey.mText, rKey.mDot) ||
                !isName(pTunable->mName, rKey.mText + rKey.mDot + 1, rKey.mNameEnd - rKey.mDot - 1)) {
                continue;
            }
            if ((rKey.mPlace < 0 && pTunable->mCount != 1u) || static_cast<u32>(at) >= pTunable->mCount) {
                continue;
            }
            *pAt = static_cast<u32>(at);
            return pTunable;
        }
        return end();
    }
}  // namespace

namespace nebula {
    s32 setTunable(const char* pKey, f64 value) {
        Key key;
        if (!parseKey(pKey, &key)) {
            return -1;
        }
        s32 result = -1;
        u32 at = 0;
        for (const Tunable* pTunable = findFrom(begin(), key, &at); pTunable < end(); pTunable = findFrom(pTunable + 1, key, &at)) {
            store(pTunable->mAddress, pTunable->mKind, at, value);
            result = 0;
        }
        return result;
    }

    bool getTunable(const char* pKey, f64* pValue, f64* pGameValue) {
        Key key;
        if (!parseKey(pKey, &key)) {
            return false;
        }
        u32 at = 0;
        const Tunable* const pTunable = findFrom(begin(), key, &at);
        if (pTunable >= end()) {
            return false;
        }
        if (pValue != nullptr) {
            *pValue = load(pTunable->mAddress, pTunable->mKind, at);
        }
        if (pGameValue != nullptr) {
            *pGameValue = load(pTunable->mGameValues, pTunable->mKind, at);
        }
        return true;
    }

    bool tunableAt(s32 index, const char** pFile, const char** pName, s32* pPlace, u32* pKind, f64* pGameValue, f64* pValue) {
        if (index < 0) {
            return false;
        }
        for (const Tunable* pTunable = begin(); pTunable < end(); ++pTunable) {
            if (pTunable->mName == nullptr) {
                continue;
            }
            if (static_cast<u32>(index) >= pTunable->mCount) {
                index -= static_cast<s32>(pTunable->mCount);
                continue;
            }
            *pFile = pTunable->mFile;
            *pName = pTunable->mName;
            *pPlace = pTunable->mCount == 1u ? -1 : index;
            *pKind = pTunable->mKind;
            *pGameValue = load(pTunable->mGameValues, pTunable->mKind, static_cast<u32>(index));
            *pValue = load(pTunable->mAddress, pTunable->mKind, static_cast<u32>(index));
            return true;
        }
        return false;
    }
}  // namespace nebula
