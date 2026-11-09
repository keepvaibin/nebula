#pragma once

#include <nebula_types.h>

// A file's tuning number that mods set from luma.json ("tunables": {"Kuribo.cWalkSpeed": 2.0}) without a recompile.
// NEBULA_TUNABLE(f32, cWalkSpeed, 2.0f); stands where `const f32 cWalkSpeed = 2.0f;` stood: the same name, still the
// file's own, its value constant-initialized (no constructor). Beside it a record goes into the "nebtun" section, which
// the game walks to set a number by "<file stem>.<name>". Numbers used where the language needs a constant (an array
// size, a case, a template argument) or in another file-scope or static initializer stay const.

namespace nebula {
    enum TunableKind : u32 {
        TunableKind_F32 = 1,
        TunableKind_F64,
        TunableKind_S32,
        TunableKind_U32,
        TunableKind_S16,
        TunableKind_U16,
        TunableKind_S8,
        TunableKind_U8,
        TunableKind_Bool,
    };

    template <typename T>
    struct TunableKindOf;
    template <> struct TunableKindOf<f32> { static constexpr u32 value = TunableKind_F32; };
    template <> struct TunableKindOf<f64> { static constexpr u32 value = TunableKind_F64; };
    template <> struct TunableKindOf<s32> { static constexpr u32 value = TunableKind_S32; };
    template <> struct TunableKindOf<u32> { static constexpr u32 value = TunableKind_U32; };
    template <> struct TunableKindOf<s16> { static constexpr u32 value = TunableKind_S16; };
    template <> struct TunableKindOf<u16> { static constexpr u32 value = TunableKind_U16; };
    template <> struct TunableKindOf<s8> { static constexpr u32 value = TunableKind_S8; };
    template <> struct TunableKindOf<u8> { static constexpr u32 value = TunableKind_U8; };
    template <> struct TunableKindOf<bool> { static constexpr u32 value = TunableKind_Bool; };

    struct Tunable {
        const char* mFile;  // __FILE_NAME__: "Kuribo.cpp"
        const char* mName;  // "cWalkSpeed"
        void* mAddress;
        u32 mKind;
        f64 mDefault;
    };

    // By "<file stem>.<name>": every number of that name in that file. 0: set; -1: no such number.
    s32 setTunable(const char* pKey, f64 value);
    // The index-th record past the section's padding: its key parts, kind, default and current value. false: past the end.
    bool tunableAt(s32 index, const char** pFile, const char** pName, u32* pKind, f64* pDefault, f64* pValue);
}  // namespace nebula

#if defined(_WIN32)
#pragma section("nebtun$m", read)
#define NEBULA_TUNABLE_SECTION __declspec(allocate("nebtun$m"))
#else
#define NEBULA_TUNABLE_SECTION __attribute__((section("nebtun")))
#endif

#if defined(__has_attribute)
#if __has_attribute(retain)
#define NEBULA_TUNABLE_KEEP __attribute__((used, retain))
#endif
#endif
#ifndef NEBULA_TUNABLE_KEEP
#define NEBULA_TUNABLE_KEEP __attribute__((used))
#endif

#define NEBULA_TUNABLE(type, name, value)                                                                                     \
    static type name = (value);                                                                                               \
    NEBULA_TUNABLE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::Tunable name##_nebula_tunable = {                       \
        __FILE_NAME__, #name, &name, ::nebula::TunableKindOf<type>::value, static_cast<f64>(value)}
