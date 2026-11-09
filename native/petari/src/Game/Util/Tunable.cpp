#include "Game/Util/Tunable.hpp"

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

    bool isName(const char* pName, const char* pKeyName) {
        while (*pName != '\0' && *pName == *pKeyName) {
            ++pName;
            ++pKeyName;
        }
        return *pName == *pKeyName;
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
        return static_cast<T>(value < 0.0 ? value - 0.5 : value + 0.5);
    }

    void store(const nebula::Tunable& rTunable, f64 value) {
        switch (rTunable.mKind) {
        case nebula::TunableKind_F32:
            *static_cast<f32*>(rTunable.mAddress) = static_cast<f32>(value);
            break;
        case nebula::TunableKind_F64:
            *static_cast<f64*>(rTunable.mAddress) = value;
            break;
        case nebula::TunableKind_S32:
            *static_cast<s32*>(rTunable.mAddress) = clampTo<s32>(value, -2147483648.0, 2147483647.0);
            break;
        case nebula::TunableKind_U32:
            *static_cast<u32*>(rTunable.mAddress) = clampTo<u32>(value, 0.0, 4294967295.0);
            break;
        case nebula::TunableKind_S16:
            *static_cast<s16*>(rTunable.mAddress) = clampTo<s16>(value, -32768.0, 32767.0);
            break;
        case nebula::TunableKind_U16:
            *static_cast<u16*>(rTunable.mAddress) = clampTo<u16>(value, 0.0, 65535.0);
            break;
        case nebula::TunableKind_S8:
            *static_cast<s8*>(rTunable.mAddress) = clampTo<s8>(value, -128.0, 127.0);
            break;
        case nebula::TunableKind_U8:
            *static_cast<u8*>(rTunable.mAddress) = clampTo<u8>(value, 0.0, 255.0);
            break;
        case nebula::TunableKind_Bool:
            *static_cast<bool*>(rTunable.mAddress) = value != 0.0;
            break;
        default:
            break;
        }
    }

    f64 load(const nebula::Tunable& rTunable) {
        switch (rTunable.mKind) {
        case nebula::TunableKind_F32:
            return *static_cast<const f32*>(rTunable.mAddress);
        case nebula::TunableKind_F64:
            return *static_cast<const f64*>(rTunable.mAddress);
        case nebula::TunableKind_S32:
            return *static_cast<const s32*>(rTunable.mAddress);
        case nebula::TunableKind_U32:
            return *static_cast<const u32*>(rTunable.mAddress);
        case nebula::TunableKind_S16:
            return *static_cast<const s16*>(rTunable.mAddress);
        case nebula::TunableKind_U16:
            return *static_cast<const u16*>(rTunable.mAddress);
        case nebula::TunableKind_S8:
            return *static_cast<const s8*>(rTunable.mAddress);
        case nebula::TunableKind_U8:
            return *static_cast<const u8*>(rTunable.mAddress);
        case nebula::TunableKind_Bool:
            return *static_cast<const bool*>(rTunable.mAddress) ? 1.0 : 0.0;
        default:
            return 0.0;
        }
    }
}  // namespace

namespace nebula {
    s32 setTunable(const char* pKey, f64 value) {
        if (pKey == nullptr) {
            return -1;
        }
        s32 dot = -1;
        for (s32 i = 0; pKey[i] != '\0'; ++i) {
            if (pKey[i] == '.') {
                dot = i;
            }
        }
        if (dot <= 0 || pKey[dot + 1] == '\0') {
            return -1;
        }
        s32 result = -1;
        for (const Tunable* pTunable = begin(); pTunable < end(); ++pTunable) {
            if (pTunable->mName != nullptr && isFile(pTunable->mFile, pKey, dot) && isName(pTunable->mName, pKey + dot + 1)) {
                store(*pTunable, value);
                result = 0;
            }
        }
        return result;
    }

    bool tunableAt(s32 index, const char** pFile, const char** pName, u32* pKind, f64* pDefault, f64* pValue) {
        if (index < 0) {
            return false;
        }
        for (const Tunable* pTunable = begin(); pTunable < end(); ++pTunable) {
            if (pTunable->mName == nullptr) {
                continue;
            }
            if (index-- == 0) {
                *pFile = pTunable->mFile;
                *pName = pTunable->mName;
                *pKind = pTunable->mKind;
                *pDefault = pTunable->mDefault;
                *pValue = load(*pTunable);
                return true;
            }
        }
        return false;
    }
}  // namespace nebula
