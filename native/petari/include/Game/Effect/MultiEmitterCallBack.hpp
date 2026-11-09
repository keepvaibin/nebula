#pragma once

#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>
#include "Game/Particle/EmitterCallBack.hpp"

// mFlags: forced following (each wins over the effect's own setting) and a base scale.
enum { FORCE_FOLLOW_ON = 0x1, FORCE_FOLLOW_OFF = 0x2, FORCE_SCALE_ON = 0x10, BASE_SCALE = 0x100 };

class MultiEmitter;

class MultiEmitterCallBackBase : public EmitterCallBack {
public:
    MultiEmitterCallBackBase() {};

    virtual void init(EmitterCore*) override {};
};

// Places an effect's emitters on its host: a matrix, or translation/rotation/scale vectors.
class MultiEmitterCallBack : public MultiEmitterCallBackBase {
public:
    // Which parts of the host the emitters take this frame.
    struct Follow {
        bool mTranslation = false;
        bool mRotation = false;
        bool mScale = false;

        bool any() const { return mTranslation || mRotation || mScale; }
    };

    MultiEmitterCallBack(const MultiEmitter*, const Vec3f& rOffset);
    MultiEmitterCallBack(const MultiEmitter*, const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale, const Vec3f& rOffset);
    MultiEmitterCallBack(const MultiEmitter*, MtxPtr pMtx, const Vec3f& rOffset);
    MultiEmitterCallBack(const MultiEmitter*, MtxPtr pMtx, const Vec3f* pScale, const Vec3f& rOffset);

    void execute(EmitterCore*) override;
    void init(EmitterCore*) override;
    void setHostSRT(const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale);
    void setHostMtx(MtxPtr);
    void setBaseScale(f32);
    void forceFollowOn();
    void forceFollowOff();
    void forceScaleOn();

    void setColor(EmitterCore*);

    bool isFlagOn(u16 flag) const { return (mFlags & flag) == flag; }
    void turnFlagOn(u16 flag) { mFlags |= flag; }

    const MultiEmitter* mEmitter;
    const Vec3f* mHostTranslation;
    const Vec3f* mHostRotation;
    const Vec3f* mHostScale;
    MtxPtr mHostMtx;
    Vec3f mOffset;  // in the host's rotated (and, when scale is followed, scaled) frame
    f32 mBaseScale;
    Color8 mPrmColor;
    Color8 mEnvColor;
    u16 mFlags;

private:
    // `atCreate`: the emitter is being created (the effect's "affect" bits), else a frame (its "follow" bits).
    Follow follows(bool atCreate) const;
    void follow(EmitterCore*, bool atCreate);
    void followHostMtx(EmitterCore*, const Follow&, bool atCreate);
    void followHostSRT(EmitterCore*, const Follow&, bool atCreate);
    void setScale(EmitterCore*, const Vec3f& rHostScale, bool followScale, bool atCreate);
};
