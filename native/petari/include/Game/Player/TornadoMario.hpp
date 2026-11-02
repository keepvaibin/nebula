#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TornadoMario : public LiveActor {
public:
    TornadoMario(const char* pName);

    virtual ~TornadoMario();
    virtual void init(const JMapInfoIter& rIter) override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void hideForce();
    void setTrHeight(const TVec3f& rPos, const TVec3f& rFront, const TVec3f& rUp, const TVec3f& rShadowDir);
    void show();
    void hide();

    bool _8C;
    bool mIsVisible;
    bool _8E;
    bool _8F;
    s32 _90;
    s32 _94;
    s32 _98;
    TVec3f mUp;
    TVec3f mFront;
};
