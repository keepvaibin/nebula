#pragma once

#include <nebula_geometry.hpp>

struct SoundParams;

class SoundAudible {
public:
    virtual ~SoundAudible();
    virtual SoundParams* getOuterParams(int) = 0;
    virtual void calc() = 0;

    Vec3f& getPos() {
        return mPos;
    }
    void setPos(const Vec3f& other) {
        mPos.set(other);
    }

    Vec3f mPos;
};
