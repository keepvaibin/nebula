#pragma once

#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>
#include "Game/Particle/EmitterCallBack.hpp"

enum { FORCE_FOLLOW_ON = 0x1, FORCE_FOLLOW_OFF = 0x2, FORCE_SCALE_ON = 0x10, RESET_FOLLOW_CURRENT = 0x40, BASE_SCALE = 0x100 };

class MultiEmitter;

class MultiEmitterCallBackBase : public EmitterCallBack {
public:
    MultiEmitterCallBackBase() {};

    virtual ~MultiEmitterCallBackBase() {};
    virtual void init(EmitterCore*) {};
};

class MultiEmitterCallBack : public MultiEmitterCallBackBase {
public:
    struct FlagSRT {
        bool mScale;
        bool mRotation;
        bool mTranslation;

        inline bool isTransform() const {
            return mScale || mRotation || mTranslation ? true : false;
        }
    };

    MultiEmitterCallBack(const MultiEmitter*, const Vec3f&);
    MultiEmitterCallBack(const MultiEmitter*, const Vec3f*, const Vec3f*, const Vec3f*, const Vec3f&);
    MultiEmitterCallBack(const MultiEmitter*, MtxPtr, const Vec3f&);
    MultiEmitterCallBack(const MultiEmitter*, MtxPtr, const Vec3f*, const Vec3f&);

    void execute(EmitterCore*);
    void setHostSRT(const Vec3f*, const Vec3f*, const Vec3f*);
    void setHostMtx(MtxPtr);
    void setBaseScale(f32);
    void forceFollowOn();
    void forceFollowOff();
    void forceScaleOn();
    void resetFollowCurrent();
    void init(EmitterCore*);
    void setSRTFromHostMtx(EmitterCore*, MtxPtr, const FlagSRT&, bool);
    void setSRTFromHostSRT(EmitterCore*, const FlagSRT&, bool);
    void setScaleFromHostScale(EmitterCore*, const Vec3f&, bool, bool);
    void effectLight(EmitterCore*);
    void followSRT(EmitterCore*, bool);
    void setColor(EmitterCore*);
    void isFollowSRT(FlagSRT*, bool) const;

    inline bool isFlagOn(u16 flag) const {
        u16 mask = mFlags & flag;
        return mask == flag;
    }

    inline bool isFlagOff(u16 flag) const {
        u16 mask = mFlags & flag;
        return mask != flag;
    }

    inline void turnFlagOn(u16 flag) {
        mFlags |= flag;
    }

    inline void turnFlagOff(u16 flag) {
        mFlags &= ~flag;
    }

    const MultiEmitter* mEmitter;
    const Vec3f* mScale;
    const Vec3f* mRotation;
    const Vec3f* mTranslation;
    MtxPtr mMtx;
    Vec3f _18;
    f32 mBaseScale;
    Color8 mPrmColor;
    Color8 mEnvColor;
    u16 mFlags;
};
