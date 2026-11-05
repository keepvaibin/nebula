#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

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

    Pos3f _8C;
    Pos3f _BC;
    Pos3f _EC;
    s32 mObjArg7;
    f32 mPlayerSearchDistance;
    Vec3f _124;
    f32 mFloatRotateSpeed;
    s32 mTimer;
    PartsModel* mBloomModel;
    bool _13C;
};
