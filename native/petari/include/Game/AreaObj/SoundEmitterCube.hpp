#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudAnmSoundObject;

class SoundEmitterCube : public AreaObj {
public:
    SoundEmitterCube(int formType, const char* pName);

    virtual ~SoundEmitterCube();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    AudAnmSoundObject* mSoundObj;
    TVec3f _40;
    const char* mSoundName;
};
