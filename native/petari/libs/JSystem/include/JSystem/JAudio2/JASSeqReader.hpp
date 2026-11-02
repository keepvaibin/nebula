#pragma once


class JASSeqReader {
public:
    JASSeqReader() {
        init();
    }
    void init();
    void init(void*);
    bool call(u32);
    bool loopStart(u32);
    bool loopEnd();
    bool ret();
    u32 readMidiValue();
    void* getStackPtr(u32 idx) const;

    void jump(u32 param_1) {
        mSeqCursor = mSeqBuff + param_1;
    }

    void jump(void* param_1) {
        mSeqCursor = (u8*)param_1;
    }

    // A sequence's bytecode holds its operands high byte first: read as bytes, whatever the host's order.
    u32 get24(u32 param_0) const {
        const u8* const p = mSeqBuff + param_0;
        return (p[0] << 16) | (p[1] << 8) | p[2];
    }

    u32* getBase() {
        return (u32*)mSeqBuff;
    }
    u32 getOffset() {
        return (u32)((const u8*)mSeqCursor - (const u8*)mSeqBuff);
    }
    u8* getAddr(u32 param_0) {
        return mSeqBuff + param_0;
    }
    u8 getByte(u32 param_0) const {
        return *(mSeqBuff + param_0);
    }
    u16 get16(u32 param_0) const {
        const u8* const p = mSeqBuff + param_0;
        return static_cast< u16 >((p[0] << 8) | p[1]);
    }
    u32 get32(u32 param_0) const {
        const u8* const p = mSeqBuff + param_0;
        return (static_cast< u32 >(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }
    u8* getCur() {
        return mSeqCursor;
    }
    u32 readByte() {
        return *mSeqCursor++;
    }
    u32 read16() {
        const u32 value = (mSeqCursor[0] << 8) | mSeqCursor[1];
        mSeqCursor += 2;
        return value;
    }
    u32 read24() {
        const u32 value = (mSeqCursor[0] << 16) | (mSeqCursor[1] << 8) | mSeqCursor[2];
        mSeqCursor += 3;
        return value;
    }
    u16 getLoopCount() const {
        if (mNumStacks == 0) {
            return 0;
        }
        return mLoopCounts[mNumStacks - 1];
    }

    u8* mSeqBuff;
    u8* mSeqCursor;
    u32 mNumStacks;
    u8* mStackPtrs[8];
    u16 mLoopCounts[8];
};
