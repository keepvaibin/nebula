#include "Game/AudioLib/AudMeNameConverter.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/RhythmLib/AudRhythmMeSystem.hpp"

AudMeNameConverter::AudMeNameConverter() {
    init();
}

// -1 (cNoValue) for a name the table lacks.
u32 AudMeNameConverter::getMeID(const char* pName) const {
    return mIndex.find(pName);
}

void AudMeNameConverter::init() {
    const AudMePlayingParamsHolder& rParams = AudWrap::getRhythmMeSystem()->mMeMgr->mParams;
    mIndex.reset(rParams.mNumEntries);
    for (u32 i = 0; i < rParams.mNumEntries; i++) {
        mIndex.add(rParams.getName(i), i);
    }
}
