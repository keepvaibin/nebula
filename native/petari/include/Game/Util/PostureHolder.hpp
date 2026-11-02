#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class LiveActor;

class PostureHolder {
public:
    PostureHolder(const LiveActor* pActor);

    void store(const LiveActor* pActor);
    void restore(LiveActor* pActor);

private:
    TVec3f mScale;
    TVec3f mRotation;
    TVec3f mPosition;
};
