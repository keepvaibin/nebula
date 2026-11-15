#include "Game/System/SaveFileSummary.hpp"
#include "Game/System/SaveFileId.hpp"
#include "Game/System/UserFile.hpp"

bool readSaveFileSummary(UserFile* pScratch, int user, const u8* const ppParts[], const u32 pSizes[], SaveFileSummary* pOut) {
    *pOut = {false, false, 0, 0};
    pScratch->resetAllData();
    if (pSizes[SaveFileId::Kind_Config] == 0) {
        return true;  // never created
    }
    pScratch->loadFromConfigDataBinary({SaveFileId::Kind_Config, user}, ppParts[SaveFileId::Kind_Config],
                                       pSizes[SaveFileId::Kind_Config]);
    if (pScratch->mIsConfigDataCorrupted) {
        return false;
    }
    if (!pScratch->isCreated()) {
        return true;
    }
    const bool luigi = !pScratch->isLastLoadedMario();
    const SaveFileId::Kind kind = luigi ? SaveFileId::Kind_Luigi : SaveFileId::Kind_Mario;
    if (pSizes[kind] == 0) {
        return false;
    }
    pScratch->loadFromGameDataBinary({kind, user}, ppParts[kind], pSizes[kind]);
    if (pScratch->mIsGameDataCorrupted) {
        return false;
    }
    pOut->mExists = true;
    pOut->mLuigi = luigi;
    pOut->mStars = static_cast< u32 >(pScratch->getPowerStarNum());
    pOut->mSaved = pScratch->getLastModified();
    return true;
}
