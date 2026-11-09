#pragma once

#include "Game/Util/Array.hpp"

#include "Game/LiveActor/LiveActor.hpp"

class Air : public LiveActor {
public:
    Air(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void initModel(const char*);
    virtual void setFarClipping();

    bool isDrawing() const;
    bool tryChange();
    void appearFadeOut();
    void appearFadeIn();
    void exeIn();
    void exeOut();

    u8 mIsChanged;
    bool mIsWaitStartCamera;
    f32 mDistance;
};

class AirFar100m : public Air {
public:
    AirFar100m(const char*);

    virtual void setFarClipping() override;
};

class ProjectionMapAir : public Air {
public:
    ProjectionMapAir(const char*);

    virtual void initModel(const char*) override;
};

class PriorDrawAir : public Air {
public:
    PriorDrawAir(const char*);
};

class PriorDrawAirHolder : public NameObj {
public:
    PriorDrawAirHolder();

    void add(PriorDrawAir*);
    bool isExistValidDrawAir() const;

    MR::GrowTable< PriorDrawAir*, 8 > mAirs;
    s32 mAirCount;
};

namespace MR {
    bool isExistPriorDrawAir();
};  // namespace MR
