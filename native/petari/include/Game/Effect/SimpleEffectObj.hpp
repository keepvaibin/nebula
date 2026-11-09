#pragma once

#include "Game/LiveActor/LiveActor.hpp"

struct SimpleEffectData;

class SimpleEffectObj : public LiveActor {
public:
    SimpleEffectObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void startClipped() override;
    virtual void endClipped() override;

    virtual f32 getClippingRadius() const {
        return 500.0f;
    };

    virtual f32 getFarClipDistance() const {
        return 50.0f;
    };

    // From the object's position.
    virtual Vec3f getClippingCenterOffset() const {
        return Vec3f(0.0f, 0.0f, 0.0f);
    };

    virtual bool isSyncClipping() const {
        return false;
    };

    virtual void setStateWait();
    virtual void setStateMove();

    bool isStateMove() const;
    void exeWait();
    void exeMove();

    const char* mObjName;
    const SimpleEffectData* mData;  // null: the effect only
    Vec3f mClippingCenter;
};

// What an effect object of one name does besides its effect.
struct SimpleEffectData {
    const char* mName;
    const char* mStartSound;  // when it starts
    const char* mLevelSound;  // every step while it plays
    s32 mShake;               // camera shake when it starts: cNoShake, 0 strong, 1 normal, 2 weak
    const char* mRumble;      // pad rumble at mRumbleStep
    s32 mRumbleStep;

    static constexpr s32 cNoShake = -1;
};
