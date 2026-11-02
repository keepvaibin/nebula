#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class PartsModel;

class AssemblyBlock : public LiveActor {
public:
    /// @brief Creates a new `AssemblyBlock`.
    /// @param A pointer to the null-terminated name of the object.
    AssemblyBlock(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeAssemble();
    void exeAssembleWait();
    void exeReturn();
    void exeTimer();
    bool tryStartAssemble();
    bool tryStartReturn();

    TPos3f _8C;
    TPos3f _BC;
    TPos3f _EC;
    s32 mObjArg7;
    f32 mPlayerSearchDistance;
    TVec3f _124;
    f32 mFloatRotateSpeed;
    s32 mTimer;
    PartsModel* mBloomModel;
    bool _13C;
};
