#include "Game/System/NANDManagerThread.hpp"
#include "Game/System/NANDManager.hpp"
#include "Game/System/NebulaFrameClock.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // One save file operation on the host (B-13: the game's saves are host files, not a NAND): started, then waited
    // for a frame at a time while the host works on its own thread.
    s32 runSaveRequest(u32 op, const char* pName, void* pBuffer, u32 size, u32* pLength) {
        volatile s32 status = NEBULA_SAVE_PENDING;
        const NebulaSaveRequest request = {op, pName, pBuffer, size, pLength, &status};
        const NebulaHostServices* const host = nebula_host();
        host->save_request(host->user, &request);
        while (__atomic_load_n(&status, __ATOMIC_ACQUIRE) == NEBULA_SAVE_PENDING) {
            NebulaFrameClock::waitFrame();
        }
        return status;
    }

    // The result the save sequences interpret (NANDResultCode).
    s32 toResultCode(s32 status) {
        switch (status) {
        case NEBULA_SAVE_DONE:
            return NAND_RESULT_OK;
        case NEBULA_SAVE_NO_FILE:
            return NAND_RESULT_NOEXISTS;
        case NEBULA_SAVE_TOO_LARGE:
            return NAND_RESULT_AUTHENTICATION;
        default:
            return NAND_RESULT_ACCESS;
        }
    }
}  // namespace

NANDManagerThread::NANDManagerThread(int priority, int msgCount, JKRHeap* pHeap) : OSThreadWrapper(0x8000, msgCount, priority, pHeap) {
}

void* NANDManagerThread::run() {
    while (true) {
        OSMessage msg;
        OSReceiveMessage(&mQueue, &msg, OS_MESSAGE_BLOCK);

        NANDRequestInfo* pRequestInfo = static_cast< NANDRequestInfo* >(msg);
        pRequestInfo->_40 = 2;

        switch (pRequestInfo->mType) {
        case 1:
            pRequestInfo->mResult = toResultCode(runSaveRequest(NEBULA_SAVE_DELETE, pRequestInfo->mPath, nullptr, 0, nullptr));
            break;
        case 2:
            executeWriteSequence(pRequestInfo);
            break;
        case 3:
            executeReadSequence(pRequestInfo);
            break;
        case 4:
            // Room to create the save: a host's disk has it.
            *pRequestInfo->mCheckAnswer = 0;
            pRequestInfo->mResult = NAND_RESULT_OK;
            break;
        }

        pRequestInfo->_40 = 0;

        if (pRequestInfo->_54 != nullptr) {
            (pRequestInfo->_54)(pRequestInfo);
        }
    }
}

void NANDManagerThread::executeWriteSequence(NANDRequestInfo* pRequestInfo) {
    const s32 status = runSaveRequest(NEBULA_SAVE_WRITE, pRequestInfo->mPath, const_cast< void* >(pRequestInfo->mWriteBuf),
                                      pRequestInfo->mFsBlock, nullptr);
    pRequestInfo->mResult = toResultCode(status);
    if (status != NEBULA_SAVE_DONE) {
        pRequestInfo->mFsBlock = 0;
    }
}

void NANDManagerThread::executeReadSequence(NANDRequestInfo* pRequestInfo) {
    u32 length = 0;
    const s32 status = runSaveRequest(NEBULA_SAVE_READ, pRequestInfo->mPath, pRequestInfo->mReadBuf, pRequestInfo->mFsBlock, &length);
    if (pRequestInfo->mReadLength != nullptr && status != NEBULA_SAVE_NO_FILE) {
        *pRequestInfo->mReadLength = length;
    }
    pRequestInfo->mResult = toResultCode(status);
    pRequestInfo->mFsBlock = status == NEBULA_SAVE_DONE ? length : 0;
}
