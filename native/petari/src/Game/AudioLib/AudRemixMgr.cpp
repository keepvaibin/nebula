#include "Game/AudioLib/AudRemixMgr.hpp"
#include "Game/System/NebulaArena.hpp"

AudRemixMgr::AudRemixMgr(NebulaArena* pArena) {
    mArena = pArena;
    mGroups = nullptr;
    mSoundObj = nullptr;
    mRemixSeq = nullptr;
}

void AudRemixMgr::init() {
    mSoundObj = new (mArena, 0) AudSoundObject(nullptr, 0x10, mArena);
    mRemixSeq = new (mArena, 0) AudRemixSequencer();
}

void AudRemixMgr::update() {
    mRemixSeq->update();
}

void AudRemixMgr::setRemixSeqResource(void* ptr) {
    u32* newPtr = (u32*)ptr;
    int groupCount = *(newPtr);
    newPtr++;
    mGroupCount = groupCount;
    newPtr += mGroupCount;
    mGroups = new (mArena, 0) RemixNoteGroupData[mGroupCount];
    for (int i = 0; i < mGroupCount; i++) {
        RemixNoteGroupData* pGroupData = &mGroups[i];
        pGroupData->mIndex = i;
        pGroupData->mTrackCount = *newPtr++;
        pGroupData->mNoteCount = *newPtr++;
        newPtr += pGroupData->mNoteCount;
        pGroupData->mRemixTracks = new (mArena, 0) RemixNoteTrackData[pGroupData->mTrackCount];
        for (int j = 0; j < pGroupData->mTrackCount; j++) {
            pGroupData->mRemixTracks[j].mTrackNo = *newPtr++;
            pGroupData->mRemixTracks[j].mNotes = reinterpret_cast< RemixNoteData* >(newPtr);
            newPtr += pGroupData->mNoteCount * 4;
        }
    }
}

RemixNoteGroupData* AudRemixMgr::getRemixNoteGroupDataFromMelodyNo(s32 melodyNo) const {
    for (int i = 0; i < mGroupCount; i++) {
        if (melodyNo == mGroups[i].mIndex) {
            return &mGroups[i];
        }
    }
    return nullptr;
}
