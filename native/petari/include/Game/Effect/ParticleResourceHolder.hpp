#pragma once

#include "Game/Util/Array.hpp"


class Table;
class ParticleResources;
struct ResTIMG;

class ParticleResourceHolder {
public:
    struct EffectNum {
        EffectNum(const char* pGroupName) : mGroupName(pGroupName), mCount(1) {
        }

        const char* mGroupName;
        int mCount;
    };

    ParticleResourceHolder(const char* pArchiveName);

    u16 getUserIndex(const char* pName) const;
    void countAutoEffectNum();
    void swapTexture(const ResTIMG* pImage, const char* pName);
    bool isExistInResource(const char* pName, u16* pIndex) const;
    Table* getAutoEffectListBinary() const;
    int getAutoEffectNum(const char* pGroupName) const;
    // Nebula: a resource's name in ParticleNames.bcsv by its user index (ParticleResource::getUsrIdx; null: none).
    const char* getParticleName(u32 index) const;

    ParticleResources* mResourceMgr;
    Table* mAutoEffectList;
    Table* mParticleNames;
    MR::GrowTable< EffectNum*, 1024 > mEffectNums;
    int mNumEffectNums;
};
