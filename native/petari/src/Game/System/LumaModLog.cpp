#include "Game/System/LumaModLog.hpp"

#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/NebulaText.hpp"
#include <luma_modlog.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// The mod log (galaxy/mod_log.h). The store is made by the first add (one compare-and-swap of sStore, section 2) and
// lives until the module unloads. Entries and mods are lists that only grow: each is made whole outside the lock and
// published by one release store of the list's last `next`, so the screen walks them with acquire loads and no lock.
// The lock is held only to look up a key, count, and link (no allocation under it).

namespace luma {
    namespace modlog {
        namespace {
            struct Store {
                u32 lock;
                u32 revision;
                u32 kept;
                u32 errors;
                Entry* head;
                Entry* tail;
                Mod* modHead;
                Mod* modTail;
                const Entry* noticed;  // the last entry the notice looked at (game thread only); null: none yet
                u32 summaryTaken;      // game thread only
            };

            // File-scope and constant-initialised (the module has no function-local statics with run-time init).
            Store* sStore = nullptr;
            u32 sModsOn = 0;
            LumaModLogFact sFact = nullptr;

            const char cArgsCut[] = "ModLog.ArgsCut";
            const char cNoText[] = "ModLog.NoText";
            const char cNebula[] = "nebula";
            const u32 cLeast[4] = {0, 0x80u, 0x800u, 0x10000u};  // the least code point of each UTF-8 length
            const char* const cSourceNames[LUMA_MODLOG_SOURCE_COUNT] = {"validator", "loader", "cook", "code", "content", "guard"};

            void* zeroed(size_t size) {
                unsigned char* const p = static_cast< unsigned char* >(::malloc(size != 0 ? size : 1));
                if (p != nullptr) {
                    for (size_t i = 0; i < size; i++) {
                        p[i] = 0;
                    }
                }
                return p;
            }

            void lockStore(Store* pStore) {
                while (__atomic_exchange_n(&pStore->lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
#if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                    __asm__ volatile("yield");
#endif
                }
            }

            void unlockStore(Store* pStore) { __atomic_store_n(&pStore->lock, 0u, __ATOMIC_RELEASE); }

            Store* storeIfAny() { return __atomic_load_n(&sStore, __ATOMIC_ACQUIRE); }

            // Made once: the first add that finds none publishes its own; one that loses the swap frees its own.
            Store* store() {
                Store* pStore = storeIfAny();
                if (pStore != nullptr) {
                    return pStore;
                }
                Store* const pMade = static_cast< Store* >(zeroed(sizeof(Store)));
                if (pMade == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for the mod log");
                }
                Store* pExpected = nullptr;
                if (__atomic_compare_exchange_n(&sStore, &pExpected, pMade, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    return pMade;
                }
                ::free(pMade);
                return pExpected;
            }

            bool same(const char* pA, const char* pB) {
                while (*pA != 0 && *pA == *pB) {
                    pA++;
                    pB++;
                }
                return *pA == *pB;
            }

            u32 lengthOf(const char* pText) {
                u32 n = 0;
                while (pText[n] != 0) {
                    n++;
                }
                return n;
            }

            u32 lengthOf16(const char16_t* pText) {
                u32 n = 0;
                while (pText[n] != 0) {
                    n++;
                }
                return n;
            }

            // One code point of UTF-8 at *ppText, advanced past it; a broken sequence is U+FFFD and one byte.
            u32 decode8(const char** ppText) {
                const unsigned char* p = reinterpret_cast< const unsigned char* >(*ppText);
                const u32 c = p[0];
                u32 need = 0;
                u32 value = 0;
                if (c < 0x80u) {
                    *ppText += 1;
                    return c;
                } else if (c >= 0xC2u && c < 0xE0u) {
                    need = 1;
                    value = c & 0x1Fu;
                } else if (c >= 0xE0u && c < 0xF0u) {
                    need = 2;
                    value = c & 0x0Fu;
                } else if (c >= 0xF0u && c < 0xF5u) {
                    need = 3;
                    value = c & 0x07u;
                } else {
                    *ppText += 1;
                    return 0xFFFDu;
                }
                for (u32 i = 1; i <= need; i++) {
                    if ((p[i] & 0xC0u) != 0x80u) {
                        *ppText += 1;
                        return 0xFFFDu;
                    }
                    value = (value << 6) | (p[i] & 0x3Fu);
                }
                if (value < cLeast[need] || value > 0x10FFFFu || (value >= 0xD800u && value < 0xE000u)) {
                    *ppText += 1;
                    return 0xFFFDu;
                }
                *ppText += need + 1;
                return value;
            }

            // Appends code point `c` as UTF-16 at pDst[at] (when pDst is not null); the units it takes.
            u32 put16(char16_t* pDst, u32 at, u32 c) {
                if (c >= 0x10000u) {
                    if (pDst != nullptr) {
                        pDst[at] = static_cast< char16_t >(0xD800u + ((c - 0x10000u) >> 10));
                        pDst[at + 1] = static_cast< char16_t >(0xDC00u + ((c - 0x10000u) & 0x3FFu));
                    }
                    return 2;
                }
                if (pDst != nullptr) {
                    pDst[at] = static_cast< char16_t >(c);
                }
                return 1;
            }

            u32 put8Units(char16_t* pDst, u32 at, const char* pText) {
                u32 n = 0;
                while (*pText != 0) {
                    n += put16(pDst, at + n, decode8(&pText));
                }
                return n;
            }

            // A host value as the fallback shows it: as given (T:/P: and all), so nothing is lost.
            // A module value: as given.
            const char* valueAt(const LumaModLogEntry& rEntry, u32 i) {
                return rEntry.values[i] != nullptr ? rEntry.values[i] : "";
            }

            u32 argCount(const LumaModLogEntry& rEntry) {
                return rEntry.count < LUMA_MODLOG_ARGS ? rEntry.count : LUMA_MODLOG_ARGS;
            }

            u32 fill(char16_t* pDst, const char16_t* pTemplate, const LumaModLogEntry& rEntry, bool english, u32 depth);

            // A value "R:<row>" (luma_modlog.h `row`): that row's words in the same language, its {names} filled from the
            // same entry, one level deep (a row inside it stays as its value). At pDst + at when pDst is not null; the
            // units, or ~0u when the value is not such a row.
            u32 putRow(char16_t* pDst, u32 at, const char* pValue, const LumaModLogEntry& rEntry, bool english, u32 depth) {
                if (depth != 0 || pValue[0] != 'R' || pValue[1] != ':' || !NebulaText::has(pValue + 2)) {
                    return ~0u;
                }
                const char* const pRow = pValue + 2;
                const char16_t* pTemplate = english ? NebulaText::english(pRow) : nullptr;
                char16_t* pPlayed = nullptr;
                if (pTemplate == nullptr) {
                    const s32 length = NebulaText::length(pRow);
                    pPlayed = static_cast< char16_t* >(::malloc((length + 1) * sizeof(char16_t)));
                    if (pPlayed == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                    }
                    NebulaText::get(pPlayed, length + 1, pRow);
                    pTemplate = pPlayed;
                }
                const u32 units = fill(pDst != nullptr ? pDst + at : nullptr, pTemplate, rEntry, english, depth + 1);
                ::free(pPlayed);
                return units;
            }

            // The row's words with {mod} and the entry's {names} filled; a {name} the entry does not give stays as it is
            // (visible, never blank). `english`: the row is the English one (a value "R:<row>" follows it).
            // Into pDst when it is not null; the units, without a terminator.
            u32 fill(char16_t* pDst, const char16_t* pTemplate, const LumaModLogEntry& rEntry, bool english, u32 depth) {
                u32 at = 0;
                const u32 count = argCount(rEntry);
                for (const char16_t* pAt = pTemplate; *pAt != 0;) {
                    if (*pAt == u'{') {
                        char name[32];
                        u32 n = 0;
                        const char16_t* pEnd = pAt + 1;
                        while (*pEnd != 0 && *pEnd != u'}' && *pEnd < 0x80 && n + 1 < sizeof(name)) {
                            name[n++] = static_cast< char >(*pEnd++);
                        }
                        name[n] = 0;
                        if (*pEnd == u'}') {
                            const char* pValue = nullptr;
                            if (same(name, "mod")) {
                                pValue = rEntry.mod;
                            } else {
                                for (u32 i = 0; i < count && pValue == nullptr; i++) {
                                    if (rEntry.names[i] != nullptr && same(name, rEntry.names[i])) {
                                        pValue = valueAt(rEntry, i);
                                    }
                                }
                            }
                            if (pValue != nullptr) {
                                const u32 rowUnits = putRow(pDst, at, pValue, rEntry, english, depth);
                                at += rowUnits != ~0u ? rowUnits : put8Units(pDst, at, pValue);
                                pAt = pEnd + 1;
                                continue;
                            }
                        }
                    }
                    if (pDst != nullptr) {
                        pDst[at] = *pAt;
                    }
                    at++;
                    pAt++;
                }
                return at;
            }

            // The fallback words: the code and every value as given ("L505 kind=P:Name.Function found=T:Mario::foo"),
            // or the row's id and the values when the row is not in the text table. UTF-8 into pDst when it is not null;
            // the bytes, without a terminator.
            u32 bare(char* pDst, const LumaModLogEntry& rEntry, const char* pRowId) {
                char head[48];
                if (pRowId != nullptr) {
                    nebula_snprintf(head, sizeof(head), "%s", pRowId);
                } else if (rEntry.source <= LUMA_MODLOG_COOK) {
                    nebula_snprintf(head, sizeof(head), "L%03u", rEntry.code);
                } else {
                    nebula_snprintf(head, sizeof(head), "code %u", rEntry.code);
                }
                u32 at = 0;
                const auto put = [&](const char* pText) {
                    for (; *pText != 0; pText++) {
                        if (pDst != nullptr) {
                            pDst[at] = *pText;
                        }
                        at++;
                    }
                };
                put(head);
                for (u32 i = 0; i < argCount(rEntry); i++) {
                    put(" ");
                    put(rEntry.names[i] != nullptr ? rEntry.names[i] : "?");
                    put("=");
                    put(valueAt(rEntry, i));
                }
                return at;
            }

            u32 utf8Length(const char16_t* pText, u32 units) {
                u32 bytes = 0;
                for (u32 i = 0; i < units; i++) {
                    u32 c = pText[i];
                    if (c >= 0xD800u && c < 0xDC00u && i + 1 < units && pText[i + 1] >= 0xDC00u && pText[i + 1] < 0xE000u) {
                        bytes += 4;
                        i++;
                    } else {
                        bytes += c < 0x80u ? 1 : c < 0x800u ? 2 : 3;
                    }
                }
                return bytes;
            }

            // `units` of UTF-16 as UTF-8 at pDst (room for utf8Length's bytes and a terminator).
            void write8(char* pDst, const char16_t* pText, u32 units) {
                u32 at = 0;
                for (u32 i = 0; i < units; i++) {
                    u32 c = pText[i];
                    if (c >= 0xD800u && c < 0xDC00u && i + 1 < units && pText[i + 1] >= 0xDC00u && pText[i + 1] < 0xE000u) {
                        c = 0x10000u + ((c - 0xD800u) << 10) + (static_cast< u32 >(pText[++i]) - 0xDC00u);
                    } else if (c >= 0xD800u && c < 0xE000u) {
                        c = 0xFFFDu;
                    }
                    if (c < 0x80u) {
                        pDst[at++] = static_cast< char >(c);
                    } else if (c < 0x800u) {
                        pDst[at++] = static_cast< char >(0xC0u | (c >> 6));
                        pDst[at++] = static_cast< char >(0x80u | (c & 0x3Fu));
                    } else if (c < 0x10000u) {
                        pDst[at++] = static_cast< char >(0xE0u | (c >> 12));
                        pDst[at++] = static_cast< char >(0x80u | ((c >> 6) & 0x3Fu));
                        pDst[at++] = static_cast< char >(0x80u | (c & 0x3Fu));
                    } else {
                        pDst[at++] = static_cast< char >(0xF0u | (c >> 18));
                        pDst[at++] = static_cast< char >(0x80u | ((c >> 12) & 0x3Fu));
                        pDst[at++] = static_cast< char >(0x80u | ((c >> 6) & 0x3Fu));
                        pDst[at++] = static_cast< char >(0x80u | (c & 0x3Fu));
                    }
                }
                pDst[at] = 0;
            }

            void copy(char* pDst, const char* pText) {
                while (*pText != 0) {
                    *pDst++ = *pText++;
                }
                *pDst = 0;
            }

            const char* sourceName(u32 source) {
                return source < LUMA_MODLOG_SOURCE_COUNT ? cSourceNames[source] : "?";
            }

            // The words an entry gets, before it is made: whole lines in UTF-16 (played) and UTF-8 (English).
            struct Words {
                char16_t* played;     // malloc'd
                u32 playedUnits;
                char* english;        // malloc'd
                u32 englishBytes;
                bool bare;            // the code and values only: no words were found
            };

            // The entry's host sentence, when its size covers the field.
            const char* hostOf(const LumaModLogEntry& rEntry) {
                return rEntry.size >= offsetof(LumaModLogEntry, host) + sizeof(const char*) ? rEntry.host : nullptr;
            }

            const char* rowOf(const LumaModLogEntry& rEntry) {
                return rEntry.size >= offsetof(LumaModLogEntry, row) + sizeof(const char*) ? rEntry.row : nullptr;
            }

            void wordsOf(const LumaModLogEntry& rEntry, const char* pRowId, bool own, Words* pWords) {
                pWords->played = nullptr;
                pWords->english = nullptr;
                pWords->bare = false;
                const char* const pHost = rEntry.text == nullptr ? hostOf(rEntry) : nullptr;
                if (pHost != nullptr) {
                    // A host sentence (the Mods page's): translated as the settings menu translates host text; its
                    // English as given.
                    const u32 room = 4 * lengthOf(pHost) + 256;
                    pWords->played = static_cast< char16_t* >(::malloc(room * sizeof(char16_t)));
                    pWords->englishBytes = lengthOf(pHost);
                    pWords->english = static_cast< char* >(::malloc(pWords->englishBytes + 1));
                    if (pWords->played == nullptr || pWords->english == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                    }
                    pWords->playedUnits = static_cast< u32 >(NebulaSettingsMenu::translateHost(pWords->played, static_cast< s32 >(room), pHost));
                    copy(pWords->english, pHost);
                    return;
                }
                if (rEntry.text != nullptr) {
                    // The host's: already in both languages.
                    pWords->playedUnits = put8Units(nullptr, 0, rEntry.text);
                    pWords->played = static_cast< char16_t* >(::malloc((pWords->playedUnits + 1) * sizeof(char16_t)));
                    const char* const pEnglish = rEntry.english != nullptr ? rEntry.english : rEntry.text;
                    pWords->englishBytes = lengthOf(pEnglish);
                    pWords->english = static_cast< char* >(::malloc(pWords->englishBytes + 1));
                    if (pWords->played == nullptr || pWords->english == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                    }
                    put8Units(pWords->played, 0, rEntry.text);
                    pWords->played[pWords->playedUnits] = 0;
                    copy(pWords->english, pEnglish);
                    return;
                }
                // The module's own sources, and the log's own lines (code 0) whatever their source, by their row.
                // A row the host gave (luma_modlog.h `row`) is used whatever the source.
                const bool given = pRowId != nullptr && pRowId == rowOf(rEntry);
                const bool fromRow =
                    pRowId != nullptr && (rEntry.source >= LUMA_MODLOG_CODE || own || given) && NebulaText::has(pRowId);
                if (fromRow) {
                    const s32 length = NebulaText::length(pRowId);
                    char16_t* const pTemplate = static_cast< char16_t* >(::malloc((length + 1) * sizeof(char16_t)));
                    if (pTemplate == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                    }
                    NebulaText::get(pTemplate, length + 1, pRowId);
                    pWords->playedUnits = fill(nullptr, pTemplate, rEntry, false, 0);
                    pWords->played = static_cast< char16_t* >(::malloc((pWords->playedUnits + 1) * sizeof(char16_t)));
                    const char16_t* const pEnglishTemplate = NebulaText::english(pRowId);
                    const char16_t* const pUse = pEnglishTemplate != nullptr ? pEnglishTemplate : pTemplate;
                    const u32 englishUnits = fill(nullptr, pUse, rEntry, pEnglishTemplate != nullptr, 0);
                    char16_t* const pEnglish16 = static_cast< char16_t* >(::malloc((englishUnits + 1) * sizeof(char16_t)));
                    if (pWords->played == nullptr || pEnglish16 == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                    }
                    fill(pWords->played, pTemplate, rEntry, false, 0);
                    pWords->played[pWords->playedUnits] = 0;
                    fill(pEnglish16, pUse, rEntry, pEnglishTemplate != nullptr, 0);
                    pWords->englishBytes = utf8Length(pEnglish16, englishUnits);
                    pWords->english = static_cast< char* >(::malloc(pWords->englishBytes + 1));
                    if (pWords->english == nullptr) {
                        NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                    }
                    write8(pWords->english, pEnglish16, englishUnits);
                    ::free(pEnglish16);
                    ::free(pTemplate);
                    return;
                }
                // No words: the code and every value, the same in both.
                pWords->bare = true;
                pWords->englishBytes = bare(nullptr, rEntry, pRowId);
                pWords->english = static_cast< char* >(::malloc(pWords->englishBytes + 1));
                if (pWords->english == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                }
                bare(pWords->english, rEntry, pRowId);
                pWords->english[pWords->englishBytes] = 0;
                pWords->playedUnits = put8Units(nullptr, 0, pWords->english);
                pWords->played = static_cast< char16_t* >(::malloc((pWords->playedUnits + 1) * sizeof(char16_t)));
                if (pWords->played == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                }
                put8Units(pWords->played, 0, pWords->english);
                pWords->played[pWords->playedUnits] = 0;
            }

            // The code as the page names it: the host's sources "L<code>"; the module's their row id
            // ("Hook.NoResult"; a script's "S0901" without "Script."), or the number when there is no row; "" for code 0
            // (the log's own lines, a host sentence).
            void codeNameOf(const LumaModLogEntry& rEntry, const char* pRowId, char* pDst, u32 size) {
                pDst[0] = 0;
                if (rEntry.code == 0) {
                    return;
                }
                if (rEntry.source <= LUMA_MODLOG_COOK) {
                    nebula_snprintf(pDst, size, "L%u", rEntry.code);
                    return;
                }
                if (pRowId == nullptr || pRowId[0] == 0) {
                    nebula_snprintf(pDst, size, "%u", rEntry.code);
                    return;
                }
                static const char cScript[] = "Script.";
                u32 n = 0;
                while (cScript[n] != 0 && pRowId[n] == cScript[n]) {
                    n++;
                }
                nebula_snprintf(pDst, size, "%s", cScript[n] == 0 ? pRowId + n : pRowId);
            }

            // One block: the entry, its played text (UTF-16), then its mod, file, played text (UTF-8), English and code
            // name.
            // The Notes shown as a notice as an Error is (mod-log 4.1, draft 7): a page a mod opened outside the game
            // (services/system.md J-SS5), so the player coming back sees which mod it was.
            const char* const cShownNotes[] = {"System.LinkOpened"};

            bool shownNote(const char* pRowId) {
                if (pRowId == nullptr) {
                    return false;
                }
                for (const char* pShown : cShownNotes) {
                    if (same(pShown, pRowId)) {
                        return true;
                    }
                }
                return false;
            }

            // The values' part of the key: FNV-1a over the count, each value and the given words (host entries), each
            // ended by a 0 byte so ("ab","c") and ("a","bc") differ. Computed before the lock.
            u64 argsKeyOf(const LumaModLogEntry& rEntry) {
                u64 h = 0xCBF29CE484222325ull;
                const auto mix = [&h](const char* pText) {
                    for (const char* p = pText != nullptr ? pText : ""; *p != 0; p++) {
                        h = (h ^ static_cast< unsigned char >(*p)) * 0x100000001B3ull;
                    }
                    h = (h ^ 0xFFu) * 0x100000001B3ull;
                };
                const u32 count = rEntry.count < LUMA_MODLOG_ARGS ? rEntry.count : LUMA_MODLOG_ARGS;
                h = (h ^ count) * 0x100000001B3ull;
                for (u32 i = 0; i < count; i++) {
                    mix(rEntry.values[i]);
                }
                mix(rEntry.text);
                mix(rEntry.english);
                return h;
            }

            Entry* make(const LumaModLogEntry& rEntry, const char* pFile, const Words& rWords, const char* pRowId) {
                char codeName[64];
                codeNameOf(rEntry, pRowId, codeName, sizeof(codeName));
                const u32 modBytes = lengthOf(rEntry.mod) + 1;
                const u32 fileBytes = lengthOf(pFile) + 1;
                const u32 playedBytes = utf8Length(rWords.played, rWords.playedUnits) + 1;
                const u32 codeBytes = lengthOf(codeName) + 1;
                const u32 rowBytes = (pRowId != nullptr ? lengthOf(pRowId) : 0u) + 1;
                const size_t size = sizeof(Entry) + (rWords.playedUnits + 1) * sizeof(char16_t) + modBytes + fileBytes + playedBytes +
                                    rWords.englishBytes + 1 + codeBytes + rowBytes;
                unsigned char* const pBlock = static_cast< unsigned char* >(zeroed(size));
                if (pBlock == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for %s's mod-log line %u", rEntry.mod, rEntry.code);
                }
                Entry* const pEntry = reinterpret_cast< Entry* >(pBlock);
                char16_t* const pText = reinterpret_cast< char16_t* >(pBlock + sizeof(Entry));
                for (u32 i = 0; i <= rWords.playedUnits; i++) {
                    pText[i] = rWords.played[i];
                }
                char* pAt = reinterpret_cast< char* >(pText + rWords.playedUnits + 1);
                char* const pMod = pAt;
                copy(pMod, rEntry.mod);
                pAt += modBytes;
                char* const pPlace = pAt;
                copy(pPlace, pFile);
                pAt += fileBytes;
                char* const pPlayed8 = pAt;
                write8(pPlayed8, rWords.played, rWords.playedUnits);
                pAt += playedBytes;
                char* const pEnglish = pAt;
                copy(pEnglish, rWords.english);
                pAt += rWords.englishBytes + 1;
                char* const pCodeName = pAt;
                copy(pCodeName, codeName);
                pAt += codeBytes;
                char* const pRow = pAt;
                copy(pRow, pRowId != nullptr ? pRowId : "");

                pEntry->source = rEntry.source;
                pEntry->level = rEntry.level;
                pEntry->code = rEntry.code;
                pEntry->line = rEntry.line;
                pEntry->column = rEntry.column;
                pEntry->count = 1;
                pEntry->argsKey = argsKeyOf(rEntry);
                pEntry->shown = rEntry.level == LUMA_MODLOG_ERROR || shownNote(pRowId) ? 1u : 0u;
                pEntry->mod = pMod;
                pEntry->file = pPlace;
                pEntry->text = pText;
                pEntry->textUtf8 = pPlayed8;
                pEntry->english = pEnglish;
                pEntry->codeName = pCodeName;
                pEntry->row = pRow;
                pEntry->next = nullptr;
                return pEntry;
            }

            Mod* makeMod(const char* pId) {
                const u32 bytes = lengthOf(pId) + 1;
                unsigned char* const pBlock = static_cast< unsigned char* >(zeroed(sizeof(Mod) + bytes));
                if (pBlock == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for %s's mod log", pId);
                }
                Mod* const pMod = reinterpret_cast< Mod* >(pBlock);
                char* const pName = reinterpret_cast< char* >(pBlock + sizeof(Mod));
                copy(pName, pId);
                pMod->id = pName;
                return pMod;
            }

            // Under the lock.
            Mod* findMod(Store* pStore, const char* pId) {
                for (Mod* pMod = pStore->modHead; pMod != nullptr; pMod = pMod->next) {
                    if (same(pMod->id, pId)) {
                        return pMod;
                    }
                }
                return nullptr;
            }

            // Under the lock. The key is the mod, source, code, file, line, column and the values (argsKey): the same
            // mistake again is a repeat, another mistake at the same place is a line of its own. Only that mod's entries
            // are walked (at most cPerMod), so a repeat from a busy thread holds the lock briefly.
            Entry* findEntry(Store* pStore, const LumaModLogEntry& rEntry, const char* pFile, u64 argsKey) {
                const Mod* const pMod = findMod(pStore, rEntry.mod);
                for (Entry* pEntry = pMod != nullptr ? pMod->entries : nullptr; pEntry != nullptr; pEntry = pEntry->nextOfMod) {
                    if (pEntry->code == rEntry.code && pEntry->source == rEntry.source && pEntry->line == rEntry.line &&
                        pEntry->column == rEntry.column && pEntry->argsKey == argsKey && same(pEntry->file, pFile)) {
                        return pEntry;
                    }
                }
                return nullptr;
            }

            bool canKeep(const Store* pStore, const Mod* pMod) {
                return pMod->kept < cPerMod && (pStore->kept < cSession || pMod->kept < cFirstKept);
            }

            void linkMod(Store* pStore, Mod* pMod) {
                if (pStore->modTail == nullptr) {
                    __atomic_store_n(&pStore->modHead, pMod, __ATOMIC_RELEASE);
                } else {
                    __atomic_store_n(&pStore->modTail->next, pMod, __ATOMIC_RELEASE);
                }
                pStore->modTail = pMod;
            }

            void bump(Store* pStore) { __atomic_add_fetch(&pStore->revision, 1u, __ATOMIC_RELEASE); }

            // True for the one caller that raises *pSaid to `count`: a fact is written once a count and never for a count
            // below one already written (session's replay racing a keep, two repeats finishing out of order).
            bool claimFact(u32* pSaid, u32 count) {
                u32 seen = __atomic_load_n(pSaid, __ATOMIC_RELAXED);
                while (seen < count) {
                    if (__atomic_compare_exchange_n(pSaid, &seen, count, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                        return true;
                    }
                }
                return false;
            }

            // The JSON of a kept entry for the harness, at its count.
            void entryFact(const Entry* pEntry, u32 count) {
                if (!facts() || !claimFact(&const_cast< Entry* >(pEntry)->factCount, count)) {
                    return;
                }
                const u32 room = 256 + 6 * (lengthOf(pEntry->mod) + lengthOf(pEntry->file) + lengthOf(pEntry->textUtf8) +
                                            lengthOf(pEntry->english) + lengthOf(pEntry->row));
                char* const pJson = static_cast< char* >(::malloc(room));
                char* const pPart = static_cast< char* >(::malloc(room));
                if (pJson == nullptr || pPart == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for a mod-log fact");
                }
                u32 at = 0;
                const auto put = [&](const char* pText) {
                    for (; *pText != 0 && at + 1 < room; pText++) {
                        pJson[at++] = *pText;
                    }
                    pJson[at] = 0;
                };
                char number[96];
                put("{\"mod\":");
                jsonString(pPart, room, pEntry->mod);
                put(pPart);
                nebula_snprintf(number, sizeof(number), ",\"source\":\"%s\",\"level\":\"%s\",\"code\":%u,\"file\":",
                                sourceName(pEntry->source), pEntry->level == LUMA_MODLOG_ERROR ? "error" : "note", pEntry->code);
                put(number);
                jsonString(pPart, room, pEntry->file);
                put(pPart);
                nebula_snprintf(number, sizeof(number), ",\"line\":%u,\"column\":%u,\"text\":", pEntry->line, pEntry->column);
                put(number);
                jsonString(pPart, room, pEntry->textUtf8);
                put(pPart);
                put(",\"english\":");
                jsonString(pPart, room, pEntry->english);
                put(pPart);
                if (pEntry->row[0] != 0) {
                    // The row its words came from, so a test names the line it means whatever the language (mod-log 4.3).
                    put(",\"row\":");
                    jsonString(pPart, room, pEntry->row);
                    put(pPart);
                }
                nebula_snprintf(number, sizeof(number), ",\"count\":%u}", count);
                put(number);
                fact("modlog", pJson);
                ::free(pPart);
                ::free(pJson);
            }

            // ModLog.TooMany as a fact: the mod and how many were not kept.
            void pastFact(const Mod* pMod, u32 past) {
                if (!facts() || !claimFact(&const_cast< Mod* >(pMod)->factPast, past)) {
                    return;
                }
                const u32 room = 128 + 6 * lengthOf(pMod->id);
                char* const pJson = static_cast< char* >(::malloc(room));
                char* const pPart = static_cast< char* >(::malloc(room));
                if (pJson == nullptr || pPart == nullptr) {
                    NEBULA_PANIC("LumaCore: no memory for a mod-log fact");
                }
                jsonString(pPart, room, pMod->id);
                nebula_snprintf(pJson, room, "{\"mod\":%s,\"row\":\"ModLog.TooMany\",\"level\":\"note\",\"count\":%u}", pPart, past);
                fact("modlog", pJson);
                ::free(pPart);
                ::free(pJson);
            }

            Entry* keep(const LumaModLogEntry& rEntry, const char* pRowId, bool own);

            // The log's own Notes and Errors about an entry (ModLog.ArgsCut, ModLog.NoText): once per key.
            void addOwn(const char* pMod, u32 source, u32 level, const char* pRow) {
                LumaModLogEntry own = {};
                own.size = sizeof(own);
                own.source = source;
                own.level = level;
                own.code = 0;
                own.mod = pMod;
                own.file = pRow;  // their row is the key's file, so each is said once a mod and source
                own.names[0] = "mod";
                own.values[0] = pMod;
                own.count = 1;
                keep(own, pRow, true);
            }
        }  // namespace

        const Entry* add(const LumaModLogEntry& rEntry, const char* pRowId) {
            return keep(rEntry, pRowId, false);
        }

        void counted(const Entry* pEntry, u32 count) {
            if (pEntry == nullptr) {
                return;
            }
            Entry* const pKept = const_cast< Entry* >(pEntry);
            u32 now = __atomic_load_n(&pKept->count, __ATOMIC_RELAXED);
            while (now < count) {
                if (__atomic_compare_exchange_n(&pKept->count, &now, count, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                    bump(store());
                    entryFact(pKept, count);
                    return;
                }
            }
        }

        namespace {
        // `own`: one of the log's own lines (addOwn), which may be a Note of any source and writes its own log line.
        Entry* keep(const LumaModLogEntry& rEntry, const char* pRowId, bool own) {
            // Section 6: the caller's mistakes are Nebula's bugs; nothing is kept, the game's log names them.
            if (rEntry.size < LUMA_MODLOG_ENTRY_SIZE || rEntry.mod == nullptr || rEntry.source >= LUMA_MODLOG_SOURCE_COUNT ||
                rEntry.level > LUMA_MODLOG_NOTE) {
                nebula_log("LumaCore: a mod-log entry with size %u, mod %s, source %u, level %u, code %u is not kept (a bug in "
                           "its caller)\n",
                           rEntry.size, rEntry.size >= LUMA_MODLOG_ENTRY_SIZE && rEntry.mod != nullptr ? rEntry.mod : "(none)",
                           rEntry.source, rEntry.level, rEntry.code);
                return nullptr;
            }
            // Section 3.1: the module's own sources are Errors (the log's own lines are as they say, and so are the host's
            // entries that give their row: luma_modlog.h `row`, e.g. services/system.md's System.LinkOpened Note).
            if (rEntry.source >= LUMA_MODLOG_CODE && !own && rowOf(rEntry) == nullptr && rEntry.level != LUMA_MODLOG_ERROR) {
                nebula_log("LumaCore: mod-log entry %s %s %u given as a Note; kept as an Error (a bug in its caller)\n", rEntry.mod,
                           sourceName(rEntry.source), rEntry.code);
                LumaModLogEntry asError = rEntry;
                asError.level = LUMA_MODLOG_ERROR;
                return keep(asError, pRowId, own);
            }
            const char* const pFile = rEntry.file != nullptr ? rEntry.file : "";
            Store* const pStore = store();
            const u64 argsKey = argsKeyOf(rEntry);

            // A repeat, or past the limits: counted, nothing made.
            lockStore(pStore);
            if (Entry* pFound = findEntry(pStore, rEntry, pFile, argsKey)) {
                const u32 count = __atomic_add_fetch(&pFound->count, 1u, __ATOMIC_RELAXED);
                bump(pStore);
                unlockStore(pStore);
                entryFact(pFound, count);
                return pFound;
            }
            Mod* pMod = findMod(pStore, rEntry.mod);
            if (pMod != nullptr && !canKeep(pStore, pMod)) {
                const u32 past = __atomic_add_fetch(&pMod->past, 1u, __ATOMIC_RELAXED);
                bump(pStore);
                unlockStore(pStore);
                pastFact(pMod, past);
                return nullptr;
            }
            unlockStore(pStore);

            // Made outside the lock; another thread may keep the same key meanwhile (looked up again below).
            Words words;
            wordsOf(rEntry, pRowId, own, &words);
            Entry* pMade = make(rEntry, pFile, words, pRowId);
            Mod* pNewMod = pMod == nullptr ? makeMod(rEntry.mod) : nullptr;
            ::free(words.played);
            ::free(words.english);

            lockStore(pStore);
            if (Entry* pFound = findEntry(pStore, rEntry, pFile, argsKey)) {
                const u32 count = __atomic_add_fetch(&pFound->count, 1u, __ATOMIC_RELAXED);
                bump(pStore);
                unlockStore(pStore);
                ::free(pMade);
                ::free(pNewMod);
                entryFact(pFound, count);
                return pFound;
            }
            pMod = findMod(pStore, rEntry.mod);
            if (pMod == nullptr) {
                pMod = pNewMod;
                pNewMod = nullptr;
                linkMod(pStore, pMod);
            }
            if (!canKeep(pStore, pMod)) {
                const u32 past = __atomic_add_fetch(&pMod->past, 1u, __ATOMIC_RELAXED);
                bump(pStore);
                unlockStore(pStore);
                ::free(pMade);
                ::free(pNewMod);
                pastFact(pMod, past);
                return nullptr;
            }
            pMade->reload = pMod->reload;
            if (pStore->tail == nullptr) {
                __atomic_store_n(&pStore->head, pMade, __ATOMIC_RELEASE);
            } else {
                __atomic_store_n(&pStore->tail->next, pMade, __ATOMIC_RELEASE);
            }
            pStore->tail = pMade;
            if (pMod->lastEntry == nullptr) {
                pMod->entries = pMade;
            } else {
                pMod->lastEntry->nextOfMod = pMade;
            }
            pMod->lastEntry = pMade;
            pMod->kept++;
            __atomic_add_fetch(&pStore->kept, 1u, __ATOMIC_RELAXED);
            if (pMade->level == LUMA_MODLOG_ERROR) {
                __atomic_add_fetch(&pStore->errors, 1u, __ATOMIC_RELAXED);
            }
            bump(pStore);
            unlockStore(pStore);
            ::free(pNewMod);

            // The game's log has every line once: CODE's are the kernel's own (record), the host's are the host's; the
            // content's and the log's own are written here.
            if (pMade->source == LUMA_MODLOG_CONTENT || own) {
                if (pMade->line != 0) {
                    nebula_log("[mod %s] %s:%u: %s\n", pMade->mod, pMade->file, pMade->line, pMade->english);
                } else if (pMade->file[0] != 0 && !own) {
                    nebula_log("[mod %s] %s: %s\n", pMade->mod, pMade->file, pMade->english);
                } else {
                    nebula_log("[mod %s] %s\n", pMade->mod, pMade->english);
                }
            }
            entryFact(pMade, 1);

            if (rEntry.count > LUMA_MODLOG_ARGS) {
                addOwn(rEntry.mod, rEntry.source, LUMA_MODLOG_NOTE, cArgsCut);
            }
            if (words.bare && rEntry.text == nullptr && rEntry.source <= LUMA_MODLOG_COOK && !own) {
                addOwn(cNebula, rEntry.source, LUMA_MODLOG_ERROR, cNoText);
            }
            return pMade;
        }
        }  // namespace

        void session(u32 modsOn, LumaModLogFact pFact) {
            __atomic_store_n(&sModsOn, modsOn, __ATOMIC_RELEASE);
            __atomic_store_n(&sFact, pFact, __ATOMIC_RELEASE);
            // What was kept before the writer came: its facts now, at each one's count.
            if (pFact != nullptr) {
                for (const Mod* pMod = firstMod(); pMod != nullptr; pMod = nextOf(pMod)) {
                    if (pastOf(pMod) != 0) {
                        pastFact(pMod, pastOf(pMod));
                    }
                }
                for (const Entry* pEntry = first(); pEntry != nullptr; pEntry = nextOf(pEntry)) {
                    entryFact(pEntry, countOf(pEntry));
                }
            }
        }

        void reloaded(const char* pModId) {
            Store* const pStore = storeIfAny();
            if (pStore == nullptr || pModId == nullptr) {
                return;
            }
            lockStore(pStore);
            if (Mod* pMod = findMod(pStore, pModId)) {
                pMod->reload++;
            }
            unlockStore(pStore);
        }

        bool wanted() { return __atomic_load_n(&sModsOn, __ATOMIC_ACQUIRE) != 0 || storeIfAny() != nullptr; }

        bool allocated() { return storeIfAny() != nullptr; }

        u32 revision() {
            const Store* const pStore = storeIfAny();
            return pStore != nullptr ? __atomic_load_n(&pStore->revision, __ATOMIC_ACQUIRE) : 0;
        }

        const Entry* first() {
            const Store* const pStore = storeIfAny();
            return pStore != nullptr ? __atomic_load_n(&pStore->head, __ATOMIC_ACQUIRE) : nullptr;
        }

        const Mod* firstMod() {
            const Store* const pStore = storeIfAny();
            return pStore != nullptr ? __atomic_load_n(&pStore->modHead, __ATOMIC_ACQUIRE) : nullptr;
        }

        const Entry* nextOf(const Entry* pEntry) { return __atomic_load_n(&pEntry->next, __ATOMIC_ACQUIRE); }

        const Mod* nextOf(const Mod* pMod) { return __atomic_load_n(&pMod->next, __ATOMIC_ACQUIRE); }

        u32 countOf(const Entry* pEntry) { return __atomic_load_n(&pEntry->count, __ATOMIC_RELAXED); }

        u32 pastOf(const Mod* pMod) { return __atomic_load_n(&pMod->past, __ATOMIC_RELAXED); }

        u32 kept() {
            const Store* const pStore = storeIfAny();
            return pStore != nullptr ? __atomic_load_n(&pStore->kept, __ATOMIC_RELAXED) : 0;
        }

        u32 errors() {
            const Store* const pStore = storeIfAny();
            return pStore != nullptr ? __atomic_load_n(&pStore->errors, __ATOMIC_RELAXED) : 0;
        }

        const Entry* takeNotice() {
            Store* const pStore = storeIfAny();
            if (pStore == nullptr) {
                return nullptr;
            }
            for (;;) {
                const Entry* const pNext = pStore->noticed != nullptr ? nextOf(pStore->noticed) : first();
                if (pNext == nullptr) {
                    return nullptr;
                }
                pStore->noticed = pNext;
                if (pNext->shown != 0) {
                    return pNext;
                }
            }
        }

        u32 noticesWaiting() {
            const Store* const pStore = storeIfAny();
            if (pStore == nullptr) {
                return 0;
            }
            u32 waiting = 0;
            for (const Entry* pEntry = pStore->noticed != nullptr ? nextOf(pStore->noticed) : first(); pEntry != nullptr;
                 pEntry = nextOf(pEntry)) {
                waiting += pEntry->shown != 0 ? 1u : 0u;
            }
            return waiting;
        }

        u32 takeSummary() {
            Store* const pStore = storeIfAny();
            if (pStore == nullptr || pStore->summaryTaken != 0) {
                return 0;
            }
            pStore->summaryTaken = 1;
            u32 summary = 0;
            while (takeNotice() != nullptr) {
                summary++;
            }
            return summary;
        }

        bool facts() { return __atomic_load_n(&sFact, __ATOMIC_ACQUIRE) != nullptr; }

        void fact(const char* pKey, const char* pJson) {
            const LumaModLogFact pFact = __atomic_load_n(&sFact, __ATOMIC_ACQUIRE);
            if (pFact != nullptr) {
                pFact(pKey, pJson);
            }
        }

        void jsonString(char* pDst, u32 size, const char* pText) {
            if (size < 3) {
                if (size != 0) {
                    pDst[0] = 0;
                }
                return;
            }
            u32 at = 0;
            pDst[at++] = '"';
            for (const unsigned char* p = reinterpret_cast< const unsigned char* >(pText); *p != 0; p++) {
                char escaped[8];
                u32 n = 0;
                if (*p == '"' || *p == '\\') {
                    escaped[n++] = '\\';
                    escaped[n++] = static_cast< char >(*p);
                } else if (*p < 0x20u) {
                    nebula_snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                    n = 6;
                } else {
                    escaped[n++] = static_cast< char >(*p);
                }
                // No room for this byte and the closing quote: the string ends at the last whole character (a
                // continuation byte that does not fit takes its character's earlier bytes out too).
                if (at + n + 2 > size) {
                    if ((*p & 0xC0u) == 0x80u) {
                        while (at > 1 && (static_cast< unsigned char >(pDst[at - 1]) & 0xC0u) == 0x80u) {
                            at--;
                        }
                        if (at > 1 && static_cast< unsigned char >(pDst[at - 1]) >= 0xC0u) {
                            at--;
                        }
                    }
                    break;
                }
                for (u32 i = 0; i < n; i++) {
                    pDst[at++] = escaped[i];
                }
            }
            pDst[at++] = '"';
            pDst[at] = 0;
        }

        void toUtf8(char* pDst, u32 size, const char16_t* pText) {
            if (size == 0) {
                return;
            }
            u32 units = lengthOf16(pText);
            while (units > 0 && utf8Length(pText, units) + 1 > size) {
                units--;
                if (units > 0 && pText[units - 1] >= 0xD800u && pText[units - 1] < 0xDC00u) {
                    units--;  // never half a pair
                }
            }
            write8(pDst, pText, units);
        }

        u32 words(const char* pRow, const char* pMod, u32 count, char* pDst, u32 size) {
            char number[16];
            nebula_snprintf(number, sizeof(number), "%u", count);
            LumaModLogEntry values = {};
            values.size = sizeof(values);
            values.mod = pMod != nullptr ? pMod : "";
            values.names[0] = "count";
            values.values[0] = number;
            values.count = 1;
            return rowWords(pRow, values, pDst, size);
        }

        u32 rowWords(const char* pRow, const LumaModLogEntry& rValues, char* pDst, u32 size) {
            if (pDst == nullptr || size == 0) {
                return 0;
            }
            pDst[0] = 0;
            if (pRow == nullptr || !NebulaText::has(pRow)) {
                return 0;
            }
            const s32 length = NebulaText::length(pRow);
            char16_t* const pTemplate = static_cast< char16_t* >(::malloc((length + 1) * sizeof(char16_t)));
            if (pTemplate == nullptr) {
                NEBULA_PANIC("LumaCore: no memory for the row %s", pRow);
            }
            NebulaText::get(pTemplate, length + 1, pRow);
            // Only the fields fill reads; the caller's size is not trusted beyond them.
            LumaModLogEntry values = {};
            values.size = sizeof(values);
            values.mod = rValues.mod != nullptr ? rValues.mod : "";
            values.count = rValues.count < LUMA_MODLOG_ARGS ? rValues.count : LUMA_MODLOG_ARGS;
            for (u32 i = 0; i < values.count; i++) {
                values.names[i] = rValues.names[i] != nullptr ? rValues.names[i] : "";
                values.values[i] = rValues.values[i] != nullptr ? rValues.values[i] : "";
            }
            const u32 units = fill(nullptr, pTemplate, values, false, 0);
            char16_t* const pText = static_cast< char16_t* >(::malloc((units + 1) * sizeof(char16_t)));
            if (pText == nullptr) {
                NEBULA_PANIC("LumaCore: no memory for the mod log's row %s", pRow);
            }
            fill(pText, pTemplate, values, false, 0);
            pText[units] = 0;
            toUtf8(pDst, size, pText);
            ::free(pText);
            ::free(pTemplate);
            return lengthOf(pDst);
        }
    }  // namespace modlog
}  // namespace luma

extern "C" {
NEBULA_GAME_API void luma_modlog_session(uint32_t mods_on, LumaModLogFact fact) {
    luma::modlog::session(mods_on, fact);
}

NEBULA_GAME_API void luma_modlog_add(const LumaModLogEntry* entry) {
    if (entry == nullptr) {
        nebula_log("LumaCore: luma_modlog_add(null) (a bug in its caller)\n");
        return;
    }
    // The host's own row for the words, when it gives one (luma_modlog.h `row`).
    luma::modlog::add(*entry, entry->size >= offsetof(LumaModLogEntry, row) + sizeof(const char*) ? entry->row : nullptr);
}

NEBULA_GAME_API const void* luma_modlog_next(const void* after, LumaModLogView* view) {
    const luma::modlog::Entry* const pEntry =
        after == nullptr ? luma::modlog::first() : luma::modlog::nextOf(static_cast< const luma::modlog::Entry* >(after));
    if (pEntry == nullptr || view == nullptr) {
        return pEntry;
    }
    // As much as the caller's struct holds, whole fields only: a host built against an earlier view reads only what it
    // knows.
    LumaModLogView whole = {};
    whole.size = sizeof(whole);
    whole.source = pEntry->source;
    whole.level = pEntry->level;
    whole.code = pEntry->code;
    whole.line = pEntry->line;
    whole.column = pEntry->column;
    whole.count = luma::modlog::countOf(pEntry);
    whole.reload = pEntry->reload;
    whole.mod = pEntry->mod;
    whole.file = pEntry->file;
    whole.text = pEntry->textUtf8;
    whole.english = pEntry->english;
    whole.code_name = pEntry->codeName;
    const u32 numbers = static_cast< u32 >(offsetof(LumaModLogView, mod));  // the uint32_t fields
    u32 size = view->size < sizeof(whole) ? view->size : static_cast< u32 >(sizeof(whole));
    size = size <= numbers ? size & ~3u : numbers + ((size - numbers) & ~static_cast< u32 >(sizeof(const char*) - 1));
    const u32 given = view->size;
    memcpy(view, &whole, size);
    view->size = given;
    return pEntry;
}

NEBULA_GAME_API const void* luma_modlog_next_mod(const void* after, const char** id, uint32_t* past) {
    const luma::modlog::Mod* const pMod =
        after == nullptr ? luma::modlog::firstMod() : luma::modlog::nextOf(static_cast< const luma::modlog::Mod* >(after));
    if (pMod != nullptr) {
        if (id != nullptr) {
            *id = pMod->id;
        }
        if (past != nullptr) {
            *past = luma::modlog::pastOf(pMod);
        }
    }
    return pMod;
}

NEBULA_GAME_API uint32_t luma_modlog_words(const char* row, const char* mod, uint32_t count, char* out, uint32_t size) {
    return luma::modlog::words(row, mod, count, out, size);
}

NEBULA_GAME_API uint32_t luma_modlog_row_words(const LumaModLogEntry* values, const char* row, char* out, uint32_t size) {
    if (values == nullptr) {
        LumaModLogEntry none = {};
        return luma::modlog::rowWords(row, none, out, size);
    }
    return luma::modlog::rowWords(row, *values, out, size);
}
}
