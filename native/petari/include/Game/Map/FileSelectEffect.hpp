#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FileSelectEffect : public LiveActor {
public:
    /// @brief Creates a new `FileSelectEffect`.
    /// @param pName A pointer to the null-terminated name of the object.
    FileSelectEffect(const char* pName = "\221\111\221\360\216\236\203\107\203\164\203\106\203\116\203\147" /* 選択時エフェクト */);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void calcAndSetBaseMtx();

    void disappear();

    void exeAppear();
    void exeWait();
    void exeDisappear();

    /* 0x8C */ f32 mEffectFrame;
};
