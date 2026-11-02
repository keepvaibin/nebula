#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudAnmSoundObject;

class SoundEmitterSphere : public AreaObj {
public:
    SoundEmitterSphere(int formType, const char* pName);

    virtual ~SoundEmitterSphere();
    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    AudAnmSoundObject* mSoundObj;
    TVec3f _40;
    const char* mSoundName;
    s32 mSoundIndex;
};
