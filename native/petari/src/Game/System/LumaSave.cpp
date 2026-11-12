#include "Game/System/LumaSave.hpp"

#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/LumaSaveChunk.hpp"
#include "Game/System/LumaServices.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <luma_modlog.h>
#include <luma_save.h>
#include <luma_service_provider.h>
#include <nebula_game_api.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_module.h>

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
                if (pHolder == nullptr || pHolder->getLumaChunk()->isNewer()) {
                    return LUMA_E_ABSENT;  // also a later version's values, kept unread (LumaSaveChunk::isNewer)
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
                if (pHolder == nullptr || pHolder->getLumaChunk()->isNewer()) {
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
                if (pHolder == nullptr || pHolder->getLumaChunk()->isNewer()) {
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
            const u32 cCodeGlobalWriteFailed = 186;  // Save.GlobalWriteFailed
            const u32 cRetryFirst = 60;     // frames before a refused write is tried again, doubling to cRetryMost
            const u32 cRetryMost = 3600;
            u32 sFrame = 0;                 // frames the save hook ran (the retries' clock)

            struct GlobalStore {
                GlobalStore() :
                    mNext(nullptr), mMod(nullptr), mValues(nullptr, LUMA_SAVE_GLOBAL_MOST, LUMA_SAVE_GLOBAL_MOST),
                    mBuffer(nullptr), mCapacity(0), mLength(0), mLoaded(0), mWriting(false), mDirty(false),
                    mSettingAside(false), mBackoff(0), mWaitUntil(0) {
                    mId[0] = '\0';
                    mName[0] = '\0';
                    mBrokenName[0] = '\0';
                }
                ~GlobalStore() { delete[] mBuffer; }

                GlobalStore* mNext;
                const LumaMod* mMod;    // null once the mod ended: the store stays until its request is done
                char mId[LumaSaveChunk::cIdMost + 1];
                char mName[5 + LumaSaveChunk::cIdMost + 5];  // "luma/<id>.bin", held by mRequest while it runs
                char mBrokenName[5 + LumaSaveChunk::cIdMost + 12];  // "luma/<id>.bin.broken", likewise
                LumaSaveChunk mValues;
                SaveRequest mRequest;
                u8* mBuffer;            // the running read's or write's bytes
                u32 mCapacity;
                u32 mLength;            // the read's file length (the host's)
                s32 mLoaded;            // 0 reading, 1 the values are here, < 0 the status every call answers
                bool mWriting;
                bool mDirty;            // changed since the last write began
                bool mSettingAside;     // the running write is the broken file's copy (setAside)
                u32 mBackoff;           // frames of the last wait after a refused write; 0 after a write that worked
                u32 mWaitUntil;         // sFrame from which a refused write is tried again
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

            // An Error about the mod's global file; the place is the file (the rows have no arguments).
            void sayGlobal(const GlobalStore* pStore, u32 code, const char* pRow, const char* pFile) {
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CODE;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = code;
                entry.mod = pStore->mId;
                entry.file = pFile;
                entry.count = 0;
                luma::modlog::add(entry, pRow);
            }

            void saveFrame();

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
#if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                    __asm__ volatile("yield");
#endif
                }
            }

            // A file that does not parse: its bytes are written to luma/<id>.bin.broken first (said, naming it; an older
            // copy there is replaced), and luma/<id>.bin is written over only once that copy is down. The values start
            // empty. The read's buffer is the copy's until it is done.
            void setAside(GlobalStore* pStore) {
                sayGlobal(pStore, cCodeGlobalBroken, "Save.GlobalBroken", pStore->mBrokenName);
                pStore->mRequest.setWriteSeq(pStore->mBrokenName, pStore->mBuffer, pStore->mLength);
                pStore->mRequest.start();
                pStore->mWriting = true;
                pStore->mSettingAside = true;
                pStore->mLoaded = 1;
                luma::services::setSaveHook(saveFrame);
            }

            // Game thread: the read begun at the mod's begin, finished (waited for when still running) and kept.
            void finishRead(GlobalStore* pStore) {
                for (;;) {
                    waitFor(pStore);
                    const SaveRequest::Result result = pStore->mRequest.getResult();
                    if (result == SaveRequest::Result_Done) {
                        const LumaSaveChunk::Result parsed = pStore->mValues.parse(pStore->mBuffer, pStore->mLength);
                        if (parsed == LumaSaveChunk::Result_NotFound) {
                            setAside(pStore);
                            return;
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
                            // Past the 1 MiB this game writes (a later version's, or no global file at all): never
                            // written over; every call answers LUMA_E_ABSENT.
                            sayGlobal(pStore, cCodeGlobalBroken, "Save.GlobalBroken", pStore->mName);
                            pStore->mLoaded = LUMA_E_ABSENT;
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
                if (pStore->mLoaded > 0 && pStore->mValues.isNewer()) {
                    return LUMA_E_ABSENT;  // a later version's file: kept, never written (no change reaches it)
                }
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

            // A write the host finished. Refused (a full disk, a locked file): the values stay here and are written whole
            // again after a wait doubling from 1 s to 1 min, said once for each run of refusals. The broken file's copy
            // refused: luma/<id>.bin is never written over this session (every call answers LUMA_E_IO).
            void writeDone(GlobalStore* pStore) {
                const bool written = pStore->mRequest.getResult() == SaveRequest::Result_Done;
                pStore->mWriting = false;
                delete[] pStore->mBuffer;
                pStore->mBuffer = nullptr;
                pStore->mCapacity = 0;
                if (pStore->mSettingAside) {
                    pStore->mSettingAside = false;
                    if (!written) {
                        pStore->mLoaded = LUMA_E_IO;
                        pStore->mDirty = false;
                    }
                    return;
                }
                if (written) {
                    pStore->mBackoff = 0;
                    return;
                }
                if (pStore->mBackoff == 0) {
                    sayGlobal(pStore, cCodeGlobalWriteFailed, "Save.GlobalWriteFailed", pStore->mName);
                }
                pStore->mBackoff = pStore->mBackoff == 0 ? cRetryFirst
                                   : pStore->mBackoff >= cRetryMost / 2 ? cRetryMost
                                                                        : pStore->mBackoff * 2;
                pStore->mWaitUntil = sFrame + pStore->mBackoff;
                pStore->mDirty = true;
            }

            // The frame's start while a store has work: a finished write let go, a changed store written (a refused
            // one once its wait is over), an ended mod's store freed once its request is done. False: none has work.
            bool globalsWork() {
                sFrame++;
                bool more = false;
                for (GlobalStore** ppStore = &sStores; *ppStore != nullptr;) {
                    GlobalStore* const pStore = *ppStore;
                    if (pStore->mWriting && pStore->mRequest.isDone()) {
                        writeDone(pStore);
                    }
                    if (pStore->mDirty && !pStore->mWriting && static_cast< s32 >(sFrame - pStore->mWaitUntil) >= 0) {
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
                return more;
            }

            bool forgetWork();

            // The save hook (LumaServices::setSaveHook, wakeSaveHook): save.global's stores and a FORGET's files.
            // Unhooked when neither has work.
            void saveFrame() {
                const bool globals = globalsWork();
                const bool forgets = forgetWork();
                if (!globals && !forgets) {
                    luma::services::setSaveHook(nullptr);
                }
            }

            void changed(GlobalStore* pStore) {
                pStore->mDirty = true;
                luma::services::setSaveHook(saveFrame);
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
                nebula_snprintf(pStore->mBrokenName, sizeof(pStore->mBrokenName), "luma/%s.bin.broken", pStore->mId);
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
                    writeDone(pStore);
                }
                if (pStore->mDirty) {
                    startWrite(pStore);  // a refused write's wait is not kept: the game may end before it is over
                }
                luma::services::setSaveHook(saveFrame);
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
                luma::services::setSaveHook(saveFrame);
            }

            const LumaServiceRow cGlobalRows[] = {
                {185, "Save.GlobalBroken", {nullptr, nullptr, nullptr, nullptr}},
                {186, "Save.GlobalWriteFailed", {nullptr, nullptr, nullptr, nullptr}},
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

        // ---- The REMOVED MODS line's half (item 5; luma_service_provider.h luma_save_kept, luma_save_forget) ----
        namespace {
            // Per game-data file in memory (6 users, Mario and Luigi): the mods with values in its chunk. Written on
            // the game thread, read by the host from any thread, both under sKeptLock.
            const u32 cKeptFiles = 12;
            const u32 cForgetMost = 256;  // ids forgotten in one session (one past it is logged, not forgotten)
            struct KeptRow {
                char mId[LumaSaveChunk::cIdMost + 1];
                u32 mBytes;
            };
            struct KeptFile {
                KeptRow* mRows;
                u32 mCount;
            };
            KeptFile sKeptFiles[cKeptFiles] = {};
            char sForgetting[cForgetMost][LumaSaveChunk::cIdMost + 1];
            u32 sForgettingCount = 0;
            u32 sForgetApplied = 0;  // of them, applied by forgetWork (game thread)
            char sKeptLock = 0;

            void lockKept() {
                while (__atomic_test_and_set(&sKeptLock, __ATOMIC_ACQUIRE)) {
#if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
                    __asm__ volatile("yield");
#endif
                }
            }

            void unlockKept() {
                __atomic_clear(&sKeptLock, __ATOMIC_RELEASE);
            }

            bool sameId(const char* pA, const char* pB) {
                for (u32 i = 0;; i++) {
                    if (pA[i] != pB[i]) {
                        return false;
                    }
                    if (pA[i] == '\0') {
                        return true;
                    }
                }
            }

            // Under sKeptLock.
            bool isForgotten(const char* pId) {
                for (u32 i = 0; i < sForgettingCount; i++) {
                    if (sameId(sForgetting[i], pId)) {
                        return true;
                    }
                }
                return false;
            }

            // A mod id's characters and length as the chunk keeps them (a-z 0-9 - ., 1 to 64).
            bool isIdText(const char* pId) {
                if (pId == nullptr) {
                    return false;
                }
                u32 length = 0;
                for (; pId[length] != '\0'; length++) {
                    const char c = pId[length];
                    if (length >= LumaSaveChunk::cIdMost || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
                        return false;
                    }
                }
                return length != 0;
            }

            struct RowsFill {
                KeptRow* mRows;
                u32 mCount;
            };

            // applyForgets' and forgetWork's copy of the forgotten ids, taken under sKeptLock and used outside it (game
            // thread only; off the stack: 4 KiB is a stack probe the module cannot link, DIRECTOR R9).
            char sApplying[cForgetMost][LumaSaveChunk::cIdMost + 1];

            // The frame's start after a FORGET (woken from the host's thread): the new ids out of the files in memory at
            // once, the stored ones and the loaded file's values, and when a stored file held values of one, the files
            // written by the quiet save, so a FORGET holds with no save of the player's. Waits while a sequence runs and
            // until the start's read (none read: no stored file has values). False: nothing more to do.
            bool forgetWork() {
                lockKept();
                const u32 count = sForgettingCount;
                for (u32 i = sForgetApplied; i < count; i++) {
                    nebula_snprintf(sApplying[i], sizeof(sApplying[i]), "%s", sForgetting[i]);
                }
                unlockKept();
                if (count == sForgetApplied) {
                    return false;
                }
                SaveDataHandleSequence* const pSequence =
                    SingletonHolder< GameSystem >::get()->mSequenceDirector->mSaveDataHandleSequence;
                if (pSequence == nullptr || pSequence->mSaveDataHandler == nullptr || pSequence->getPreLoadState() == 0 ||
                    pSequence->isActive()) {
                    return true;
                }
                bool held = false;
                GameDataHolder* const pHolder = loadedHolder();
                for (u32 i = sForgetApplied; i < count; i++) {
                    held = pSequence->mSaveDataHandler->forgetLumaMod(sApplying[i]) || held;
                    if (pHolder != nullptr) {
                        pHolder->getLumaChunk()->forgetMod(sApplying[i]);
                    }
                }
                sForgetApplied = count;
                if (held) {
                    (void)pSequence->startQuietSave();  // false: saves are off; the files in memory are without them
                }
                return false;
            }
        }  // namespace

        void fileStored(u32 index, const u8* pFile, u32 size) {
            if (index >= cKeptFiles) {
                return;
            }
            KeptRow* pRows = nullptr;
            u32 count = 0;
            u32 at = 0;
            u32 chunkSize = 0;
            if (LumaSaveChunk::findInFile(pFile, size, &at, &chunkSize)) {
                LumaSaveChunk values(nullptr);
                if (values.parse(pFile + at + BinaryDataChunk::cDataOffset, chunkSize - BinaryDataChunk::cDataOffset) ==
                    LumaSaveChunk::Result_Ok) {
                    values.forEachMod([](void* pUser, const char*, u32, u32) { (*static_cast< u32* >(pUser))++; }, &count);
                    pRows = count != 0 ? new KeptRow[count] : nullptr;
                    if (pRows != nullptr) {
                        RowsFill fill = {pRows, 0};
                        values.forEachMod(
                            [](void* pUser, const char* pId, u32 idLength, u32 bytes) {
                                RowsFill& rFill = *static_cast< RowsFill* >(pUser);
                                KeptRow& rRow = rFill.mRows[rFill.mCount++];
                                for (u32 i = 0; i < idLength; i++) {
                                    rRow.mId[i] = pId[i];
                                }
                                rRow.mId[idLength] = '\0';
                                rRow.mBytes = bytes;
                            },
                            &fill);
                    } else {
                        count = 0;  // no memory: this file's mods are not counted until it is stored again
                    }
                }
            }
            lockKept();
            KeptRow* const pOld = sKeptFiles[index].mRows;
            sKeptFiles[index].mRows = pRows;
            sKeptFiles[index].mCount = count;
            unlockKept();
            delete[] pOld;
        }

        void applyForgets(SaveDataHandler* pHandler, GameDataHolder* pHolder) {
            char (&ids)[cForgetMost][LumaSaveChunk::cIdMost + 1] = sApplying;
            lockKept();
            const u32 count = sForgettingCount;
            for (u32 i = 0; i < count; i++) {
                nebula_snprintf(ids[i], sizeof(ids[i]), "%s", sForgetting[i]);
            }
            unlockKept();
            // A forgotten mod is a removed one: it cannot come back this session, so its id stays forgotten to the end.
            for (u32 i = 0; i < count; i++) {
                pHandler->forgetLumaMod(ids[i]);
                if (pHolder != nullptr) {
                    pHolder->getLumaChunk()->forgetMod(ids[i]);
                }
            }
        }

        // ---- save.game (item 8): the game's own file out (export_file, take) and in (offer) ----
        namespace {
            const u32 cExportsOpen = 4;   // one mod's open export requests
            const u32 cExportSlots = 32;  // all mods'
            const u32 cUserFiles = 6;
            const u32 cCodeExported = 181;  // Save.Exported

            struct ExportRequest {
                LumaEventNode mNode;  // first: deliver and drop are given the request through it
                LumaHandle mHandle;   // 0: a free slot
                u32 mFile;
                LumaEventFn mFn;
                void* mUser;
                const u8* mBytes;  // inside its EXPORT event's call only, else null
                u32 mLength;
                bool mTaken;
                bool mCancelled;  // no event: freed at its delivery or drop (the kernel may still hold the node)
            };
            ExportRequest sExports[cExportSlots] = {};
            LumaHandle sLastExport = 0;

            void sayExported(const LumaMod* pMod, u32 file) {
                char text[4];
                nebula_snprintf(text, sizeof(text), "%u", file);
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_CODE;
                entry.level = LUMA_MODLOG_NOTE;
                entry.code = cCodeExported;
                entry.mod = sKernel->mod_id(pMod);
                entry.file = "";
                entry.names[0] = "file";
                entry.values[0] = text;
                entry.count = 1;
                if (entry.mod != nullptr) {
                    luma::modlog::add(entry, "Save.Exported");
                }
            }

            void freeExport(ExportRequest* pRequest) {
                pRequest->mHandle = 0;
                pRequest->mFn = nullptr;
                pRequest->mUser = nullptr;
                pRequest->mBytes = nullptr;
                pRequest->mLength = 0;
                pRequest->mCancelled = false;
            }

            // The frame's start after the request: the file as the game would write it now, given in the event's call,
            // then freed with the request.
            void exportDeliver(LumaEventNode* pNode) {
                ExportRequest* const pRequest = reinterpret_cast< ExportRequest* >(pNode);
                if (pRequest->mCancelled) {
                    freeExport(pRequest);
                    return;
                }
                SaveDataHandleSequence* const pSequence =
                    SingletonHolder< GameSystem >::get()->mSequenceDirector->mSaveDataHandleSequence;
                const UserFile* pLoaded = pSequence->getCurrentUserFile();
                if (loadedHolder() == nullptr || pLoaded->mGameDataHolder != loadedHolder() ||
                    pLoaded->mGameDataHolder->getFileId().mUser != static_cast< s32 >(pRequest->mFile)) {
                    pLoaded = nullptr;
                }
                u8* pBytes = nullptr;
                u32 length = 0;
                const SaveDataHandler::ExportResult result =
                    pSequence->mSaveDataHandler->makeExport(static_cast< int >(pRequest->mFile), pLoaded, &pBytes, &length);
                LumaEvent event = {};
                event.size = sizeof(event);
                event.kind = LUMA_SAVE_GAME_EVENT_EXPORT;
                event.handle = pRequest->mHandle;
                event.status = result == SaveDataHandler::Export_Ok      ? LUMA_OK
                               : result == SaveDataHandler::Export_Empty ? LUMA_E_NOT_FOUND
                                                                         : LUMA_E_MEMORY;
                event.value = length;
                pRequest->mBytes = pBytes;
                pRequest->mLength = length;
                pRequest->mTaken = false;
                if (result == SaveDataHandler::Export_Ok) {
                    sayExported(pNode->mod, pRequest->mFile);
                }
                (void)sKernel->call(pNode->mod, LUMA_SAVE_GAME_NAME, pRequest->mFn, pRequest->mUser, &event);
                delete[] pBytes;
                freeExport(pRequest);
            }

            void exportDrop(LumaEventNode* pNode) {
                freeExport(reinterpret_cast< ExportRequest* >(pNode));
            }

            // The mod's open request `handle` (not cancelled), or null.
            ExportRequest* openExport(const LumaMod* pMod, LumaHandle handle) {
                if (handle == 0) {
                    return nullptr;
                }
                for (ExportRequest& rRequest : sExports) {
                    if (rRequest.mHandle == handle && rRequest.mNode.mod == pMod && !rRequest.mCancelled) {
                        return &rRequest;
                    }
                }
                return nullptr;
            }

            int32_t svcExport(const LumaMod* pMod, uint32_t file, LumaEventFn fn, void* pUser, LumaHandle* pOut) {
                const char* const pWhere = "save.game.export_file";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (file < 1 || file > cUserFiles) {
                    report(pMod, pWhere, 101u, "file");
                    return LUMA_E_ARGUMENT;
                }
                if (fn == nullptr || pOut == nullptr) {
                    report(pMod, pWhere, 101u, fn == nullptr ? "fn" : "out");
                    return LUMA_E_ARGUMENT;
                }
                u32 open = 0;
                ExportRequest* pFree = nullptr;
                for (ExportRequest& rRequest : sExports) {
                    if (rRequest.mHandle == 0) {
                        if (pFree == nullptr) {
                            pFree = &rRequest;
                        }
                    } else if (rRequest.mNode.mod == pMod && !rRequest.mCancelled) {
                        open++;
                    }
                }
                if (open >= cExportsOpen || pFree == nullptr) {
                    reportLimit(pMod, pWhere, open >= cExportsOpen ? cExportsOpen : cExportSlots);
                    return LUMA_E_LIMIT;
                }
                if (++sLastExport == 0) {
                    ++sLastExport;
                }
                pFree->mNode = {};
                pFree->mNode.mod = pMod;
                pFree->mNode.deliver = exportDeliver;
                pFree->mNode.drop = exportDrop;
                pFree->mHandle = sLastExport;
                pFree->mFile = file;
                pFree->mFn = fn;
                pFree->mUser = pUser;
                pFree->mBytes = nullptr;
                pFree->mLength = 0;
                pFree->mTaken = false;
                pFree->mCancelled = false;
                const s32 posted = sKernel->post(&pFree->mNode);
                if (posted != LUMA_OK) {
                    freeExport(pFree);
                    return posted;
                }
                *pOut = pFree->mHandle;
                return LUMA_PENDING;
            }

            int64_t svcTake(const LumaMod* pMod, LumaHandle handle, void* pBuf, uint64_t size) {
                const char* const pWhere = "save.game.take";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                ExportRequest* const pRequest = openExport(pMod, handle);
                if (pRequest == nullptr || pRequest->mBytes == nullptr || pRequest->mTaken) {
                    report(pMod, pWhere, 102u, nullptr);
                    return LUMA_E_HANDLE;
                }
                if (pBuf == nullptr && size != 0) {
                    report(pMod, pWhere, 101u, "buf");
                    return LUMA_E_ARGUMENT;
                }
                const u32 copied = size < pRequest->mLength ? static_cast< u32 >(size) : pRequest->mLength;
                if (copied != 0) {
                    __builtin_memcpy(pBuf, pRequest->mBytes, copied);
                }
                pRequest->mTaken = true;
                return pRequest->mLength;
            }

            // A file in the mod's data folder: relative, parts split by '/', none empty, "." or "..", no '\' or ':'.
            bool isDataPath(const char* pPath) {
                if (pPath == nullptr || pPath[0] == '\0' || pPath[0] == '/') {
                    return false;
                }
                const char* pPart = pPath;
                for (const char* p = pPath;; p++) {
                    if (*p == '\\' || *p == ':') {
                        return false;
                    }
                    if (*p == '/' || *p == '\0') {
                        const u32 length = static_cast< u32 >(p - pPart);
                        if (length == 0 || (length == 1 && pPart[0] == '.') ||
                            (length == 2 && pPart[0] == '.' && pPart[1] == '.')) {
                            return false;
                        }
                        if (*p == '\0') {
                            return true;
                        }
                        pPart = p + 1;
                    }
                }
            }

            int32_t svcOffer(const LumaMod* pMod, const char* pPath, LumaEventFn fn, void* pUser, LumaHandle* pOut) {
                const char* const pWhere = "save.game.offer";
                (void)pUser;
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (!isDataPath(pPath)) {
                    report(pMod, pWhere, 101u, "path");
                    return LUMA_E_ARGUMENT;
                }
                if (fn == nullptr || pOut == nullptr) {
                    report(pMod, pWhere, 101u, fn == nullptr ? "fn" : "out");
                    return LUMA_E_ARGUMENT;
                }
                // The mod's data folder is the fs service's (plan 2.3), not built yet: no file can be named (item 8).
                return LUMA_E_ABSENT;
            }

            int32_t svcCancel(const LumaMod* pMod, LumaHandle handle) {
                const char* const pWhere = "save.game.cancel";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                ExportRequest* const pRequest = openExport(pMod, handle);
                if (pRequest == nullptr) {
                    report(pMod, pWhere, 102u, nullptr);
                    return LUMA_E_HANDLE;
                }
                // Queued: freed at its delivery, with no event. Inside its own call: freed when the call returns.
                pRequest->mCancelled = true;
                return LUMA_OK;
            }

            const LumaSaveGameService cGameTable = {
                {sizeof(LumaSaveGameService), LUMA_SAVE_GAME_MAJOR, LUMA_SAVE_GAME_MINOR},
                svcExport,
                svcTake,
                svcOffer,
                svcCancel,
            };

            // After the mod's stop: nothing more for it (a queued request is dropped by the kernel, or freed at delivery).
            void gameGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                for (ExportRequest& rRequest : sExports) {
                    if (rRequest.mHandle != 0 && rRequest.mNode.mod == pMod) {
                        rRequest.mCancelled = true;
                    }
                }
            }

            const LumaServiceRow cGameRows[] = {
                {181, "Save.Exported", {"file", nullptr, nullptr, nullptr}},
                {182, "Save.Offered", {"file", nullptr, nullptr, nullptr}},
                {183, "Save.Replaced", {"file", nullptr, nullptr, nullptr}},
            };

            const LumaServiceProvider cGameProvider = {
                sizeof(LumaServiceProvider),
                LUMA_SAVE_GAME_NAME,
                &cGameTable.base,
                nullptr,
                cGameRows,
                sizeof(cGameRows) / sizeof(cGameRows[0]),
                nullptr,
                nullptr,
                gameGone,
                nullptr,
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

        const LumaServiceProvider* gameProvider() {
            return &cGameProvider;
        }
    }
}

extern "C" {
NEBULA_GAME_API void luma_save_kept(LumaSaveKeptFn fn, void* user) {
    using namespace luma::save;
    if (fn == nullptr) {
        return;
    }
    // The pairs copied under the lock, fn called after it: room for every row (a file stored meanwhile may add some:
    // counted again until the room holds).
    KeptRow* pOut = nullptr;
    u32 room = 0;
    u32 out = 0;
    for (;;) {
        lockKept();
        u32 rows = 0;
        for (u32 f = 0; f < cKeptFiles; f++) {
            rows += sKeptFiles[f].mCount;
        }
        if (rows <= room) {
            break;  // still locked: the copy below
        }
        unlockKept();
        delete[] pOut;
        pOut = new KeptRow[rows];
        room = pOut != nullptr ? rows : 0;
        if (pOut == nullptr) {
            return;  // no memory: nothing given this time
        }
    }
    for (u32 f = 0; f < cKeptFiles; f++) {
        for (u32 r = 0; r < sKeptFiles[f].mCount; r++) {
            const char* const pId = sKeptFiles[f].mRows[r].mId;
            // Each id once, at its first file, with its bytes over every file.
            bool earlier = false;
            for (u32 g = 0; g <= f && !earlier; g++) {
                for (u32 s = 0; s < (g < f ? sKeptFiles[g].mCount : r); s++) {
                    if (sameId(sKeptFiles[g].mRows[s].mId, pId)) {
                        earlier = true;
                        break;
                    }
                }
            }
            if (earlier || isForgotten(pId)) {
                continue;
            }
            u32 bytes = 0;  // at most 12 files of 256 KiB
            for (u32 g = f; g < cKeptFiles; g++) {
                for (u32 s = 0; s < sKeptFiles[g].mCount; s++) {
                    if (sameId(sKeptFiles[g].mRows[s].mId, pId)) {
                        bytes += sKeptFiles[g].mRows[s].mBytes;
                    }
                }
            }
            nebula_snprintf(pOut[out].mId, sizeof(pOut[out].mId), "%s", pId);
            pOut[out].mBytes = bytes;
            out++;
        }
    }
    unlockKept();
    for (u32 i = 0; i < out; i++) {
        fn(user, pOut[i].mId, pOut[i].mBytes);
    }
    delete[] pOut;
}

NEBULA_GAME_API void luma_save_forget(const char* id) {
    using namespace luma::save;
    if (!isIdText(id)) {
        return;
    }
    lockKept();
    const bool known = isForgotten(id);
    const bool room = sForgettingCount < cForgetMost;
    if (!known && room) {
        nebula_snprintf(sForgetting[sForgettingCount], sizeof(sForgetting[0]), "%s", id);
        sForgettingCount++;
    }
    unlockKept();
    if (!known && !room) {
        nebula_log("LumaCore save: %u mods forgotten this session already; %s is not forgotten (FORGET it after a restart)\n",
                   cForgetMost, id);
    } else if (!known) {
        luma::services::wakeSaveHook(saveFrame);
    }
}
}
