#include "JSystem/JKernel/JKRAramStream.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "JSystem/JKernel/JKRAramPiece.hpp"
#include "JSystem/JSupport/JSUFileStream.hpp"
#include "JSystem/JUtility/JUTException.hpp"

const u32 stack_size = 0x4000;

JKRAramStream* JKRAramStream::sAramStreamObject;

JKRAramStream* JKRAramStream::create(s32 priority) {
    if (!sAramStreamObject) {
        sAramStreamObject = new (JKRGetSystemHeap(), 0) JKRAramStream(priority);
        JKRResetAramTransferBuffer();
    }

    return sAramStreamObject;
}

void* JKRAramStream::sMessageBuffer[4] = {
    NULL,
    NULL,
    NULL,
    NULL,
};

NebulaMessageQueue JKRAramStream::sMessageQueue = {0};

JKRAramStream::JKRAramStream(s32 priority) : JKRThread(stack_size, 0x10, priority) {
    resume();
}

JKRAramStream::~JKRAramStream() {
}

void* JKRAramStream::run() {
    sMessageQueue.init(sMessageBuffer, ARRAY_SIZEU(sMessageBuffer));

    for (;;) {
        void* message;
        sMessageQueue.receive(&message, true);
        JKRAramStreamCommand* command = (JKRAramStreamCommand*)message;

        switch (command->mType) {
        case JKRAramStreamCommand::READ:
            break;
        case JKRAramStreamCommand::WRITE:
            writeToAram(command);
            break;
        }
    }
}

s32 JKRAramStream::writeToAram(JKRAramStreamCommand* command) {
    u32 dstSize = command->mSize;
    u32 offset = command->mOffset;
    u32 writtenLength = 0;
    u32 destination = command->mAddress;
    u8* buffer = command->mTransferBuffer;
    u32 bufferSize = command->mTransferBufferSize;
    JKRHeap* heap = command->mHeap;

    if (buffer) {
        bufferSize = bufferSize == 0 ? 0x8000 : bufferSize;

        command->mTransferBufferSize = bufferSize;
        command->mAllocatedTransferBuffer = false;
    } else {
        bufferSize = bufferSize == 0 ? 0x8000 : bufferSize;

        if (heap) {
            buffer = (u8*)JKRAllocFromHeap(heap, bufferSize, -0x20);
            command->mTransferBuffer = buffer;
        } else {
            buffer = (u8*)JKRAllocFromSysHeap(bufferSize, -0x20);
            command->mTransferBuffer = buffer;
        }

        command->mTransferBufferSize = bufferSize;
        command->mAllocatedTransferBuffer = true;
    }

    if (!buffer) {
        if (!heap) {
            JKRGetCurrentHeap()->dump();
        } else {
            heap->dump();
        }

        JUTException::panic(__FILE__, 172, ":::Cannot alloc memory\n");
    }

    if (buffer) {
        command->mStream->seek(offset, SEEK_FROM_START);
        while (dstSize != 0) {
            u32 length = (dstSize > bufferSize) ? bufferSize : dstSize;

            s32 readLength = command->mStream->read(buffer, length);
            if (readLength == 0) {
                writtenLength = 0;
                break;
            }

            JKRAramPcs(0, (uintptr_t)buffer, destination, length, NULL);
            dstSize -= length;
            offset += length;
            writtenLength += length;
            destination += length;

            if (command->mReturnSize) {
                *command->mReturnSize += length;
            }
        }

        if (command->mAllocatedTransferBuffer) {
            JKRFree(buffer);
            command->mAllocatedTransferBuffer = false;
        }
    }

    command->mMessageQueue.send((void*)(intptr_t)writtenLength, false);
    return writtenLength;
}

u8* JKRAramStream::transBuffer;
u32 JKRAramStream::transSize;
JKRHeap* JKRAramStream::transHeap;

JKRAramStreamCommand* JKRAramStream::write_StreamToAram_Async(JSUFileInputStream* stream, u32 addr, u32 size, u32 offset, u32* returnSize) {
    JKRAramStreamCommand* command = new (JKRGetSystemHeap(), -4) JKRAramStreamCommand();
    command->mType = JKRAramStreamCommand::WRITE;
    command->mAddress = addr;
    command->mSize = size;
    command->mStream = stream;
    command->field_0x2c = 0;
    command->mOffset = offset;
    command->mTransferBuffer = transBuffer;
    command->mHeap = transHeap;
    command->mTransferBufferSize = transSize;
    command->mReturnSize = returnSize;
    if (returnSize) {
        *returnSize = 0;
    }

    command->mMessageQueue.init(&command->mMessage, 1);
    sMessageQueue.send(command, true);
    return command;
}

JKRAramStreamCommand* JKRAramStream::sync(JKRAramStreamCommand* command, BOOL isNonBlocking) {
    void* message;
    if (isNonBlocking == 0) {
        command->mMessageQueue.receive(&message, true);
        if (message == NULL) {
            return NULL;
        } else {
            return command;
        }
    } else {
        if (command->mMessageQueue.receive(&message, false) == FALSE) {
            return NULL;
        } else if (message == NULL) {
            return NULL;
        } else {
            return command;
        }
    }
}

void JKRAramStream::setTransBuffer(u8* buffer, u32 bufferSize, JKRHeap* heap) {
    transBuffer = NULL;
    transSize = 0x8000;
    transHeap = NULL;

    if (buffer) {
        transBuffer = (u8*)ALIGN_NEXT((uintptr_t)buffer, 0x20);
    }

    if (bufferSize) {
        transSize = ALIGN_PREV(bufferSize, 0x20);
    }

    if (heap && !buffer) {
        transHeap = heap;
    }
}
