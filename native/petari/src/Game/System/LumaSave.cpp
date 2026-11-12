#include "Game/System/LumaSave.hpp"

#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/LumaSaveChunk.hpp"
#include "Game/System/LumaServices.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <luma_modlog.h>
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

            // ---- save.global (item 7): one mod's values across files, in <save root>/<code>/luma/<id>.bin ----
            // The file holds the chunk's data form for that one mod (u16 version, u16 mods 0 or 1, its block), at most
            // 1 MiB. Read through the host's save service as the mod begins, so its first call seldom waits; written whole
            // (atomically, by the host) at the start of the frame after a change, at most one write a mod in flight.

            const u32 cFirstRead = 4096;    // most global files fit; a longer one is read again at its length
            const u32 cCodeGlobalBroken = 185;  // Save.GlobalBroken

            struct GlobalStore {
                GlobalStore() :
                    mNext(nullptr), mMod(nullptr), mValues(nullptr, LUMA_SAVE_GLOBAL_MOST, LUMA_SAVE_GLOBAL_MOST),
                    mBuffer(nullptr), mCapacity(0), mLength(0), mLoaded(0), mWriting(false), mDirty(false) {
                    mId[0] = '\0';
                    mName[0] = '\0';
                }
                ~GlobalStore() { delete[] mBuffer; }

                GlobalStore* mNext;
                const LumaMod* mMod;    // null once the mod ended: the store stays until its request is done
                char mId[LumaSaveChunk::cIdMost + 1];
                char mName[5 + LumaSaveChunk::cIdMost + 5];  // "luma/<id>.bin", held by mRequest while it runs
                LumaSaveChunk mValues;
                SaveRequest mRequest;
                u8* mBuffer;            // the running read's or write's bytes
                u32 mCapacity;
                u32 mLength;            // the read's file length (the host's)
                s32 mLoaded;            // 0 reading, 1 the values are here, < 0 the status every call answers
                bool mWriting;
                bool mDirty;            // changed since the last write began
            };
            GlobalStore* sStores = nullptr;

            GlobalStore* storeOf(const LumaMod* pMod) {
                for (GlobalStore* pStore = sStores; pStore != nullptr; pStore = pStore->mNext) {
                    if (pStore->mMod == pMod) {
                        return pStore;
                    }
                }
                return nullptr;
            }

            void sayGlobalBroken(const GlobalStore* pStore) {
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CODE;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = cCodeGlobalBroken;
                entry.mod = pStore->mId;
                entry.file = pStore->mName;  // the place: luma/<id>.bin; the row has no arguments
                entry.count = 0;
                luma::modlog::add(entry, "Save.GlobalBroken");
            }

            bool startRead(GlobalStore* pStore, u32 capacity) {
                delete[] pStore->mBuffer;
                pStore->mBuffer = new u8[capacity];
                if (pStore->mBuffer == nullptr) {
                    pStore->mCapacity = 0;
                    return false;
                }
                pStore->mCapacity = capacity;
                pStore->mLength = 0;
                pStore->mRequest.setReadSeq(pStore->mName, pStore->mBuffer, capacity, &pStore->mLength);
                pStore->mRequest.start();
                return true;
            }

            // The host stores the status from its own thread (an acquire load each time round).
            void waitFor(const GlobalStore* pStore) {
                while (!pStore->mRequest.isDone()) {
                }
            }

            // Game thread: the read begun at the mod's begin, finished (waited for when still running) and kept.
            void finishRead(GlobalStore* pStore) {
                for (;;) {
                    waitFor(pStore);
                    const SaveRequest::Result result = pStore->mRequest.getResult();
                    if (result == SaveRequest::Result_Done) {
                        const LumaSaveChunk::Result parsed = pStore->mValues.parse(pStore->mBuffer, pStore->mLength);
                        if (parsed == LumaSaveChunk::Result_NotFound) {
                            sayGlobalBroken(pStore);  // starts empty; the next change writes the file again
                        }
                        pStore->mLoaded = parsed == LumaSaveChunk::Result_Memory ? LUMA_E_MEMORY : 1;
                    } else if (result == SaveRequest::Result_NoFile) {
                        pStore->mLoaded = 1;
                    } else if (result == SaveRequest::Result_Corrupted) {
                        // Longer than the buffer: read again at the length the host gave, up to the 1 MiB a file holds.
                        if (pStore->mLength > pStore->mCapacity && pStore->mLength <= LUMA_SAVE_GLOBAL_MOST) {
                            if (startRead(pStore, pStore->mLength)) {
                                continue;
                            }
                            pStore->mLoaded = LUMA_E_MEMORY;
                        } else {
                            sayGlobalBroken(pStore);
                            pStore->mLoaded = 1;
                        }
                    } else {
                        // The file system refused: no values, and nothing is written over the file.
                        pStore->mLoaded = LUMA_E_IO;
                    }
                    break;
                }
                delete[] pStore->mBuffer;
                pStore->mBuffer = nullptr;
                pStore->mCapacity = 0;
            }

            // The mod's store with its values here, or the status to return.
            s32 readyStore(const LumaMod* pMod, GlobalStore** ppStore) {
                GlobalStore* const pStore = storeOf(pMod);
                if (pStore == nullptr) {
                    return LUMA_E_MEMORY;  // no memory for it at the mod's begin
                }
                if (pStore->mLoaded == 0) {
                    finishRead(pStore);
                }
                *ppStore = pStore;
                return pStore->mLoaded < 0 ? pStore->mLoaded : LUMA_OK;
            }

            // The whole file to the host. No memory now: stays changed, tried again next frame.
            void startWrite(GlobalStore* pStore) {
                const u32 size = pStore->mValues.getDataSize();
                u8* const pBytes = new u8[size];
                if (pBytes == nullptr) {
                    return;
                }
                (void)pStore->mValues.serialize(pBytes, size);  // exactly its size: never short
                delete[] pStore->mBuffer;
                pStore->mBuffer = pBytes;
                pStore->mRequest.setWriteSeq(pStore->mName, pBytes, size);
                pStore->mRequest.start();
                pStore->mWriting = true;
                pStore->mDirty = false;
            }

            // The frame's start while a store has work (LumaServices::setSaveHook): a finished write let go, a changed
            // store written, an ended mod's store freed once its request is done. Unhooked when none has work.
            void frameWork() {
                bool more = false;
                for (GlobalStore** ppStore = &sStores; *ppStore != nullptr;) {
                    GlobalStore* const pStore = *ppStore;
                    if (pStore->mWriting && pStore->mRequest.isDone()) {
                        // A refused write keeps the values here; the mod's next change writes them all again.
                        pStore->mWriting = false;
                        delete[] pStore->mBuffer;
                        pStore->mBuffer = nullptr;
                    }
                    if (pStore->mDirty && !pStore->mWriting) {
                        startWrite(pStore);
                    }
                    if (pStore->mMod == nullptr && !pStore->mDirty && pStore->mRequest.isDone()) {
                        *ppStore = pStore->mNext;
                        delete pStore;
                        continue;
                    }
                    more = more || pStore->mWriting || pStore->mDirty || pStore->mMod == nullptr;
                    ppStore = &pStore->mNext;
                }
                if (!more) {
                    luma::services::setSaveHook(nullptr);
                }
            }

            void changed(GlobalStore* pStore) {
                pStore->mDirty = true;
                luma::services::setSaveHook(frameWork);
            }

            int64_t svcGlobalGet(const LumaMod* pMod, const char* pKey, void* pBuf, uint64_t size) {
                const char* const pWhere = "save.global.get";
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
                GlobalStore* pStore = nullptr;
                if (const s32 status = readyStore(pMod, &pStore); status != LUMA_OK) {
                    return status;
                }
                const u32 room = size < LumaSaveChunk::cValueMost ? static_cast< u32 >(size) : LumaSaveChunk::cValueMost;
                const s64 length = pStore->mValues.get(pStore->mId, pKey, pBuf, room);
                return length >= 0 ? length : LUMA_E_NOT_FOUND;
            }

            int32_t svcGlobalSet(const LumaMod* pMod, const char* pKey, const void* pValue, uint64_t bytes) {
                const char* const pWhere = "save.global.set";
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
                if (bytes > LumaSaveChunk::cValueMost) {
                    reportLimit(pMod, pWhere, LumaSaveChunk::cValueMost);
                    return LUMA_E_LIMIT;
                }
                GlobalStore* pStore = nullptr;
                if (const s32 status = readyStore(pMod, &pStore); status != LUMA_OK) {
                    return status;
                }
                switch (pStore->mValues.set(pStore->mId, pKey, pValue, static_cast< u32 >(bytes))) {
                case LumaSaveChunk::Result_Ok:
                    changed(pStore);
                    return LUMA_OK;
                case LumaSaveChunk::Result_Limit:
                case LumaSaveChunk::Result_ChunkFull:
                    reportLimit(pMod, pWhere, LUMA_SAVE_GLOBAL_MOST);
                    return LUMA_E_LIMIT;
                default:
                    return LUMA_E_MEMORY;
                }
            }

            int32_t svcGlobalRemove(const LumaMod* pMod, const char* pKey) {
                const char* const pWhere = "save.global.remove";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!checkKey(pMod, pWhere, pKey)) {
                    return LUMA_E_ARGUMENT;
                }
                GlobalStore* pStore = nullptr;
                if (const s32 status = readyStore(pMod, &pStore); status != LUMA_OK) {
                    return status;
                }
                if (pStore->mValues.remove(pStore->mId, pKey) != LumaSaveChunk::Result_Ok) {
                    return LUMA_E_NOT_FOUND;
                }
                changed(pStore);
                return LUMA_OK;
            }

            const LumaSaveGlobalService cGlobalTable = {
                {sizeof(LumaSaveGlobalService), LUMA_SAVE_GLOBAL_MAJOR, LUMA_SAVE_GLOBAL_MINOR},
                svcGlobalGet,
                svcGlobalSet,
                svcGlobalRemove,
            };

            // Game thread, before the mod's begin: its file's read starts (no memory: every call answers LUMA_E_MEMORY).
            void globalBegin(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                const char* const pId = sKernel->mod_id(pMod);
                if (pId == nullptr) {
                    return;
                }
                GlobalStore* const pStore = new GlobalStore;
                if (pStore == nullptr) {
                    return;
                }
                nebula_snprintf(pStore->mId, sizeof(pStore->mId), "%s", pId);
                nebula_snprintf(pStore->mName, sizeof(pStore->mName), "luma/%s.bin", pStore->mId);
                pStore->mMod = pMod;
                if (!startRead(pStore, cFirstRead)) {
                    pStore->mLoaded = LUMA_E_MEMORY;
                }
                pStore->mNext = sStores;
                sStores = pStore;
            }

            // After the mod's stop: a change not yet written begins now (the game may end before another frame).
            void globalGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                GlobalStore* const pStore = storeOf(pMod);
                if (pStore == nullptr || !pStore->mDirty) {
                    return;
                }
                if (pStore->mWriting) {
                    waitFor(pStore);
                    pStore->mWriting = false;
                }
                startWrite(pStore);
                luma::services::setSaveHook(frameWork);
            }

            // Before the mod's memory goes: its store leaves the mod; freed at once, or by frameWork once its request
            // is done (the host holds the bytes until then).
            void globalEnd(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                GlobalStore* const pStore = storeOf(pMod);
                if (pStore == nullptr) {
                    return;
                }
                pStore->mMod = nullptr;
                luma::services::setSaveHook(frameWork);
            }

            const LumaServiceRow cGlobalRows[] = {
                {185, "Save.GlobalBroken", {nullptr, nullptr, nullptr, nullptr}},
            };

            const LumaServiceProvider cGlobalProvider = {
                sizeof(LumaServiceProvider),
                LUMA_SAVE_GLOBAL_NAME,
                &cGlobalTable.base,
                nullptr,
                cGlobalRows,
                sizeof(cGlobalRows) / sizeof(cGlobalRows[0]),
                nullptr,
                globalBegin,
                globalGone,
                globalEnd,
                nullptr,
            };
        }  // namespace

        void fileLoaded() {
            sLoaded = true;
        }

        const LumaServiceProvider* provider() {
            return &cProvider;
        }

        const LumaServiceProvider* globalProvider() {
            return &cGlobalProvider;
        }
    }
}
