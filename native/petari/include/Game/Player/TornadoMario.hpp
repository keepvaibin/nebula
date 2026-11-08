#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TornadoMario : public LiveActor {
public:
    TornadoMario(const char* pName);

    virtual ~TornadoMario();
    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void hideForce();
    void setTrHeight(const Vec3f& rPos, const Vec3f& rFront, const Vec3f& rUp, const Vec3f& rShadowDir);
    void show();
    void hide();

    bool mIsVisible;
    Vec3f mUp;
    Vec3f mFront;
};
