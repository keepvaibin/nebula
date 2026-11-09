#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"


class Table;
class ParticleResources;
struct ResTIMG;

class ParticleResourceHolder {
public:
    // An AutoEffectList group (GroupName, ASCII case folded): its rows in table order.
    struct EffectNum {
        const char* mGroupName;  // as its first row spells it
        int mCount;
        s32 mFirstRow;
        s32 mLastRow;
    };

    ParticleResourceHolder(const char* pArchiveName);

    u16 getUserIndex(const char* pName) const;
    void countAutoEffectNum();
    void swapTexture(const ResTIMG* pImage, const char* pName);
    bool isExistInResource(const char* pName, u16* pIndex) const;
    Table* getAutoEffectListBinary() const;
    int getAutoEffectNum(const char* pGroupName) const;
    // The group's first row, and the row after `row` in its group; -1: none.
    s32 getAutoEffectFirstRow(const char* pGroupName) const;
    s32 getAutoEffectNextRow(s32 row) const;
    // Nebula: a resource's name in ParticleNames.bcsv by its user index (ParticleResource::getUsrIdx; null: none).
    const char* getParticleName(u32 index) const;

    ParticleResources* mResourceMgr;
    Table* mAutoEffectList;
    Table* mParticleNames;
    EffectNum* mEffectNums;
    int mNumEffectNums;
    MR::NameIndexOf< MR::NameKeyCase > mGroupIndex;  // group name -> its EffectNum
    s32* mNextRows;                                  // per row: the next row of its group, -1 after the last
    MR::NameIndex mUserIndices;                      // ParticleNames "name" -> its row (the user index), the first row of a name
};
