#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudAnmSoundObject;

class SoundEmitterCube : public AreaObj {
public:
    SoundEmitterCube(int formType, const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void movement();

    AudAnmSoundObject* mSoundObj;
    Vec3f mSoundPos;
    const char* mSoundName;
};
