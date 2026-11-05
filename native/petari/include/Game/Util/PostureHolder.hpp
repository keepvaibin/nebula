#pragma once

#include <nebula_geometry.hpp>

class LiveActor;

class PostureHolder {
public:
    PostureHolder(const LiveActor* pActor);

    void store(const LiveActor* pActor);
    void restore(LiveActor* pActor);

private:
    Vec3f mScale;
    Vec3f mRotation;
    Vec3f mPosition;
};
