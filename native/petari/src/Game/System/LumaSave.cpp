#include "Game/System/LumaSave.hpp"

#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/LumaSaveChunk.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <luma_save.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>

// The save service (contract services-save). Game thread only: the values live in the loaded file's GameDataHolder,
// which the game's save sequence reads on the same thread. No mod, no cost: nothing here runs; the chunk is 16 bytes
// in each file (item 10).

namespace luma {
    namespace save {
        namespace {
            const LumaServiceKernel* sKernel = nullptr;
            // A file was picked on the file-select screen this session (the title's own stage ends it again).
            bool sLoaded = false;

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg) {
                const char* args[1] = {pArg};
                sKernel->report(pMod, pWhere, code, args, pArg != nullptr ? 1u : 0u);
            }

            void reportLimit(const LumaMod* pMod, const char* pWhere, u32 limit) {
                char text[16];
                nebula_snprintf(text, sizeof(text), "%u", limit);
                report(pMod, pWhere, 104u, text);
            }

            // The mod known and live, on the game thread (services-registry 4). LUMA_OK, or the status to return.
            s32 enter(const LumaMod* pMod, const char* pWhere) {
                if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                    sKernel->unknown_mod(pWhere);
                    return LUMA_E_ARGUMENT;
                }
                if (sKernel->mod_live(pMod) == 0) {
                    return LUMA_E_CANCELLED;
                }
                if (sKernel->game_thread() == 0) {
                    report(pMod, pWhere, 100u, nullptr);
                    return LUMA_E_THREAD;
                }
                return LUMA_OK;
            }

            bool checkKey(const LumaMod* pMod, const char* pWhere, const char* pKey) {
                if (LumaSaveChunk::isValidKey(pKey)) {
                    return true;
                }
                report(pMod, pWhere, 101u, "key");
                return false;
            }

            // The loaded file's game data, or null at the title and the file select (item 1: LUMA_E_ABSENT).
            GameDataHolder* loadedHolder() {
                if (!sLoaded || MR::isStageFileSelect()) {
                    return nullptr;
                }
                return GameDataFunction::getCurrentGameDataHolder();
            }

            int64_t svcGet(const LumaMod* pMod, const char* pKey, void* pBuf, uint64_t size) {
                const char* const pWhere = "save.get";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!checkKey(pMod, pWhere, pKey)) {
                    return LUMA_E_ARGUMENT;
                }
                if (pBuf == nullptr && size != 0) {
                    report(pMod, pWhere, 101u, "buf");
                    return LUMA_E_ARGUMENT;
                }
                GameDataHolder* const pHolder = loadedHolder();
                if (pHolder == nullptr) {
                    return LUMA_E_ABSENT;
                }
                const u32 room = size < LumaSaveChunk::cValueMost ? static_cast< u32 >(size) : LumaSaveChunk::cValueMost;
                const s64 length = pHolder->getLumaChunk()->get(sKernel->mod_id(pMod), pKey, pBuf, room);
                return length >= 0 ? length : LUMA_E_NOT_FOUND;
            }

            int32_t svcSet(const LumaMod* pMod, const char* pKey, const void* pValue, uint64_t bytes) {
                const char* const pWhere = "save.set";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!checkKey(pMod, pWhere, pKey)) {
                    return LUMA_E_ARGUMENT;
                }
                if (pValue == nullptr && bytes != 0) {
                    report(pMod, pWhere, 101u, "value");
                    return LUMA_E_ARGUMENT;
                }
                GameDataHolder* const pHolder = loadedHolder();
                if (pHolder == nullptr) {
                    return LUMA_E_ABSENT;
                }
                if (bytes > LumaSaveChunk::cValueMost) {
                    reportLimit(pMod, pWhere, LumaSaveChunk::cValueMost);
                    return LUMA_E_LIMIT;
                }
                switch (pHolder->getLumaChunk()->set(sKernel->mod_id(pMod), pKey, pValue, static_cast< u32 >(bytes))) {
                case LumaSaveChunk::Result_Ok:
                    return LUMA_OK;
                case LumaSaveChunk::Result_Limit:
                    reportLimit(pMod, pWhere, LumaSaveChunk::cModMost);
                    return LUMA_E_LIMIT;
                case LumaSaveChunk::Result_ChunkFull:
                    reportLimit(pMod, pWhere, LumaSaveChunk::cChunkMost + BinaryDataChunk::cDataOffset);
                    return LUMA_E_LIMIT;
                default:
                    return LUMA_E_MEMORY;
                }
            }

            int32_t svcRemove(const LumaMod* pMod, const char* pKey) {
                const char* const pWhere = "save.remove";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!checkKey(pMod, pWhere, pKey)) {
                    return LUMA_E_ARGUMENT;
                }
                GameDataHolder* const pHolder = loadedHolder();
                if (pHolder == nullptr) {
                    return LUMA_E_ABSENT;
                }
                return pHolder->getLumaChunk()->remove(sKernel->mod_id(pMod), pKey) == LumaSaveChunk::Result_Ok ? LUMA_OK
                                                                                                                 : LUMA_E_NOT_FOUND;
            }

            int32_t svcFile(const LumaMod* pMod, uint32_t* pNumber, LumaSaveCharacter* pCharacter) {
                const char* const pWhere = "save.file";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pNumber == nullptr || pCharacter == nullptr) {
                    report(pMod, pWhere, 101u, pNumber == nullptr ? "number" : "character");
                    return LUMA_E_ARGUMENT;
                }
                const GameDataHolder* const pHolder = loadedHolder();
                if (pHolder == nullptr) {
                    return LUMA_E_ABSENT;
                }
                const SaveFileId file = pHolder->getFileId();
                *pNumber = static_cast< uint32_t >(file.mUser);
                *pCharacter = file.mKind == SaveFileId::Kind_Mario ? LUMA_SAVE_MARIO : LUMA_SAVE_LUIGI;
                return LUMA_OK;
            }

            const LumaSaveService cTable = {
                {sizeof(LumaSaveService), LUMA_SAVE_MAJOR, LUMA_SAVE_MINOR},
                svcGet,
                svcSet,
                svcRemove,
                svcFile,
            };

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            // The chunk's own rows (said by LumaSaveChunk through the mod log, about a file, not reported by a mod).
            const LumaServiceRow cRows[] = {
                {180, "Save.ChunkBroken", {"file", nullptr, nullptr, nullptr}},
                {184, "Save.ChunkTooBig", {"file", nullptr, nullptr, nullptr}},
            };

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_SAVE_NAME,
                &cTable.base,
                nullptr,
                cRows,
                sizeof(cRows) / sizeof(cRows[0]),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                registered,
            };
        }  // namespace

        void fileLoaded() {
            sLoaded = true;
        }

        const LumaServiceProvider* provider() {
            return &cProvider;
        }
    }
}
