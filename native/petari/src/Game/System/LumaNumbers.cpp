// Numbers in the game's code by name (LumaNumbers.hpp; contract numbers, plan 1.5).
#include "Game/System/LumaNumbers.hpp"

#include "Game/System/LumaHook.hpp"
#include "Game/System/LumaLoader.hpp"

#include <luma_hook.h>
#include <nebula_format.h>
#include <stdlib.h>

namespace luma {
#if defined(__APPLE__)
    extern const Numbers sNumbersStart __asm("section$start$__DATA$lumanum");
    extern const Numbers sNumbersStop __asm("section$end$__DATA$lumanum");
#elif !defined(_WIN32)
    extern "C" const Numbers __start_lumanum[];
    extern "C" const Numbers __stop_lumanum[];
#endif

    namespace numbers {
        namespace {
            // The units' records the linker gathered. On Windows "lumanum$a" < "$m" < "$z", and the linker may pad
            // between objects with zeros: each record is 32 bytes aligned to 32 (luma_hook.h), so a pad is whole
            // records, each with a null unit.
#if defined(_WIN32)
            __attribute__((section("lumanum$a"), used)) const Numbers sBefore = {};
            __attribute__((section("lumanum$z"), used)) const Numbers sAfter = {};
            const Numbers* hidden(const Numbers* p) {
                // The walk goes from one marker to the other: the compiler must not know they are two objects.
                __asm__("" : "+r"(p));
                return p;
            }
            const Numbers* unitsBegin() { return hidden(&sBefore) + 1; }
            const Numbers* unitsEnd() { return hidden(&sAfter); }
#elif defined(__APPLE__)
            const Numbers* unitsBegin() { return &sNumbersStart; }
            const Numbers* unitsEnd() { return &sNumbersStop; }
#else
            const Numbers* unitsBegin() { return __start_lumanum; }
            const Numbers* unitsEnd() { return __stop_lumanum; }
#endif

            // Every number by (name, place): open addressing, linear probing, made at the first ask and never changed.
            const Number** sSlots = nullptr;
            u32 sSlotCount = 0;  // a power of two
            u32 sCount = 0;
            u32 sBuilt = 0;  // 1 once sSlots is whole (read with acquire)
            u32 sLock = 0;

            // What mods set: each number one has set, with the game's value from before the first set; and each set,
            // in order (a mod's later set of one number replaces its earlier one at the end).
            struct Touched {
                const Number* number;
                u64 game;
            };
            struct Setting {
                u32 touched;
                const LumaMod* mod;
                u64 bits;
            };
            Touched* sTouched = nullptr;
            u32 sTouchedCount = 0;
            u32 sTouchedRoom = 0;
            Setting* sSettings = nullptr;
            u32 sSettingCount = 0;
            u32 sSettingRoom = 0;

            void lock() {
                while (__atomic_exchange_n(&sLock, 1u, __ATOMIC_ACQUIRE) != 0u) {
#if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                    __asm__ volatile("yield");
#endif
                }
            }

            void unlock() { __atomic_store_n(&sLock, 0u, __ATOMIC_RELEASE); }

            __attribute__((format(printf, 2, 3))) void say(const LumaMod* pMod, const char* pFormat, ...) {
                char text[512];
                __builtin_va_list args;
                __builtin_va_start(args, pFormat);
                nebula_vsnprintf(text, sizeof(text), pFormat, args);
                __builtin_va_end(args);
                luma::core()->log(pMod, text);
            }

            bool same(const char* pA, const char* pB) {
                while (*pA != 0 && *pA == *pB) {
                    pA++;
                    pB++;
                }
                return *pA == *pB;
            }

            u32 hashOf(const char* pName, u32 place) {
                u32 h = 2166136261u;
                for (const char* p = pName; *p != 0; p++) {
                    h = (h ^ static_cast<u8>(*p)) * 16777619u;
                }
                h = (h ^ place) * 16777619u;
                return h ^ (h >> 15);
            }

            // false: no memory (said by the caller).
            bool build() {
                if (__atomic_load_n(&sBuilt, __ATOMIC_ACQUIRE) != 0u) {
                    return true;
                }
                lock();
                if (sBuilt == 0u) {
                    u32 count = 0;
                    for (const Numbers* pUnit = unitsBegin(); pUnit < unitsEnd(); pUnit++) {
                        if (pUnit->unit != nullptr) {
                            count += pUnit->count;
                        }
                    }
                    u32 slots = 16;
                    while (slots < count * 2u) {
                        slots <<= 1;
                    }
                    const Number** const pSlots = static_cast<const Number**>(::calloc(slots, sizeof(const Number*)));
                    if (pSlots == nullptr) {
                        unlock();
                        return false;
                    }
                    for (const Numbers* pUnit = unitsBegin(); pUnit < unitsEnd(); pUnit++) {
                        if (pUnit->unit == nullptr) {
                            continue;
                        }
                        for (u32 i = 0; i < pUnit->count; i++) {
                            const Number* const pNumber = &pUnit->numbers[i];
                            u32 at = hashOf(pNumber->name, pNumber->place) & (slots - 1);
                            while (pSlots[at] != nullptr) {
                                at = (at + 1) & (slots - 1);
                            }
                            pSlots[at] = pNumber;
                        }
                    }
                    sSlots = pSlots;
                    sSlotCount = slots;
                    sCount = count;
                    __atomic_store_n(&sBuilt, 1u, __ATOMIC_RELEASE);
                }
                unlock();
                return true;
            }

            // The number, or null with *pSeveral the count of matches when a null signature matches more than one.
            const Number* lookUp(const char* pName, const char* pSignature, u32 place, u32* pSeveral) {
                *pSeveral = 0;
                const Number* pFound = nullptr;
                const char* const pWanted = pSignature != nullptr ? pSignature : nullptr;
                for (u32 at = hashOf(pName, place) & (sSlotCount - 1); sSlots[at] != nullptr; at = (at + 1) & (sSlotCount - 1)) {
                    const Number* const pNumber = sSlots[at];
                    if (pNumber->place != place || !same(pNumber->name, pName)) {
                        continue;
                    }
                    if (pWanted != nullptr) {
                        if (same(pNumber->signature, pWanted)) {
                            return pNumber;
                        }
                        continue;
                    }
                    pFound = pNumber;
                    (*pSeveral)++;
                }
                if (*pSeveral > 1) {
                    return nullptr;
                }
                *pSeveral = 0;
                return pFound;
            }

            u32 sizeOf(u32 kind) {
                static const u8 kSizes[11] = {0, 4, 8, 1, 1, 2, 2, 4, 4, 8, 8};
                return kind < 11 ? kSizes[kind] : 0;
            }

            u64 read(const Number* pNumber) {
                u64 bits = 0;
                __builtin_memcpy(&bits, pNumber->address, sizeOf(pNumber->kind));
                return bits;
            }

            void write(const Number* pNumber, u64 bits) { __builtin_memcpy(pNumber->address, &bits, sizeOf(pNumber->kind)); }

            // The value as the number's kind; false when it does not fit (not whole for a whole number, out of range).
            bool encode(u32 kind, f64 value, u64* pBits) {
                if (!(value == value)) {
                    return false;
                }
                if (kind == 1) {
                    if (value > 3.4028234663852886e38 || value < -3.4028234663852886e38) {
                        return false;
                    }
                    const f32 f = static_cast<f32>(value);
                    u32 b;
                    __builtin_memcpy(&b, &f, 4);
                    *pBits = b;
                    return true;
                }
                if (kind == 2) {
                    __builtin_memcpy(pBits, &value, 8);
                    return true;
                }
                if (value != static_cast<f64>(static_cast<s64>(value)) && !(value >= 9223372036854775808.0 && value < 18446744073709551616.0)) {
                    return false;
                }
                static const f64 kLow[11] = {0, 0, 0, -128.0, 0, -32768.0, 0, -2147483648.0, 0, -9223372036854775808.0, 0};
                static const f64 kHigh[11] = {0, 0, 0, 127.0, 255.0, 32767.0, 65535.0, 2147483647.0, 4294967295.0,
                                              9223372036854775807.0, 18446744073709551615.0};
                if (kind < 3 || kind > 10 || value < kLow[kind] || value > kHigh[kind]) {
                    return false;
                }
                // Even kinds are unsigned (4, 6, 8, 10).
                *pBits = (kind % 2 == 0) ? static_cast<u64>(value) : static_cast<u64>(static_cast<s64>(value));
                return true;
            }

            template <class T> bool grow(T** ppItems, u32* pRoom, u32 count) {
                if (count < *pRoom) {
                    return true;
                }
                const u32 room = *pRoom == 0 ? 64 : *pRoom * 2;
                T* const pNew = static_cast<T*>(::realloc(*ppItems, room * sizeof(T)));
                if (pNew == nullptr) {
                    return false;
                }
                *ppItems = pNew;
                *pRoom = room;
                return true;
            }

            void nameOf(char* pOut, u32 size, const char* pName, const char* pSignature, u32 place) {
                if (pSignature != nullptr && pSignature[0] != 0) {
                    nebula_snprintf(pOut, size, "%s %s.%u", pName, pSignature, place);
                } else {
                    nebula_snprintf(pOut, size, "%s.%u", pName, place);
                }
            }

            // The number a mod names, or null, said.
            const Number* named(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, const char* pAsk) {
                if (pName == nullptr || pName[0] == 0) {
                    say(pMod, "%s: no name", pAsk);
                    return nullptr;
                }
                if (!build()) {
                    say(pMod, "%s %s: no memory for the numbers' index", pAsk, pName);
                    return nullptr;
                }
                // A file's constant has the signature "": null asks the same.
                const char* const pSig = (place == 0 && pSignature == nullptr) ? "" : pSignature;
                u32 several = 0;
                const Number* const pNumber = lookUp(pName, pSig, place, &several);
                if (pNumber == nullptr) {
                    char name[320];
                    nameOf(name, sizeof(name), pName, pSig, place);
                    if (several > 1) {
                        say(pMod, "%s %s: %u functions of that name have a number at that place; give the signature", pAsk, name, several);
                    } else {
                        say(pMod, "%s %s: the game's code has no such number (luma_numbers.json lists them)", pAsk, name);
                    }
                }
                return pNumber;
            }
        }  // namespace

        void* find(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, u32* pKind) {
            const Number* const pNumber = named(pMod, pName, pSignature, place, "number");
            if (pKind != nullptr) {
                *pKind = pNumber != nullptr ? pNumber->kind : 0;
            }
            return pNumber != nullptr ? pNumber->address : nullptr;
        }

        s32 set(const LumaMod* pMod, const char* pName, const char* pSignature, u32 place, f64 value) {
            if (!onGameThread()) {
                return -3;
            }
            const Number* const pNumber = named(pMod, pName, pSignature, place, "set_number");
            if (pNumber == nullptr) {
                return -1;
            }
            u64 bits = 0;
            if (!encode(pNumber->kind, value, &bits)) {
                static const char* const kKinds[11] = {"?", "f32", "f64", "s8", "u8", "s16", "u16", "s32", "u32", "s64", "u64"};
                char name[320];
                nameOf(name, sizeof(name), pNumber->name, pNumber->signature, pNumber->place);
                say(pMod, "set_number %s: %g is no %s", name, value, kKinds[pNumber->kind < 11 ? pNumber->kind : 0]);
                return -2;
            }
            u32 touched = 0;
            while (touched < sTouchedCount && sTouched[touched].number != pNumber) {
                touched++;
            }
            if (touched == sTouchedCount) {
                if (!grow(&sTouched, &sTouchedRoom, sTouchedCount)) {
                    say(pMod, "set_number %s: no memory to remember the game's number", pNumber->name);
                    return -1;
                }
                sTouched[sTouchedCount++] = {pNumber, read(pNumber)};
            }
            // The mod's earlier set of this number goes; this one is the last.
            for (u32 i = 0; i < sSettingCount; i++) {
                if (sSettings[i].touched == touched && sSettings[i].mod == pMod) {
                    for (u32 j = i + 1; j < sSettingCount; j++) {
                        sSettings[j - 1] = sSettings[j];
                    }
                    sSettingCount--;
                    break;
                }
            }
            if (!grow(&sSettings, &sSettingRoom, sSettingCount)) {
                say(pMod, "set_number %s: no memory to remember the set", pNumber->name);
                return -1;
            }
            sSettings[sSettingCount++] = {touched, pMod, bits};
            write(pNumber, bits);
            return 0;
        }

        u32 count() {
            return build() ? sCount : 0;
        }

        void modGone(const LumaMod* pMod) {
            u32 kept = 0;
            u32 undone = 0;
            for (u32 i = 0; i < sSettingCount; i++) {
                if (sSettings[i].mod != pMod) {
                    sSettings[kept++] = sSettings[i];
                    continue;
                }
                const u32 touched = sSettings[i].touched;
                undone++;
                // What the mods still here set last, or the game's own.
                u64 bits = sTouched[touched].game;
                for (u32 j = sSettingCount; j-- > 0;) {
                    if (sSettings[j].touched == touched && sSettings[j].mod != pMod) {
                        bits = sSettings[j].bits;
                        break;
                    }
                }
                write(sTouched[touched].number, bits);
            }
            sSettingCount = kept;
            if (undone != 0) {
                say(pMod, "its %u numbers are the game's again (or another mod's)", undone);
            }
        }
    }
}
