#pragma once

#include "Game/System/NebulaAtomic.hpp"


#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JAudio2/JASOscillator.hpp"
#include "JSystem/JAudio2/JASRegisterParam.hpp"
#include "JSystem/JAudio2/JASSeqCtrl.hpp"
#include "JSystem/JAudio2/JASTrackPort.hpp"
#include "JSystem/JGadget/linklist.hpp"

struct JASSoundParams;
class JASChannel;
class JASDefaultBankTable;

struct JASVoice;

struct JASTrack : public JASPoolAllocObject_MultiThreaded< JASTrack > {
    static const int CHANNEL_MGR_MAX = 4;
    static const int TIMED_PARAMS = 6;
    static const int OSC_NUM = 2;

    enum Status {
        STATUS_FREE,
        STATUS_RUN,
        STATUS_STOPPED,
    };

    struct TChannelMgr : public JASPoolAllocObject_MultiThreaded< TChannelMgr > {
        TChannelMgr(JASTrack*);
        void init();
        void releaseAll();
        bool noteOff(u32, u16);
        void setPauseFlag(bool);

        static const int CHANNEL_MAX = 8;

        JASChannel* mChannels[CHANNEL_MAX];
        JASChannelParams mParams;
        s16 _38[CHANNEL_MAX];
        JASSoundParams* mSoundParams;
        JASTrack* mTrack;
    };

    struct TList {
        typedef JGadget::TLinkList< JASTrack, JGadget::NEBULA_NODE_OFFSET_OF_T > TrackList;
        typedef TrackList::iterator iterator;
        TList() : mCallbackRegistered() {
        }

        void append(JASTrack*);
        void seqMain();

        static s32 cbSeqMain(void*);

        TrackList mTracks;
        bool mCallbackRegistered;
    };

    struct MoveParam_ {
        f32 mValue;
        f32 mTarget;
        u32 mCount;
    };

    JASTrack();
    ~JASTrack();
    void setChannelMgrCount(u32);
    void init();
    void initTimed();
    void inherit(JASTrack const&);
    void assignExtBuffer(u32, JASSoundParams*);
    void setSeqData(void*, u32);
    void startSeq();
    void stopSeq();
    void start();
    void close();
    bool connectChild(u32, JASTrack*);
    void closeChild(u32);
    JASTrack* openChild(u32);
    void connectBus(int, int);
    f32 getVolume() const;
    f32 getPitch() const;
    f32 getPan() const;
    f32 getFxmix() const;
    f32 getDolby() const;
    void setLatestKey(u8);
    JASChannel* channelStart(JASTrack::TChannelMgr*, u32, u32, u32);
    bool noteOn(u32, u32, u32);
    bool gateOn(u32, u32, f32, u32);
    bool noteOff(u32, u16);
    bool checkNoteStop(u32) const;
    void overwriteOsc(JASChannel*);
    void updateTimedParam();
    void updateTrack(f32);
    void updateTempo();
    void updateSeq(bool, f32);
    u32 seqTimeToDspTime(f32);
    void setParam(u32, f32, u32);
    void noteOffAll(u16);
    void mute(bool);
    void setOscScale(u32, f32);
    // A table in a sequence's bytecode (high byte first), copied into the track in host order.
    void setOscTable(u32, u8 const*);
    void setOscAdsr(s16, s16, s16, s16, u16);
    // The coefficients as the sequence data has them (big-endian).
    void setFIR(u8 const*);
    void setIIR(s16 const*);
    u16 readPortSelf(u32);
    void writePortSelf(u32, u16);
    void writePort(u32, u16);
    u16 readPort(u32);
    void setChannelPauseFlag(bool);
    void pause(bool);
    s32 getTransposeTotal() const;
    bool isMute() const;
    void setTempo(u16);
    void setTempoRate(f32);
    // Its place from elsewhere: how many samples of its own time it should have played, from this call on (its next tick is
    // the clock's zero); below 0 for its own tempo's time.
    typedef f64 (*SecondsClock)(void*);
    void setSecondsClock(SecondsClock, void*);
    void setTimebase(u16);
    void updateChannel(JASChannel*, JASVoice*);
    JASTrack* getRootTrack();
    int tickProc();
    s32 seqMain();

    static void channelUpdateCallback(u32, JASChannel*, JASVoice*, void*);

    static JASOscillator::Point const sAdsTable[4];
    static JASOscillator::Data const sEnvOsc;
    static JASOscillator::Data const sPitchEnvOsc;

    static JASDefaultBankTable sDefaultBankTable;
    static TList sTrackList;

    static const int MAX_CHILDREN = 16;

    JASSeqCtrl* getSeqCtrl() {
        return &mSeqCtrl;
    }

    u16 getPort(u32 param_0) const {
        return mTrackPort.get(param_0);
    }

    void setPort(u32 param_0, u16 param_1) {
        mTrackPort.set(param_0, param_1);
    }

    u32 checkPortIn(u32 param_0) const {
        return mTrackPort.checkImport(param_0);
    }

    u32 checkPort(u32 param_0) const {
        return mTrackPort.checkExport(param_0);
    }

    u32 readReg(JASRegisterParam::RegID param_0) {
        return mRegisterParam.read(param_0);
    }

    void writeReg(JASRegisterParam::RegID param_0, u32 param_1) {
        mRegisterParam.write(param_0, param_1);
    }

    JASTrack* getParent() {
        return mParent;
    }

    JASTrack* getChild(u32 index) {
        return mChildren[index];
    }

    int getChannelMgrCount() const {
        return mChannelMgrCount;
    }

    f32 getVibDepth() const {
        return mVibDepth;
    }

    void setVibDepth(f32 param_0) {
        mVibDepth = param_0;
    }

    f32 getVibPitch() const {
        return mVibPitch;
    }

    void setVibPitch(f32 param_0) {
        mVibPitch = param_0;
    }

    f32 getTremDepth() const {
        return mTremDepth;
    }

    void setTremDepth(f32 param_0) {
        mTremDepth = param_0;
    }

    f32 getTremPitch() const {
        return mTremPitch;
    }

    void setTremPitch(f32 param_0) {
        mTremPitch = param_0;
    }

    u32 getVibDelay() const {
        return mVibDelay;
    }

    void setVibDelay(u32 param_0) {
        mVibDelay = param_0;
    }

    u32 getTremDelay() const {
        return mTremDelay;
    }

    void setTremDelay(u32 param_0) {
        mTremDelay = param_0;
    }

    int getStatus() const {
        return mStatus;
    }

    void setStatus(int status) {
        mStatus = status;
    }

    void setAutoDelete(bool param_0) {
        mIsOwnedByParent = param_0;
    }

    f32 getPanPower() const {
        return mPanPower;
    }

    void setPanPower(f32 param_0) {
        mPanPower = param_0;
    }

    u32 getSkipSample() const {
        return mSkipSample;
    }

    void setSkipSample(u32 param_0) {
        mSkipSample = param_0;
    }

    u16 getDirectRelease() const {
        return mDirectRelease;
    }

    void setDirectRelease(u16 param_0) {
        mDirectRelease = param_0;
    }

    u16 getTimebase() const {
        return mTimebase;
    }

    int getTranspose() const {
        return mTranspose;
    }

    void setTranspose(s32 param_0) {
        mTranspose = param_0;
    }

    u16 getBankNumber() const {
        return mBankNumber;
    }

    void setBankNumber(u16 param_0) {
        mBankNumber = param_0;
    }

    u16 getProgNumber() const {
        return mProgNumber;
    }

    void setProgNumber(u16 param_0) {
        mProgNumber = param_0;
    }

    u8 getBendSense() const {
        return mBendSense;
    }

    void setBendSense(u8 param_0) {
        mBendSense = param_0;
    }

    u8 getNoteOnPrio() const {
        return mNoteOnPrio;
    }

    void setNoteOnPrio(u8 param_0) {
        mNoteOnPrio = param_0;
    }

    u8 getReleasePrio() const {
        return mReleasePrio;
    }

    void setReleasePrio(u8 param_0) {
        mReleasePrio = param_0;
    }

    u8 getGateRate() const {
        return mGateRate;
    }

    void setGateRate(u8 param_0) {
        mGateRate = param_0;
    }

    JASSeqCtrl mSeqCtrl;
    JASTrackPort mTrackPort;
    JASRegisterParam mRegisterParam;
    union MoveParam_u {
        struct {
            MoveParam_ volume;
            MoveParam_ pitch;
            MoveParam_ fxmix;
            MoveParam_ pan;
            MoveParam_ dolby;
            MoveParam_ distFilter;
        } params;
        MoveParam_ array[TIMED_PARAMS];
        MoveParam_u() {
        }
    } mMoveParam;
    JASOscillator::Data mOscParam[OSC_NUM];
    JASOscillator::Point mOscPoint[4];
    static const u32 cOscTablePoints = 32;
    JASOscillator::Point mOscTable[OSC_NUM][cOscTablePoints];
    JASTrack* mParent;
    JASTrack* mChildren[MAX_CHILDREN];
    TChannelMgr* mChannelMgrs[CHANNEL_MGR_MAX];
    TChannelMgr mDefaultChannelMgr;
    u32 mChannelMgrCount;
    const JASDefaultBankTable* mBankTable;
    f32 mPlaytime;
    f32 mSampleInterval;
    f32 mVibDepth;
    f32 mVibPitch;
    f32 mTremDepth;
    f32 mTremPitch;
    u16 mVibDelay;
    u16 mTremDelay;
    short mFIR[8];
    short mIIR[8];
    u8 mFilterMode;
    f32 mPanPower;
    f32 mTempoRate;
    SecondsClock mSecondsClock;
    void* mSecondsClockData;
    f64 mClockTicks;
    u32 mSkipSample;
    u16 mDirectRelease;
    u16 mTempo;
    u16 mTimebase;
    s8 mTranspose;
    u8 mPitch;
    u16 mBankNumber;
    u16 mProgNumber;
    u8 mBendSense;
    u8 mNoteOnPrio;
    u8 mReleasePrio;
    u8 mGateRate;
    u16 mMixConfig[6];
    NebulaAtomic< s32 > mStatus;
    union {
        struct {
            bool mPauseFlag : 1;
            bool mIsMute : 1;
            bool mIsDirectlyPlayed : 1;
            bool mIsOwnedByParent : 1;
            bool mReadyToPlay : 1;
            bool mInvalidateSeq : 1;
            volatile bool mIsStopped : 1;
            bool flag7 : 1;
        };
        volatile u8 byteRepr;
    };
    JGadget::TLinkListNode mNode;

    // TList's node offset (JGadget::NEBULA_NODE_OFFSET_OF_T).
    static int nebulaListNodeOffset() {
        return -(int)offsetof(JASTrack, mNode);
    }

    // The sound whose output track this is (JAISe and JAISeq set it; route 1 an effect, 2 a sequence) for the host's
    // volume categories (nebula_game_audio_channel_owner); none on a sequence's child tracks.
    const class JAISound* mNebulaOwner = nullptr;
    u8 mNebulaOwnerRoute = 0;
};
