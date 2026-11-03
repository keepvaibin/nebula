#pragma once

#include "Game/Util/Array.hpp"


class JMapInfo;
class JPAResourceManager;
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
    JMapInfo* getAutoEffectListBinary() const;
    int getAutoEffectNum(const char* pGroupName) const;
    // Nebula: a resource's name in ParticleNames.bcsv by its user index (JPAResource::getUsrIdx; null: none).
    const char* getParticleName(u16 index) const;

    JPAResourceManager* mResourceMgr;
    JMapInfo* mAutoEffectList;
    JMapInfo* mParticleNames;
    MR::GrowTable< EffectNum*, 1024 > mEffectNums;
    int mNumEffectNums;
};
