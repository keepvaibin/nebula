#pragma once

#include "Game/RhythmLib/AudMeHandles.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <JSystem/JKernel/JKRDisposer.hpp>

class AudMeObject : public AudMeHandles, public JKRDisposer {
public:
    AudMeObject(TVec3f*, u8, NebulaArena*);

    virtual ~AudMeObject();
    virtual void dispose();

    AudMeHandle* startMe(u32);
    AudMeHandle* getLowestPrioHandle(u32);

    TVec3f* mPos;
    bool mIsAllocated;
};
