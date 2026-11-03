#pragma once

#include "Game/Util/Color.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <JSystem/JParticle/JPAEmitterCallBack.hpp>

enum { FORCE_FOLLOW_ON = 0x1, FORCE_FOLLOW_OFF = 0x2, FORCE_SCALE_ON = 0x10, RESET_FOLLOW_CURRENT = 0x40, BASE_SCALE = 0x100 };

class MultiEmitter;

class MultiEmitterCallBackBase : public JPAEmitterCallBack {
public:
    MultiEmitterCallBackBase() {};

    virtual ~MultiEmitterCallBackBase() {};
    virtual void init(JPABaseEmitter*) {};
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

    MultiEmitterCallBack(const MultiEmitter*, const TVec3f&);
    MultiEmitterCallBack(const MultiEmitter*, const TVec3f*, const TVec3f*, const TVec3f*, const TVec3f&);
    MultiEmitterCallBack(const MultiEmitter*, MtxPtr, const TVec3f&);
    MultiEmitterCallBack(const MultiEmitter*, MtxPtr, const TVec3f*, const TVec3f&);

    void execute(JPABaseEmitter*);
    void setHostSRT(const TVec3f*, const TVec3f*, const TVec3f*);
    void setHostMtx(MtxPtr);
    void setBaseScale(f32);
    void forceFollowOn();
    void forceFollowOff();
    void forceScaleOn();
    void resetFollowCurrent();
    void init(JPABaseEmitter*);
    void setSRTFromHostMtx(JPABaseEmitter*, MtxPtr, const FlagSRT&, bool);
    void setSRTFromHostSRT(JPABaseEmitter*, const FlagSRT&, bool);
    void setScaleFromHostScale(JPABaseEmitter*, const TVec3f&, bool, bool);
    void effectLight(JPABaseEmitter*);
    void followSRT(JPABaseEmitter*, bool);
    void setColor(JPABaseEmitter*);
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
    const TVec3f* mScale;
    const TVec3f* mRotation;
    const TVec3f* mTranslation;
    MtxPtr mMtx;
    TVec3f _18;
    f32 mBaseScale;
    Color8 mPrmColor;
    Color8 mEnvColor;
    u16 mFlags;
};
