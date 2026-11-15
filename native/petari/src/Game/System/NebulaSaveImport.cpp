#include "Game/System/NebulaSaveImport.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/SaveFileId.hpp"
#include "Game/System/SaveFileSummary.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <nebula_save_import.h>
#include <string.h>

namespace {
    // The host's read: Setting while nebula_save_import_read fills it (any thread), Ready once it has, until the game
    // thread takes it.
    enum Request : u32 {
        Request_None,
        Request_Setting,
        Request_Ready,
    };
    u32 sRequest = Request_None;  // atomic
    const uint8_t* sBytes = nullptr;
    uint32_t sLength = 0;
    NebulaSaveImportDone sDone = nullptr;
    void* sContext = nullptr;
    // The facts' reader, made at the first read (game thread).
    UserFile* sScratch = nullptr;

    // A Nebula.sav's user files as the screen sums them up. The save is one SaveDataHandler made: it splits.
    void sumUp(const u8* pSave, u32 length, NebulaSaveImportFile* pFiles) {
        const u8* parts[SaveDataHandler::cFileNum];
        u32 sizes[SaveDataHandler::cFileNum];
        const bool split = SaveDataHandler::splitSave(pSave, length, parts, sizes);
        for (u32 user = 1; user <= NEBULA_SAVE_IMPORT_FILES; user++) {
            NebulaSaveImportFile& rFile = pFiles[user - 1];
            rFile = {};
            const u32 first = (user - 1) * SaveFileId::Kind_Num;
            SaveFileSummary summary;
            if (!split || !readSaveFileSummary(sScratch, static_cast< int >(user), parts + first, sizes + first, &summary)) {
                rFile.damaged = 1;
                continue;
            }
            rFile.exists = summary.mExists ? 1u : 0u;
            rFile.luigi = summary.mLuigi ? 1u : 0u;
            rFile.stars = summary.mStars;
            rFile.saved = summary.mSaved;
        }
    }
}  // namespace

namespace NebulaSaveImport {
    void update() {
        if (__atomic_load_n(&sRequest, __ATOMIC_ACQUIRE) != Request_Ready) {
            return;
        }
        // The save's own reads and writes use the handler's buffers: after the start's read, between requests.
        GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
        SaveDataHandleSequence* const pSequence =
            pSystem != nullptr && pSystem->mSequenceDirector != nullptr ? pSystem->mSequenceDirector->mSaveDataHandleSequence
                                                                        : nullptr;
        SaveDataHandler* const pHandler = pSequence != nullptr ? pSequence->mSaveDataHandler : nullptr;
        if (pHandler == nullptr || pSequence->getPreLoadState() == 0 || !pHandler->isDone()) {
            return;
        }

        // The request is taken whole and freed first: the answer may ask the next read.
        const uint8_t* const pBytes = sBytes;
        const uint32_t length = sLength;
        const NebulaSaveImportDone done = sDone;
        void* const pContext = sContext;
        __atomic_store_n(&sRequest, Request_None, __ATOMIC_RELEASE);

        int32_t status = NEBULA_SAVE_IMPORT_OK;
        u8* pCopy = nullptr;  // the readers take the game's own copy (an old save's accessor takes it writable)
        u8* pImported = nullptr;
        u32 importedLength = 0;
        u8* pCurrent = nullptr;
        u32 currentLength = 0;
        NebulaSaveImportFile importedFiles[NEBULA_SAVE_IMPORT_FILES] = {};
        NebulaSaveImportFile currentFiles[NEBULA_SAVE_IMPORT_FILES] = {};
        if (sScratch == nullptr) {
            sScratch = new UserFile();
        }
        pCopy = new u8[length];
        if (sScratch == nullptr || pCopy == nullptr) {
            status = NEBULA_SAVE_IMPORT_MEMORY;
        } else {
            memcpy(pCopy, pBytes, length);
            if (!pHandler->convertSave(pCopy, length, &pImported, &importedLength)) {
                status = NEBULA_SAVE_IMPORT_DAMAGED;
            } else if (!pHandler->makeSave(&pCurrent, &currentLength)) {
                status = NEBULA_SAVE_IMPORT_MEMORY;
            } else {
                sumUp(pImported, importedLength, importedFiles);
                sumUp(pCurrent, currentLength, currentFiles);
            }
        }
        delete[] pCopy;
        nebula_log("save import: read %u bytes: status %d, %u bytes in Nebula.sav's form\n", length,
                   static_cast< int >(status), importedLength);
        if (status == NEBULA_SAVE_IMPORT_OK) {
            done(pContext, status, pImported, importedLength, importedFiles, pCurrent, currentLength, currentFiles);
        } else {
            done(pContext, status, nullptr, 0, nullptr, nullptr, 0, nullptr);
        }
        delete[] pImported;
        delete[] pCurrent;
    }
}  // namespace NebulaSaveImport

extern "C" NEBULA_GAME_API int32_t nebula_save_import_read(const uint8_t* bytes, uint32_t length, NebulaSaveImportDone done,
                                                           void* context) {
    if (bytes == nullptr || length == 0 || done == nullptr) {
        return NEBULA_SAVE_IMPORT_ARGUMENT;
    }
    u32 idle = Request_None;
    if (!__atomic_compare_exchange_n(&sRequest, &idle, Request_Setting, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return NEBULA_SAVE_IMPORT_BUSY;
    }
    sBytes = bytes;
    sLength = length;
    sDone = done;
    sContext = context;
    __atomic_store_n(&sRequest, Request_Ready, __ATOMIC_RELEASE);
    return NEBULA_SAVE_IMPORT_OK;
}
