#include "JSystem/JKernel/JKRAramPiece.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "JSystem/JKernel/JKRAram.hpp"
#include "JSystem/JKernel/JKRAramStream.hpp"
#include "JSystem/JKernel/JKRDecomp.hpp"
#include "JSystem/JUtility/JUTException.hpp"

JKRAMCommand* JKRAramPiece::prepareCommand(int direction, uintptr_t src, uintptr_t dst, u32 length, JKRAramBlock* block, JKRAMCommand::AsyncCallback callback) {
    JKRAMCommand* command = new (JKRGetSystemHeap(), -4) JKRAMCommand();
    command->mTransferDirection = direction;
    command->mSrc = src;
    command->mDst = dst;
    command->mAramBlock = block;
    command->mDataLength = length;
    command->mCallback = callback;
    return command;
}

void JKRAramPiece::sendCommand(JKRAMCommand* command) {
    startDMA(command);
}

JSUList< JKRAMCommand > JKRAramPiece::sAramPieceCommandList;
NebulaMutex JKRAramPiece::mMutex;

JKRAMCommand* JKRAramPiece::orderAsync(int direction, uintptr_t source, uintptr_t destination, u32 length, JKRAramBlock* block,
                                       JKRAMCommand::AsyncCallback callback) {
    lock();
    if ((source & 0x1f) != 0 || (destination & 0x1f) != 0) {
        JUTNativeLog("direction = %x\n", direction);
        JUTNativeLog("source = %x\n", source);
        JUTNativeLog("destination = %x\n", destination);
        JUTNativeLog("length = %x\n", length);
        JUTException::panic(__FILE__, 108, "illegal address. abort.");
    }

    JKRAramCommand* message = new (JKRGetSystemHeap(), -4) JKRAramCommand();
    JKRAMCommand* command = JKRAramPiece::prepareCommand(direction, source, destination, length, block, callback);
    message->setting(1, command);

    (JKRAram::sMessageQueue).send(message, true);
    if (command->mCallback != NULL) {
        sAramPieceCommandList.append(&command->mPieceLink);
    }

    unlock();
    return command;
}

BOOL JKRAramPiece::sync(JKRAMCommand* command, int is_non_blocking) {
    void* message;

    lock();
    if (is_non_blocking == 0) {
        command->mMessageQueue.receive(&message, true);
        sAramPieceCommandList.remove(&command->mPieceLink);
        unlock();
        return TRUE;
    }

    if (!command->mMessageQueue.receive(&message, false)) {
        unlock();
        return FALSE;
    }

    sAramPieceCommandList.remove(&command->mPieceLink);
    unlock();
    return TRUE;
}

BOOL JKRAramPiece::orderSync(int direction, uintptr_t source, uintptr_t destination, u32 length, JKRAramBlock* block) {
    lock();

    JKRAMCommand* command = JKRAramPiece::orderAsync(direction, source, destination, length, block, NULL);
    BOOL result = JKRAramPiece::sync(command, 0);
    delete command;

    unlock();
    return result;
}

void JKRAramPiece::doneDMA(uintptr_t requestAddress) {
    JKRAMCommand* command = reinterpret_cast< JKRAMCommand* >(requestAddress);

    if (command->field_0x60 != 0) {
        if (command->field_0x60 == 2) {
            JKRDecompress_SendCommand(command->mDecompCommand);
        }
    } else if (command->mCallback) {
        (*command->mCallback)(requestAddress);
    } else if (command->field_0x5C) {
        command->field_0x5C->send(command, false);
    } else {
        command->mMessageQueue.send(command, false);
    }
}

JKRAMCommand::JKRAMCommand() : mPieceLink(this), field_0x30(this) {
    mMessageQueue.init(&mMessage, 1);
    mCallback = NULL;
    field_0x5C = NULL;
    field_0x60 = 0;
    field_0x8C = NULL;
    field_0x90 = NULL;
    field_0x94 = NULL;
}

JKRAMCommand::~JKRAMCommand() {
    if (field_0x8C)
        delete field_0x8C;
    if (field_0x90)
        delete field_0x90;

    if (field_0x94)
        JKRFree(field_0x94);
}
