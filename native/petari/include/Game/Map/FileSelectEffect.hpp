#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FileSelectEffect : public LiveActor {
public:
    FileSelectEffect(const char* pName = "選択時エフェクト");

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void calcAndSetBaseMtx() override;

    void disappear();

    void exeAppear();
    void exeWait();
    void exeDisappear();

    f32 mEffectFrame;
};
