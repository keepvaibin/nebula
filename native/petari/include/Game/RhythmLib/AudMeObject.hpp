#pragma once

#include "Game/RhythmLib/AudMeHandles.hpp"
#include <nebula_geometry.hpp>
#include <JSystem/JKernel/JKRDisposer.hpp>

class AudMeObject : public AudMeHandles, public JKRDisposer {
public:
    AudMeObject(Vec3f*, u8, NebulaArena*);

    virtual ~AudMeObject();
    virtual void dispose();

    AudMeHandle* startMe(u32);
    AudMeHandle* getLowestPrioHandle(u32);

    Vec3f* mPos;
    bool mIsAllocated;
};
