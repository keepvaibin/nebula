#include "Game/AudioLib/CSSoundNameConverter.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/Speaker/SpkTable.hpp"
#include "Game/Sound/SoundGlobal.hpp"

CSSoundNameConverter::CSSoundNameConverter() {
    init();
}

// -1 (cNoValue) for a name the table lacks.
u32 CSSoundNameConverter::getSoundID(const char* pSoundName) const {
    return mIndex.find(pSoundName);
}

void CSSoundNameConverter::init() {
    const SpkTable* pTable = &SoundGlobal< SpkSystem >::getInstance()->mData->mTable;
    mIndex.reset(pTable->mResourceCount);
    for (s32 i = 0; i < pTable->mResourceCount; i++) {
        mIndex.add(pTable->getName(i), i);
    }
}
