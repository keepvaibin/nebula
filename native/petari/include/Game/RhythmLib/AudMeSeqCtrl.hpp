#pragma once

#include "Game/RhythmLib/AudMeSeqParser.hpp"
#include "Game/RhythmLib/AudMeSeqReader.hpp"

class AudMeTrack;

class AudMeSeqCtrl {
public:
    AudMeSeqCtrl();

    void start(void*, u32);
    void wait(s32, s32);
    void autoWait();
    void inherit(AudMeSeqCtrl*);
    s32 rhythmProc(AudMeTrack*, s32);

    AudMeSeqReader* getSeqReader() {
        return &mReader;
    }
    void* getBase() {
        return mReader.getBase();
    }
    void* getAddr(u32 addr) {
        return mReader.getAddr(addr);
    }
    u8 getByte(u32 addr) const {
        return mReader.getByte(addr);
    }
    u16 get16(u32 addr) const {
        return mReader.get16(addr);
    }
    u32 get24(u32 addr) const {
        return mReader.get24(addr);
    }
    u32 get32(u32 addr) const {
        return mReader.get32(addr);
    }
    void* getCur() {
        return mReader.getCur();
    }
    u32 read8() {
        return mReader.read8();
    }
    u32 read16() {
        return mReader.read16();
    }
    u32 read24() {
        return mReader.read24();
    }
    bool call(u32 addr) {
        return mReader.call(addr);
    }
    bool ret() {
        return mReader.ret();
    }
    void jump(u32 addr) {
        mReader.jump(addr);
    }
    bool loopStart(u32 loopCount) {
        return mReader.loopStart(loopCount);
    }
    bool loopEnd() {
        return mReader.loopEnd();
    }

    void setProgress(s32 progress) {
        mProgress = progress;
    }

    void lock() {
        mIsLocked = true;
    }
    void unlock() {
        mIsLocked = false;
    }

    static AudMeSeqParser sDefaultParser;

    s32 mType;
    s32 mTime;  // TODO: better name, "note time?"
    AudMeSeqReader mReader;
    AudMeSeqParser* mParser;
    bool mIsLocked;
    bool mStopAfterNote;
    s32 mProgress;
};
