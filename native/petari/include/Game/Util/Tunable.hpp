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
        const void* mGameValues;  // the game's numbers, as compiled
        u32 mKind;
        u32 mCount;  // 1, or a list's length
    };

    // By "<file stem>.<name>", or "<file stem>.<name>[<i>]" for a list's place: every number of that name in that file.
    // 0: set; -1: no such number.
    s32 setTunable(const char* pKey, f64 value);
    // The number the key names now and the game's number (the first, if several share the name). false: no such number.
    bool getTunable(const char* pKey, f64* pValue, f64* pGameValue);
    // The index-th number (a list counts each place): file, name, its place in a list (-1 for one number), kind, the
    // game's number and the number now. false: past the end.
    bool tunableAt(s32 index, const char** pFile, const char** pName, s32* pPlace, u32* pKind, f64* pGameValue, f64* pValue);
}  // namespace nebula

#if defined(_WIN32)
#pragma section("nebtun$m", read)
#define NEBULA_TUNABLE_SECTION __declspec(allocate("nebtun$m"))
#elif defined(__APPLE__)
// Mach-O names a section by its segment too.
#define NEBULA_TUNABLE_SECTION __attribute__((section("__DATA,nebtun")))
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

// (The number's parameter is not called "value": that would also replace TunableKindOf<type>::value.)
#define NEBULA_TUNABLE(type, name, number)                                                                                    \
    static type name = (number);                                                                                              \
    static const type name##_nebula_game = (number);                                                                          \
    NEBULA_TUNABLE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::Tunable name##_nebula_tunable = {                       \
        __FILE_NAME__, #name, &name, &name##_nebula_game, ::nebula::TunableKindOf<type>::value, 1u}

// A number written inside a function (a step count: `MR::isStep(this, 60)`), named for luma.json as
// "<file stem>.<function>.<k>": NEBULA_TUNABLE_NAMED(s32, cStep_exeWalk_1, "exeWalk.1", 60);
#define NEBULA_TUNABLE_NAMED(type, name, key, number)                                                                        \
    static type name = (number);                                                                                              \
    static const type name##_nebula_game = (number);                                                                          \
    NEBULA_TUNABLE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::Tunable name##_nebula_tunable = {                       \
        __FILE_NAME__, key, &name, &name##_nebula_game, ::nebula::TunableKindOf<type>::value, 1u}

// A record of floats only (a Vec, an MR::ActorMoveParam), as a list of its floats in order:
// NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hWaitParam, {0.0f, 3.0f, 0.95f, 1.0f}); for
// `const MR::ActorMoveParam hWaitParam = {0.0f, 3.0f, 0.95f, 1.0f};`. luma.json sets "<file stem>.hWaitParam" as a list.
#define NEBULA_TUNABLE_RECORD(type, name, ...)                                                                                \
    static type name = __VA_ARGS__;                                                                                           \
    static const type name##_nebula_game = __VA_ARGS__;                                                                       \
    static_assert(sizeof(type) % sizeof(f32) == 0 && alignof(type) == alignof(f32), #type " is not floats only");             \
    NEBULA_TUNABLE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::Tunable name##_nebula_tunable = {                       \
        __FILE_NAME__, #name, &name, &name##_nebula_game, ::nebula::TunableKind_F32,                                          \
        static_cast<u32>(sizeof(type) / sizeof(f32))}

// A list of such records, as one list of all their floats: NEBULA_TUNABLE_RECORDS(Vec, sOffsets, 2, {{...}, {...}});
#define NEBULA_TUNABLE_RECORDS(type, name, size, ...)                                                                         \
    static type name[size] = __VA_ARGS__;                                                                                     \
    static const type name##_nebula_game[size] = __VA_ARGS__;                                                                 \
    static_assert(sizeof(type) % sizeof(f32) == 0 && alignof(type) == alignof(f32), #type " is not floats only");             \
    NEBULA_TUNABLE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::Tunable name##_nebula_tunable = {                       \
        __FILE_NAME__, #name, name, name##_nebula_game, ::nebula::TunableKind_F32,                                            \
        static_cast<u32>(sizeof(name) / sizeof(name[0]) * (sizeof(type) / sizeof(f32)))}

// A list: NEBULA_TUNABLE_LIST(f32, cSpeeds, 3, {1.0f, 2.0f, 4.0f}); for `const f32 cSpeeds[3] = {1.0f, 2.0f, 4.0f};`
// (the size may be left empty, as the definition had it). luma.json sets it as a list, null keeping a place.
#define NEBULA_TUNABLE_LIST(type, name, size, ...)                                                                            \
    static type name[size] = __VA_ARGS__;                                                                                     \
    static const type name##_nebula_game[size] = __VA_ARGS__;                                                                 \
    NEBULA_TUNABLE_SECTION NEBULA_TUNABLE_KEEP static const ::nebula::Tunable name##_nebula_tunable = {                       \
        __FILE_NAME__, #name, name, name##_nebula_game, ::nebula::TunableKindOf<type>::value,                                 \
        static_cast<u32>(sizeof(name) / sizeof(name[0]))}
