#pragma once

#include "Game/RhythmLib/AudMeChannelMgr.hpp"
#include "Game/RhythmLib/AudMeSeqCtrl.hpp"
#include "Game/RhythmLib/AudMeTrackCallback.hpp"
#include <JSystem/JAudio2/JASBankTable.hpp>
#include <JSystem/JAudio2/JASVoice.hpp>
#include <JSystem/JGadget/linklist.hpp>
#include <JSystem/JAudio2/JASHeapCtrl.hpp>
#include <JSystem/JAudio2/JASOscillator.hpp>

class AudMeTrack : public JASPoolAllocObject< AudMeTrack > {
public:
    static const int OSC_NUM = 2;
    static const s32 MAX_CHILDREN = 8;
    static const s32 CHANNEL_MGR_MAX = 4;
    static const s32 NUM_REGS = 64;

    enum Status { STATUS_FREE = 0, STATUS_READY = 1, STATUS_RUN = 2, STATUS_START = 3 };

    enum Oscillator {
        Oscillator_Env = 0,
        Oscillator_Pitch = 1,
    };

    enum OscillatorPoint {
        OscillatorPoint_Attack = 0,
        OscillatorPoint_Decay = 1,
        OscillatorPoint_Systain = 2,
        OscillatorPoint_Release = 3,
    };

    enum ModifyType {
        ModifyType_Chord = 0,
        ModifyType_Scale = 1,
        ModifyType_Steps = 2,
        ModifyType_Octave = 3,
    };

    enum ModifyDir {
        ModifyDir_Up = 0,
        ModifyDir_Down = 1,
        ModifyDir_None = 2,
        ModifyDir_Rand = 3,
    };

    class TTrackInfo {
    public:
        void init();

        f32 getBendSense() const {
            return mBendSense;
        }

        u8 mVelocity;
        u16 mBankNumber;
        u16 mProgNumber;
        u8 mBendSense;
        u16 mRelease;
        u8 mPlayPriority;
        u8 mReleasePriority;
        u32 mNoteReg;
        u8 mNoteRangeLower;
        u8 mNoteRangeUpper;
        u8 mNoteRangeStart;
    };

    struct TList : JGadget::TLinkList< AudMeTrack, JGadget::NEBULA_NODE_OFFSET_OF_T > {
        TList() : mCallbackRegistered(false) {
        }

        ~TList() {
        }

        static s32 cbSeqMain(s32, s32, void*);

        void append(AudMeTrack*);
        void seqMain(s32, s32);
        AudMeTrack* getSameME(AudMeTrack*, u32);

        bool mCallbackRegistered;
    };

    operator JGadget::TLinkListNode() {
        return mNode;
    }

    AudMeTrack();
    ~AudMeTrack();

    void init();
    void initPlayParam();
    void inherit(AudMeTrack*);
    AudMeTrack* newChild(s32);
    bool deleteChild(s32);
    bool isHaveChild() const;
    void setSeqData(void*, u32);
    void startSeq();
    void stopSeq();
    void suspendSeq();
    void stopAtCurrentNoteEnd();
    s32 rhythmProc(s32, s32);
    void setPauseFlag(bool);
    JASChannel* channelStart(AudMeChannelMgr*, u32, u32, u32);
    bool noteOn(u32, u32, u32);
    bool noteOff(u32, u16);
    void noteOffAll(u16);
    void startNote();
    static void channelUpdateCallback(u32, JASChannel*, JASVoice*, void*);
    static void initRegister();
    void overwriteOsc(JASChannel*);
    void setOscAdsr(s16, s16, s16, s16, u16);
    void updateChannelParams();
    bool gframeProc();
    void setBaseNoteFromChord(u8*, s32, bool);
    void setBaseNoteFromScale(u8*, s32, bool);
    void setBaseNoteFromChromatic(u8*, s32, bool);
    void setBaseNoteFromOtherPart(s32);
    void setBaseNoteFromUpperPart();
    void setBaseNoteFix(u8);
    void setBaseNoteFromRegister(s32);
    void adjustNote(s32, s32);
    void modifyNote(s32, s32, s32);
    void modifyNoteRnd(s32, s32, s32);
    u32 getRandom(u32);
    void makeRandomSearchList(s32*, s32, bool);
    void adjustInNoteRange(u8&);
    void adjustInStartNoteRange(u8&);
    s32 getMyOrderInBrother();
    AudMeTrack* getBrother(s32);

    AudMeSeqCtrl* getSeqCtrl() {
        return &mSeqCtrl;
    }

    bool getFlagZ() const {
        return mJumpFlag == 0;
    }

    bool getFlagNZ() const {
        return mJumpFlag != 0;
    }

    void setJumpFlag(bool flag) {
        mJumpFlag = flag;
    }
    void clearJumpFlag() {
        mJumpFlag = false;
    }

    u8 getVelocity() const {
        return mTrackInfo.mVelocity;
    }
    void setVelocity(u8 velocity) {
        mTrackInfo.mVelocity = velocity;
    }
    u16 getBankNumber() const {
        return mTrackInfo.mBankNumber;
    }
    void setBankNumber(u16 bankNo) {
        mTrackInfo.mBankNumber = bankNo;
    }
    u16 getProgNumber() const {
        return mTrackInfo.mProgNumber;
    }
    void setProgNumber(u16 progNo) {
        mTrackInfo.mProgNumber = progNo;
    }
    u16 getRelease() const {
        return mTrackInfo.mRelease;
    }
    void setRelease(u16 release) {
        mTrackInfo.mRelease = release;
    }
    u8 getNoteRangeLower() const {
        return mTrackInfo.mNoteRangeLower;
    }
    void setNoteRangeLower(u8 lower) {
        mTrackInfo.mNoteRangeLower = lower;
    }
    u8 getNoteRangeUpper() const {
        return mTrackInfo.mNoteRangeUpper;
    }
    void setNoteRangeUpper(u8 upper) {
        mTrackInfo.mNoteRangeUpper = upper;
    }
    u8 getNoteRangeStart() const {
        return mTrackInfo.mNoteRangeStart;
    }
    void setNoteRangeStart(u8 start) {
        mTrackInfo.mNoteRangeStart = start;
    }
    void setNoteNoStartRange(bool noStart) {
        mNoStartRange = noStart;
    }
    void setPan(u8 pan) {
        mPan = pan / 127.0f;
    }
    void setDolby(u8 dolby) {
        mDolby = dolby / 127.0f;
    }
    void setVolume(u16 volume) {
        mVolume = volume / 32767.0f;
    }
    void setPitchBend(s16 bend) {
        mPitchBend = bend / 32767.0f;
    }
    void setFxMix(u16 fxMix) {
        mFxMix = fxMix / 32767.0f;
    }
    void setBendSense(u8 sense) {
        mTrackInfo.mBendSense = sense;
    }
    void setNoteReg(u32 reg) {
        mTrackInfo.mNoteReg = reg;
    }

    u8 getBaseNotePrev() const {
        return mBaseNotePrev;
    }
    void setBaseNote(u8 note) {
        mBaseNote = note;
    }

    AudMeTrack* getChild(s32 childNo) {
        return mChildren[childNo];
    }

    bool hasChild(s32 childNo) const {
        return mChildren[childNo] != nullptr;
    }

    bool isMainTrack() const {
        return mParent == nullptr;
    }

    AudMeTrack* getParent() {
        return mParent;
    }

    f32 getPitchBend() const {
        return mPitchBend;
    }

    f32 getPitch() const {
        return 1.0f;
    }

    f32 getVolume() const {
        return mVolume;
    }

    f32 getDolby() const {
        return mDolby;
    }

    f32 getPan() const {
        return mPan;
    }

    f32 getFxMix() const {
        return mFxMix;
    }

    static u8 sReg[NUM_REGS];
    static TList sTrackList;

    static JASOscillator::Point const sAdsTable[4];
    static JASOscillator::Data const sEnvOsc;
    static JASOscillator::Data const sPitchEnvOsc;

    AudMeSeqCtrl mSeqCtrl;
    AudMeTrack* mParent;
    AudMeTrack* mChildren[MAX_CHILDREN];
    s32 mBrotherNo;
    AudMeChannelMgr* mChannelMgrs[CHANNEL_MGR_MAX];
    AudMeChannelMgr mDefaultChannelMgr;
    u32 mNumChannelMgrs;
    bool mIsPaused;
    bool mIsProcStopped;
    s32 mStatus;
    const JASDefaultBankTable* mBankTable;
    JASOscillator::Data mOscParam[OSC_NUM];
    JASOscillator::Point mOscPoint[4];
    f32 mVolume;
    f32 mPitchBend;
    f32 mFxMix;
    f32 mPan;
    f32 mDolby;
    u8 mBaseNote;
    u8 mBaseNotePrev;
    bool mIsInStart;
    bool mStopAfterNote;
    bool mNoStartRange;
    u32 mMeId;
    s16 mChordTableId;
    bool mJumpFlag;
    u8 _163[MAX_CHILDREN];  // AudMeSeqParser::cmdCmpCI
    TTrackInfo mTrackInfo;
    JGadget::TLinkListNode mNode;

    // TList's node offset (JGadget::NEBULA_NODE_OFFSET_OF_T).
    static int nebulaListNodeOffset() {
        return -(int)offsetof(AudMeTrack, mNode);
    }
};
