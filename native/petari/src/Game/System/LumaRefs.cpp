#include "Game/System/LumaRefs.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <luma_modlog.h>
#include <luma_refs.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stdlib.h>
#include <string.h>

// Contract references, section 3 ("After start") and 4 (the object class's placeholder). The host's one call makes one
// block, published by one release store: the game thread reads it with an acquire load and no lock. It lives until the
// module unloads.

namespace luma {
    namespace refs {
        namespace {
            // The placeholder model, cooked from Nebula's data/placeholder/model.gltf into the store as an object archive
            // (references 4; G's cook list). 3.13 refuses a start without it.
            const char cPlaceholderArchive[] = "NebulaPlaceholder";
            const char cMissingClassRow[] = "Content.MissingClass";
            const u32 cMissingClassCode = 620;

            struct Missing {
                const char* mod;
                const char* className;
                const char* file;
                u32 line;
                u32 placed;  // its placeholder was made once (the `placeholder` fact is written then); game thread
            };

            struct Table {
                u32 count;
                Missing* items;
            };

            // File-scope and constant-initialised (the module has no function-local statics with run-time init).
            Table* sTable = nullptr;
            bool sArchiveMissingSaid = false;  // game thread

            Table* tableIfAny() { return __atomic_load_n(&sTable, __ATOMIC_ACQUIRE); }

            u32 bytesOf(const char* pText) { return static_cast< u32 >(strlen(pText != nullptr ? pText : "")) + 1; }

            char* copyInto(char*& rpAt, const char* pText) {
                const u32 bytes = bytesOf(pText);
                char* const pCopy = rpAt;
                memcpy(pCopy, pText != nullptr ? pText : "", bytes);
                rpAt += bytes;
                return pCopy;
            }

            // The placeholder object (references 4): the placeholder model at the placement's position; no behaviour, no
            // collision, no sensor.
            class PlaceholderObj : public LiveActor {
            public:
                PlaceholderObj(const char* pName) : LiveActor(pName) {}

                virtual void init(const TableIter& rIter) {
                    if (rIter.isValid()) {
                        MR::initDefaultPos(this, rIter);
                    }
                    if (MR::isExistObjectOrLayoutArchive(cPlaceholderArchive)) {
                        initModelManagerWithAnm(cPlaceholderArchive, nullptr, false);
                        MR::connectToSceneMapObj(this);
                    } else {
                        // 3.13 refuses a start without Nebula's placeholder data; reaching here is Nebula's bug.
                        if (!sArchiveMissingSaid) {
                            sArchiveMissingSaid = true;
                            nebula_log("refs: the placeholder archive %s is not in the store (3.13 should have refused the start)\n",
                                       cPlaceholderArchive);
                        }
                        MR::connectToSceneMapObjMovement(this);
                    }
                    MR::setClippingTypeSphere(this, 100.0f);
                    makeActorAppeared();
                }
            };

            NameObj* createPlaceholder(const char* pName) { return new PlaceholderObj(pName); }

            // The harness's `placeholder` fact, the first time a missing class is placed.
            void placedFact(const Missing& rMissing) {
                if (!luma::modlog::facts()) {
                    return;
                }
                char mod[160];
                char found[160];
                char file[300];
                luma::modlog::jsonString(mod, sizeof(mod), rMissing.mod);
                luma::modlog::jsonString(found, sizeof(found), rMissing.className);
                luma::modlog::jsonString(file, sizeof(file), rMissing.file);
                char json[700];
                nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"kind\":\"class\",\"found\":%s,\"file\":%s,\"line\":%u}", mod, found,
                                file, rMissing.line);
                luma::modlog::fact("placeholder", json);
            }
        }  // namespace

        CreatorFuncPtr placeholderFor(const char* pClassName) {
            Table* const pTable = tableIfAny();
            if (pTable == nullptr || pClassName == nullptr) {
                return nullptr;
            }
            bool found = false;
            for (u32 i = 0; i < pTable->count; i++) {
                Missing& rMissing = pTable->items[i];
                if (!MR::isEqualStringCase(rMissing.className, pClassName)) {
                    continue;
                }
                // One fact a class, naming its first placement row the check was given.
                if (!found && rMissing.placed == 0) {
                    placedFact(rMissing);
                }
                rMissing.placed = 1;
                found = true;
            }
            return found ? createPlaceholder : nullptr;
        }

        u32 missingCount() {
            const Table* const pTable = tableIfAny();
            return pTable != nullptr ? pTable->count : 0;
        }

        // luma_refs_check_after_start.
        void checkAfterStart(const LumaAfterStartRef* pRefs, u32 count) {
            if (tableIfAny() != nullptr) {
                nebula_log("refs: luma_refs_check_after_start called again (%u rows); the first call's answer stays\n", count);
                return;
            }
            if (pRefs == nullptr && count != 0) {
                nebula_log("refs: luma_refs_check_after_start given no rows but a count of %u; nothing checked\n", count);
                count = 0;
            }
            // Which rows name a class nobody makes, and how many bytes their copies take.
            u32 missing = 0;
            size_t bytes = sizeof(Table);
            for (u32 i = 0; i < count; i++) {
                const LumaAfterStartRef& rRef = pRefs[i];
                if (rRef.mod == nullptr || rRef.class_name == nullptr || NameObjFactory::getCreator(rRef.class_name) != nullptr) {
                    if (rRef.mod == nullptr || rRef.class_name == nullptr) {
                        nebula_log("refs: after-start row %u has no %s; left out\n", i, rRef.mod == nullptr ? "mod" : "class");
                    }
                    continue;
                }
                missing++;
                bytes += sizeof(Missing) + bytesOf(rRef.mod) + bytesOf(rRef.class_name) + bytesOf(rRef.file);
            }
            unsigned char* const pBlock = static_cast< unsigned char* >(::malloc(bytes));
            if (pBlock == nullptr) {
                NEBULA_PANIC("LumaCore: no memory for the after-start check (%u missing classes)", missing);
            }
            memset(pBlock, 0, bytes);
            Table* const pTable = reinterpret_cast< Table* >(pBlock);
            pTable->items = reinterpret_cast< Missing* >(pBlock + sizeof(Table));
            char* pAt = reinterpret_cast< char* >(pTable->items + missing);
            for (u32 i = 0; i < count; i++) {
                const LumaAfterStartRef& rRef = pRefs[i];
                if (rRef.mod == nullptr || rRef.class_name == nullptr || NameObjFactory::getCreator(rRef.class_name) != nullptr) {
                    continue;
                }
                Missing& rMissing = pTable->items[pTable->count++];
                rMissing.mod = copyInto(pAt, rRef.mod);
                rMissing.className = copyInto(pAt, rRef.class_name);
                rMissing.file = copyInto(pAt, rRef.file);
                rMissing.line = rRef.line;

                // One Error in the mod log a placement (its key: mod, CONTENT, 620, file, line).
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CONTENT;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = cMissingClassCode;
                entry.mod = rMissing.mod;
                entry.file = rMissing.file;
                entry.line = rMissing.line;
                entry.names[0] = "file";
                entry.values[0] = rMissing.file;
                entry.names[1] = "found";
                entry.values[1] = rMissing.className;
                entry.count = 2;
                luma::modlog::add(entry, cMissingClassRow);
            }
            __atomic_store_n(&sTable, pTable, __ATOMIC_RELEASE);
            if (luma::modlog::facts()) {
                char json[64];
                nebula_snprintf(json, sizeof(json), "{\"count\":%u,\"missing\":%u}", count, pTable->count);
                luma::modlog::fact("refs.after_start", json);
            }
        }
    }  // namespace refs
}  // namespace luma

extern "C" {
NEBULA_GAME_API void luma_refs_check_after_start(const LumaAfterStartRef* refs, uint32_t count) {
    luma::refs::checkAfterStart(refs, count);
}
}
