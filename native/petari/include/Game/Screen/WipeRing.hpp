#pragma once

#include "Game/Screen/WipeLayoutBase.hpp"

class WipeRing : public WipeLayoutBase {
public:
    WipeRing(bool, const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void wipe(s32) override;
    virtual void forceClose() override;
    virtual void forceOpen() override;
    virtual bool isOpen() const override;
    virtual bool isClose() const override;
    virtual bool isWipeIn() const override;
    virtual bool isWipeOut() const override;

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
