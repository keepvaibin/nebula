#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudAnmSoundObject;

class SoundEmitterCube : public AreaObj {
public:
    SoundEmitterCube(int formType, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

    AudAnmSoundObject* mSoundObj;
    Vec3f mSoundPos;
    const char* mSoundName;
};
