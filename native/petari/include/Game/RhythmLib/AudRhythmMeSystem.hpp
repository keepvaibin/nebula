#pragma once

#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudRhythmHolder.hpp"
#include "Game/RhythmLib/AudRhythmSeqParser.hpp"
#include "Game/RhythmLib/AudRhythmWrap.hpp"
#include <JSystem/JAudio2/JASGlobal.hpp>

class JAISoundHandle;
class NebulaArena;
class AudMeMgr;

class AudRhythmMeSystem : public JASGlobalInstance< AudRhythmMeSystem > {
public:
    AudRhythmMeSystem(NebulaArena*, u32, bool);

    AudRhythmMeSystem();

    bool setSeq(JAISoundHandle&, s32);
    void rejectSeq(s32);
    void rhythmProc();
    void update();
    void setIdle(bool);
    void setUsingRhythmParser(u32);

    s32 getBgmIdx() const {
        return mBgmIdx;
    }

    AudRhythmSeqParser* getSeqParser(int index) const {
        return mSeqParsers[index];
    }

    AudRhythmSeqParser** mSeqParsers;
    u32 mNumSeqParsers;
    s32 mBgmIdx;
    u32 _C;
    AudRhythmHolder* mRhythmHolders;
    AudMeMgr* mMeMgr;
    AudChordInfo mChordInfo;
};
