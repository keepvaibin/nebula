#pragma once

#include <nebula_geometry.hpp>

class SwingRopePoint {
public:
    SwingRopePoint(const Vec3f&);

    void updatePos(f32);
    void addAccel(const Vec3f&);
    bool restrict(const Vec3f&, f32, const Vec3f*);
    void strain(const Vec3f&, f32);
    void updateAxis(const Vec3f&);
    void updatePosAndAxis(const Vec3f&, f32);
    void stop();
    void setInfo(const Vec3f&, const Vec3f&, const Vec3f&, f32);
    void setPosAndAxis(const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&);
    void setAndUpdatePosAndAxis(const Vec3f&, const Vec3f&, const Vec3f&);

    const Vec3f& getSide() const {
        return mSide;
    }

    const Vec3f& getUp() const {
        return mUp;
    }

    const Vec3f& getFront() const {
        return mFront;
    }

    const Vec3f& getPosition() const {
        return mPosition;
    }

    Vec3f mPosition;
    Vec3f mVelocity;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
};
