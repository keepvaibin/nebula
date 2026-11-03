#pragma once


struct JASWaveArc;

struct JASWaveInfo {
    JASWaveInfo() {
        mBaseKey = 60;
        _24 = &one;
    }

    void copy(const JASWaveInfo& other) {
        mFormat = other.mFormat;
        mBaseKey = other.mBaseKey;
        mSampleRate = other.mSampleRate;
        mAWStartOffs = other.mAWStartOffs;
        mAWLength = other.mAWLength;
        mLoopFlags = other.mLoopFlags;
        mSampleLoopStart = other.mSampleLoopStart;
        mSampleLoopEnd = other.mSampleLoopEnd;
        mSampleCount = other.mSampleCount;
        _24 = other._24;
    }

    u8 mFormat;
    u8 mBaseKey;
    f32 mSampleRate;
    int mAWStartOffs;
    int mAWLength;
    u32 mLoopFlags;
    int mSampleLoopStart;
    int mSampleLoopEnd;
    int mSampleCount;
    const u32* _24;

    static u32 one;
};

class JASWaveHandle {
public:
    virtual ~JASWaveHandle() {
    }
    virtual const JASWaveInfo* getWaveInfo() const = 0;
    virtual const u8* getWavePtr() const = 0;
    virtual const u8* getWaveLimit() const = 0;
};

class JASWaveBank {
public:
    virtual ~JASWaveBank() {
    }
    virtual JASWaveHandle* getWaveHandle(u32) const = 0;
    virtual JASWaveArc* getWaveArc(u32) = 0;
    virtual u32 getArcCount() const = 0;
};
