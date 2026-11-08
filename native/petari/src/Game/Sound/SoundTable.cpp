#include "Game/Sound/SoundTable.hpp"

void SoundTree::init(const SoundArchiveFile* pFile) {
    mFile = pFile;
    const u8* const base = reinterpret_cast< const u8* >(pFile);
    u32 at = sizeof(SoundArchiveFile);
    mSections = reinterpret_cast< const SoundSection* >(base + at);
    at += pFile->mSectionCount * sizeof(SoundSection);
    mGroups = reinterpret_cast< const SoundGroup* >(base + at);
    at += pFile->mGroupCount * sizeof(SoundGroup);
    mItems = reinterpret_cast< const SoundItem* >(base + at);
    at += pFile->mItemCount * sizeof(SoundItem) + pFile->mStaticCount * sizeof(SoundStatic) +
          (pFile->mBankCount + pFile->mWaveBankCount) * sizeof(SoundPart);
    mNames = reinterpret_cast< const char* >(base + at);
}

const SoundSection* SoundTree::section(SoundID id, u32 in) const {
    const u32 index = id.getSectionID();
    if (index >= mFile->mSectionCount || (mSections[index].mIn & in) == 0) {
        return nullptr;
    }
    return &mSections[index];
}

const SoundItem* SoundTable::findItem(SoundID id) const {
    if (id.isAnonymous()) {
        return nullptr;
    }
    const SoundSection* pSection = section(id, cInTable);
    const u32 groupID = id.getGroupID();
    if (pSection == nullptr || groupID >= pSection->mGroupCount) {
        return nullptr;
    }
    const SoundGroup& rGroup = mGroups[pSection->mGroupFirst + groupID];
    const u32 waveID = id.getWaveID();
    if ((rGroup.mIn & cInTable) == 0 || waveID >= rGroup.mItemCount) {
        return nullptr;
    }
    return &mItems[rGroup.mItemFirst + waveID];
}

u8 SoundTable::getTypeID(SoundID id) const {
    const SoundItem* const pItem = findItem(id);
    return pItem != nullptr ? pItem->mType : 0xff;
}

const SoundItem* SoundTable::getData(SoundID id) const {
    const SoundItem* const pItem = findItem(id);
    return pItem != nullptr && (pItem->mIn & cInTable) != 0 ? pItem : nullptr;
}

int SoundNameTable::getNumGroups_inSection(u8 sectionID) const {
    if (sectionID >= mFile->mSectionCount || (mSections[sectionID].mIn & cInNames) == 0) {
        return -1;
    }
    return static_cast< int >(mSections[sectionID].mNameGroupCount);
}

int SoundNameTable::getNumItems_inGroup(u8 sectionID, u8 groupID) const {
    if (sectionID >= mFile->mSectionCount || (mSections[sectionID].mIn & cInNames) == 0) {
        return -1;
    }
    const SoundSection& rSection = mSections[sectionID];
    if (groupID >= rSection.mNameGroupCount) {
        return -1;
    }
    const SoundGroup& rGroup = mGroups[rSection.mGroupFirst + groupID];
    return (rGroup.mIn & cInNames) != 0 ? static_cast< int >(rGroup.mNameCount) : -1;
}

const char* SoundNameTable::getName(SoundID id) const {
    if (id.isAnonymous()) {
        return "<Anonymous>";
    }
    const SoundSection* pSection = section(id, cInNames);
    const u32 groupID = id.getGroupID();
    if (pSection == nullptr || groupID >= pSection->mNameGroupCount) {
        return "";
    }
    const SoundGroup& rGroup = mGroups[pSection->mGroupFirst + groupID];
    if ((rGroup.mIn & cInNames) == 0) {
        return "";
    }
    const u32 waveID = id.getWaveID();
    if (waveID >= rGroup.mNameCount) {
        return nullptr;
    }
    const SoundItem& rItem = mItems[rGroup.mItemFirst + waveID];
    return rItem.mName != ~0u ? mNames + rItem.mName : nullptr;
}
