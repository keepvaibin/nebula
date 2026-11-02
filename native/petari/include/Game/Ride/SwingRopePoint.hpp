#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class SwingRopePoint {
public:
    SwingRopePoint(const TVec3f&);

    virtual void updatePos(f32);

    void addAccel(const TVec3f&);
    bool restrict(const TVec3f&, f32, const TVec3f*);
    void strain(const TVec3f&, f32);
    void updateAxis(const TVec3f&);
    void updatePosAndAxis(const TVec3f&, f32);
    void stop();
    void setInfo(const TVec3f&, const TVec3f&, const TVec3f&, f32);
    void setPosAndAxis(const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&);
    void setAndUpdatePosAndAxis(const TVec3f&, const TVec3f&, const TVec3f&);

    const TVec3f& getSide() const {
        return mSide;
    }

    const TVec3f& getUp() const {
        return mUp;
    }

    const TVec3f& getFront() const {
        return mFront;
    }

    const TVec3f& getPosition() const {
        return mPosition;
    }

    TVec3f mPosition;
    TVec3f mVelocity;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
};
