#pragma once

#include "Game/Screen/WipeLayoutBase.hpp"

class WipeRing : public WipeLayoutBase {
public:
    WipeRing(bool, const char*);

    virtual void init(const JMapInfoIter&);
    virtual void calcAnim();
    virtual void wipe(s32);
    virtual void forceClose();
    virtual void forceOpen();
    virtual bool isOpen() const;
    virtual bool isClose() const;
    virtual bool isWipeIn() const;
    virtual bool isWipeOut() const;

    void setCenterPos(const Vec3f&);
    void startAnim(const char*);
    bool getMarioCenterPos(Vec3f*);
    f32 calcRadius() const;
    f32 calcMaxRadius() const;
    void updatePlayerPos();
    void exeClose();
    void exeWipeIn();
    void exeOpen();
    void exeWipeOut();

private:
    Vec2f mCenterPos;
    s32 mAnimFrame;
    f32 mWipeOutFrame;
    f32 mWipeInFrame;
    bool mIsCenterOnPlayer;
};
