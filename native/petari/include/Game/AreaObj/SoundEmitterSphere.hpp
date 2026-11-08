#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudAnmSoundObject;

class SoundEmitterSphere : public AreaObj {
public:
    SoundEmitterSphere(int formType, const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void movement();

    AudAnmSoundObject* mSoundObj;
    Vec3f mSoundPos;
    const char* mSoundName;
    s32 mSoundIndex;
};
