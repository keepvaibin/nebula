// Objects a mod makes by name (LumaMake.hpp; contract classes "make", plan 1.3).
//
// Made as PlacementInfoOrdered makes a placed object: the factory's creator for the name (a mod's class, a mod's object
// and a mod's change of the placed name as for a placement), then its init with a placement row. The row is a table the
// kernel writes in the scene's memory (one per call, a row per object): name, pos, dir, scale and Obj_arg0 to 7 alone,
// so every other column reads as absent (no switch, no link, no rail, no view group), as for an object made with no
// placement. Its objects stand in the main stage (zone 0) and live as long as the scene, as placed ones do.
#include "Game/System/LumaMake.hpp"

#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaHook.hpp"
#include "Game/System/LumaLoader.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/Table.hpp"

#include <nebula_format.h>
#include <new>

namespace luma {
    namespace make {
        namespace {
            constexpr u32 kMost = 1024;  // objects in one call
            // A made table's two reserved words (the importer cooks them 0): StageDataHolder knows it by them.
            constexpr u32 kMark0 = 0x4C554D41u;  // "LUMA"
            constexpr u32 kMark1 = 0x4D414B45u;  // "MAKE"

            enum Column : u32 {
                kName,
                kPosX,
                kPosY,
                kPosZ,
                kDirX,
                kDirY,
                kDirZ,
                kScaleX,
                kScaleY,
                kScaleZ,
                kArg0,
                kColumns = kArg0 + 8,
            };
            const char* const cColumnNames[kColumns] = {"name",    "pos_x",   "pos_y",    "pos_z",    "dir_x",    "dir_y",
                                                        "dir_z",   "scale_x", "scale_y",  "scale_z",  "Obj_arg0", "Obj_arg1",
                                                        "Obj_arg2", "Obj_arg3", "Obj_arg4", "Obj_arg5", "Obj_arg6", "Obj_arg7"};

            __attribute__((format(printf, 2, 3))) void say(const LumaMod* pMod, const char* pFormat, ...) {
                char text[512];
                __builtin_va_list args;
                __builtin_va_start(args, pFormat);
                nebula_vsnprintf(text, sizeof(text), pFormat, args);
                __builtin_va_end(args);
                luma::core()->log(pMod, text);
            }

            u32 bitsOf(f32 value) {
                u32 bits;
                __builtin_memcpy(&bits, &value, sizeof(bits));
                return bits;
            }

            bool finite(const f32* pValues, u32 count) {
                for (u32 i = 0; i < count; i++) {
                    if (!__builtin_isfinite(pValues[i])) {
                        return false;
                    }
                }
                return true;
            }

            // The place of object i: the given one, or the origin, unturned, scale 1, every argument -1.
            void placeOf(const LumaMakePlace* pPlace, u32 i, LumaMakePlace* pOut) {
                if (pPlace == nullptr) {
                    *pOut = LumaMakePlace{};
                    pOut->scale[0] = pOut->scale[1] = pOut->scale[2] = 1.0f;
                    for (s32& rArg : pOut->args) {
                        rArg = -1;
                    }
                    return;
                }
                __builtin_memcpy(pOut, reinterpret_cast<const u8*>(pPlace) + static_cast<u64>(i) * pPlace->size, sizeof(LumaMakePlace));
            }

            // The rows' table in the scene's memory: a TableData as the importer cooks one, then its Table. Null: none
            // left. Its result is written const void* (the game's ::Table): this function's hook row is generated
            // inside namespace luma, where the type text "const Table *" names luma::Table, and the row did not
            // compile (the integration build, 11-12).
            const void* writeTable(const char* pName, u32 count, const LumaMakePlace* pPlace) {
                u32 nameBytes = 0;
                while (pName[nameBytes] != 0) {
                    nameBytes++;
                }
                const u32 rowsAt = sizeof(TableData) + kColumns * sizeof(TableColumn);
                const u32 textAt = rowsAt + count * kColumns * sizeof(u32);
                const u32 tableAt = (textAt + nameBytes + 1 + 7u) & ~7u;
                u8* const pBlock = static_cast<u8*>(NebulaArena::alloc(tableAt + sizeof(::Table), 8, nullptr));
                if (pBlock == nullptr) {
                    return nullptr;
                }
                TableData* const pData = reinterpret_cast<TableData*>(pBlock);
                __builtin_memcpy(pData->mMagic, "NTBL", 4);
                pData->mRows = count;
                pData->mColumns = kColumns;
                pData->mRowsAt = rowsAt;
                pData->mTextAt = textAt;
                pData->mTextSize = nameBytes + 1;
                pData->mReserved[0] = kMark0;
                pData->mReserved[1] = kMark1;
                TableColumn* const pColumns = reinterpret_cast<TableColumn*>(pData + 1);
                for (u32 c = 0; c < kColumns; c++) {
                    pColumns[c].mHash = MR::getHashCode(cColumnNames[c]);
                    pColumns[c].mMask = 0xFFFFFFFFu;
                    pColumns[c].mKind = c == kName ? TableKind_Text : c < kArg0 ? TableKind_Float : TableKind_Int;
                    pColumns[c].mWidth = 32;
                }
                u32* const pCells = reinterpret_cast<u32*>(pBlock + rowsAt);
                for (u32 i = 0; i < count; i++) {
                    LumaMakePlace place;
                    placeOf(pPlace, i, &place);
                    u32* const pRow = pCells + i * kColumns;
                    pRow[kName] = 0;  // the name, at the text's start
                    for (u32 k = 0; k < 3; k++) {
                        pRow[kPosX + k] = bitsOf(place.pos[k]);
                        pRow[kDirX + k] = bitsOf(place.rot[k]);
                        pRow[kScaleX + k] = bitsOf(place.scale[k]);
                    }
                    for (u32 k = 0; k < 8; k++) {
                        pRow[kArg0 + k] = static_cast<u32>(place.args[k]);
                    }
                }
                char* const pText = reinterpret_cast<char*>(pBlock + textAt);
                __builtin_memcpy(pText, pName, nameBytes + 1);
                ::Table* const pTable = new (pBlock + tableAt) ::Table();
                pTable->attach(pData);
                pTable->setName(pText);
                return pTable;
            }

            // Restores the placement zone the caller had (another placement's, or none) when the call ends.
            struct InMainStage {
                s32 mBefore;
                InMainStage() : mBefore(MR::getCurrentPlacementZoneId()) { MR::setCurrentPlacementZoneId(0); }
                ~InMainStage() {
                    if (mBefore < 0) {
                        MR::clearCurrentPlacementZoneId();
                    } else {
                        MR::setCurrentPlacementZoneId(mBefore);
                    }
                }
            };
        }  // namespace

        bool isMadeTable(const TableData* pData) {
            return pData != nullptr && pData->mReserved[0] == kMark0 && pData->mReserved[1] == kMark1;
        }

        s32 make(const LumaMod* pMod, const char* pName, u32 count, const LumaMakePlace* pPlace, u32 flags, void** ppObjects) {
            if (!onGameThread()) {
                return -3;
            }
            if (pName == nullptr || pName[0] == 0 || count == 0 || count > kMost || (flags & ~LUMA_MAKE_DEAD) != 0 ||
                (pPlace != nullptr && pPlace->size < sizeof(LumaMakePlace))) {
                say(pMod, "make %s: %s", pName != nullptr ? pName : "(no name)",
                    pName == nullptr || pName[0] == 0 ? "no name"
                    : count == 0 || count > kMost     ? "the count is not 1 to 1024"
                    : (flags & ~LUMA_MAKE_DEAD) != 0  ? "a flag luma.h does not define"
                                                      : "a place smaller than luma.h's LumaMakePlace");
                return -4;
            }
            for (u32 i = 0; pPlace != nullptr && i < count; i++) {
                LumaMakePlace place;
                placeOf(pPlace, i, &place);
                if (!finite(place.pos, 3) || !finite(place.rot, 3) || !finite(place.scale, 3)) {
                    say(pMod, "make %s: place %u is not a finite position, turn and scale", pName, i);
                    return -4;
                }
            }
            // While the scene makes its objects (its player, then its placements): after it, nothing would give the
            // objects their after-placement init.
            if (!MR::isExistSceneObj(SceneObj_StageDataHolder) || !MR::isExistSceneObj(SceneObj_AllLiveActorGroup) ||
                !MR::isExistSceneObj(SceneObj_PlacementStateChecker) || !MR::isInitializeStatePlacementSomething()) {
                say(pMod, "make %s: not while the scene makes its objects (call it from a hook that runs then)", pName);
                return -3;
            }
            const CreatorFuncPtr pCreator = NameObjFactory::getCreator(pName);
            if (pCreator == nullptr) {
                say(pMod, "make %s: the factory makes nothing by that name", pName);
                return -1;
            }
            if (NebulaMods::isMissingFiles(pName)) {
                say(pMod, "make %s: its files are not in this game", pName);
                return -2;
            }
            const ::Table* const pTable = static_cast<const ::Table*>(writeTable(pName, count, pPlace));
            if (pTable == nullptr) {
                say(pMod, "make %s: no memory in the scene for %u places", pName, count);
                return -4;
            }
            // The actors registered and not yet given their placement's system info: another placement's, when this
            // call runs inside its init (they and ours then get that placement's, as the objects it makes). Otherwise
            // ours are taken as given one with none (the table has no view group), not left to the next placement.
            AllLiveActorGroup* const pActors = MR::getAllLiveActorGroup();
            const bool pending = pActors->mActorNum < pActors->getObjNum();
            // The game's objects are made by their Japanese names (as PlacementInfoOrdered); a mod's class by its own (the
            // factory's hook, LumaClasses, makes it the class's object).
            const char* const pArgument = MR::getJapaneseObjectName(pName);
            const bool dead = (flags & LUMA_MAKE_DEAD) != 0;
            u32 made = 0;
            {
                InMainStage inMainStage;
                for (; made < count; made++) {
                    NameObj* const pObject = pCreator(pArgument);
                    if (pObject == nullptr) {
                        say(pMod, "make %s: its creator made nothing (object %u)", pName, made);
                        break;
                    }
                    const TableIter iter(pTable, static_cast<s32>(made));
                    while (!pObject->initStep(iter, UINT64_MAX)) {
                    }
                    if (dead) {
                        if (lumacore::isA(lumacore::classOf(pObject), lumacore::kClassId< LiveActor >)) {
                            static_cast<LiveActor*>(pObject)->makeActorDead();
                        } else if (made == 0) {
                            say(pMod, "make %s: not an actor, so made alive (LUMA_MAKE_DEAD is for actors)", pName);
                        }
                    }
                    if (ppObjects != nullptr) {
                        ppObjects[made] = pObject;
                    }
                }
            }
            if (!pending) {
                pActors->mActorNum = pActors->getObjNum();
            }
            return static_cast<s32>(made);
        }
    }  // namespace make
}  // namespace luma
