#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudSoundObject;

class GlaringLightArea : public AreaObj {
public:
    GlaringLightArea(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    virtual const char* getManagerName() const override {
        return "GlaringLightArea";
    }

    AudSoundObject* mSoundObj;
    Vec3f mPos;
};

class GlaringLightAreaMgr : public AreaObjMgr {
public:
    GlaringLightAreaMgr(s32, const char*);

};
