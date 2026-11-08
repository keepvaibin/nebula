#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FileSelectEffect : public LiveActor {
public:
    FileSelectEffect(const char* pName = "選択時エフェクト");

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void calcAndSetBaseMtx();

    void disappear();

    void exeAppear();
    void exeWait();
    void exeDisappear();

    f32 mEffectFrame;
};
