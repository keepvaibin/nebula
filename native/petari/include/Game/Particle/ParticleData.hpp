#pragma once

#include <NebulaGpu.h>
#include <nebula_cooked_texture.h>
#include <nebula_geometry.hpp>

// Cooked particles (crates/nebula-import cook/particles.rs): flat records in host order, read in place. Offsets count
// from the file's start; 0 is none. Every packed field is split at cook, blend, compare and factor codes are the host's
// (nebula_game_api.h), colour tables are built at cook, and rates the loader used to derive are stored.

enum { cParticleFileMagic = 0x4354504E /* NPTC */, cParticleFileVersion = 1 };

struct ParticleFileHeader {
    u32 mMagic;
    u32 mVersion;
    u32 mResourceCount;
    u32 mTextureCount;
    u32 mResources;  // ParticleResourceData[mResourceCount]
    u32 mTextures;   // ParticleTextureEntry[mTextureCount]
    u32 mReserved[2];
};
static_assert(sizeof(ParticleFileHeader) == 32);

struct ParticleResourceData {
    u32 mUserIndex;
    u32 mFieldCount;
    u32 mKeyCount;
    u32 mTextureIndexCount;
    u32 mDynamics;        // EmitterDynamicsData
    u32 mShape;           // ParticleShapeData
    u32 mExtra;           // ExtraShapeData or 0
    u32 mChild;           // ChildShapeData or 0
    u32 mExTex;           // ExTexShapeData or 0
    u32 mFields;          // ParticleFieldData[mFieldCount], in the source's order
    u32 mKeys;            // u32[mKeyCount]: offsets of ParticleKeyData, in the source's order
    u32 mTextureIndices;  // u32[mTextureIndexCount]: the file's textures
};
static_assert(sizeof(ParticleResourceData) == 48);

// Emitter flags (EmitterDynamicsData::mFlags).
enum {
    DynamicsFlag_FixedDensity = 0x01,
    DynamicsFlag_FixedInterval = 0x02,
    DynamicsFlag_InheritScale = 0x04,
    DynamicsFlag_FollowEmtr = 0x08,
    DynamicsFlag_FollowEmtrChld = 0x10,
};

struct EmitterDynamicsData {
    u32 mFlags;       // DynamicsFlag_*
    u32 mVolumeType;  // 0 cube, 1 sphere, 2 cylinder, 3 torus, 4 point, 5 circle, 6 line
    Vec3f mEmitterScl;
    Vec3f mEmitterTrs;
    Vec3f mEmitterDir;
    f32 mInitialVelOmni;
    f32 mInitialVelAxis;
    f32 mInitialVelRndm;
    f32 mInitialVelDir;
    f32 mSpread;
    f32 mInitialVelRatio;
    f32 mRate;
    f32 mRateRndm;
    f32 mLifeTimeRndm;
    f32 mVolumeSweep;
    f32 mVolumeMinRad;
    f32 mAirResist;
    f32 mMoment;
    Vec3< s16 > mEmitterRot;  // degrees
    s16 mMaxFrame;
    s16 mStartFrame;
    s16 mLifeTime;
    u16 mVolumeSize;
    u16 mDivNumber;
    u8 mRateStep;
    u8 mReserved[3];
};
static_assert(sizeof(EmitterDynamicsData) == 0x74);

struct ParticleShapeData {
    f32 mBaseSizeX;
    f32 mBaseSizeY;
    // The texture-coordinate animation: translation s, t, scale s, t, rotation at frame 0, then their steps per frame
    // (zero without one).
    f32 mTexCrdAnm[10];
    u8 mType;  // 0 point, 1 line, 2 billboard, 3-4 direction, 5-6 stripe, 7-8 rotation, 9 direction billboard, 10 y
    u8 mDirType;
    u8 mRotType;
    u8 mPlaneType;
    u8 mTilingS;
    u8 mTilingT;
    u8 mColorMode;  // runtime/src/render/materials/particle.hlsl
    u8 mAlphaMode;
    u8 mBlendOp;    // NEBULA_BLEND_*
    u8 mBlendSrc;   // NEBULA_FACTOR_*
    u8 mBlendDst;
    u8 mZEnable;
    u8 mZCompare;   // NEBULA_COMPARE_*
    u8 mZUpdate;
    u8 mAlphaCompare0;
    u8 mAlphaCompare1;
    u8 mAlphaOp;  // 0 and, 1 or, 2 exclusive or, 3 equal
    u8 mAlphaRef0;
    u8 mAlphaRef1;
    u8 mGlobalColorAnm;
    u8 mGlobalTexAnm;
    u8 mPrjTex;
    u8 mDrawFwdAhead;
    u8 mDrawPrntAhead;
    u8 mTexCrdAnmOn;
    u8 mNoDrawParent;
    u8 mNoDrawChild;
    u8 mPrmAnm;
    u8 mEnvAnm;
    u8 mColorAnmType;
    u8 mTexAnm;
    u8 mTexAnmType;
    u8 mTexIdx;
    u8 mTexAnmKeyNum;
    u8 mColorLoopMask;
    u8 mTexLoopMask;
    u8 mLoopValue;
    u8 mReserved;
    s16 mColorAnmFrameMax;
    NebulaColor8 mPrm;
    NebulaColor8 mEnv;
    u32 mPrmTable;    // NebulaColor8[mColorAnmFrameMax + 1], or 0
    u32 mEnvTable;    // the same
    u32 mTexIdxTable;  // u8[mTexAnmKeyNum], or 0
};
static_assert(sizeof(ParticleShapeData) == 0x6C);

struct ExtraShapeData {
    u8 mScaleAnm;
    u8 mScaleXYDiff;
    u8 mScaleAnmTypeX;
    u8 mScaleAnmTypeY;
    u8 mScaleCenterX;
    u8 mScaleCenterY;
    u8 mAlphaAnm;
    u8 mAlphaFlick;
    u8 mRotateAnm;
    u8 mReserved;
    s16 mScaleAnmCycleX;
    s16 mScaleAnmCycleY;
    u8 mReserved2[2];
    f32 mScaleInTiming;
    f32 mScaleOutTiming;
    f32 mScaleInValueX;
    f32 mScaleOutValueX;
    f32 mScaleInValueY;
    f32 mScaleOutValueY;
    f32 mScaleOutRandom;
    f32 mAlphaInTiming;
    f32 mAlphaOutTiming;
    f32 mAlphaInValue;
    f32 mAlphaBaseValue;
    f32 mAlphaOutValue;
    f32 mAlphaWaveFrequency;
    f32 mAlphaWaveRandom;
    f32 mAlphaWaveAmplitude;
    f32 mRotateAngle;
    f32 mRotateAngleRandom;
    f32 mRotateSpeed;
    f32 mRotateSpeedRandom;
    f32 mRotateDirection;
    // Derived at cook.
    f32 mAlphaIncRate;
    f32 mAlphaDecRate;
    f32 mScaleIncRateX;
    f32 mScaleIncRateY;
    f32 mScaleDecRateX;
    f32 mScaleDecRateY;
};
static_assert(sizeof(ExtraShapeData) == 0x78);

struct ChildShapeData {
    u8 mType;
    u8 mDirType;
    u8 mRotType;
    u8 mPlaneType;
    u8 mScaleInherited;
    u8 mAlphaInherited;
    u8 mColorInherited;
    u8 mFieldAffected;
    u8 mScaleOutOn;
    u8 mAlphaOutOn;
    u8 mRotateOn;
    u8 mReserved;
    f32 mPosRndm;
    f32 mBaseVel;
    f32 mBaseVelRndm;
    f32 mVelInfRate;
    f32 mGravity;
    f32 mScaleX;
    f32 mScaleY;
    f32 mInheritScale;
    f32 mInheritAlpha;
    f32 mInheritRGB;
    NebulaColor8 mPrmClr;
    NebulaColor8 mEnvClr;
    f32 mTiming;
    s16 mLife;
    s16 mRate;
    u8 mStep;
    u8 mTexIdx;
    s16 mRotSpeed;
};
static_assert(sizeof(ChildShapeData) == 0x48);

struct ExTexShapeData {
    f32 mIndTexMtx[2][3];
    s8 mExpScale;
    u8 mIndTexIdx;
    u8 mSecTexIdx;
    u8 mUseIndirect;
    u8 mUseSecTex;
    u8 mReserved[3];
};
static_assert(sizeof(ExTexShapeData) == 0x20);

struct ParticleFieldData {
    u32 mType;     // ParticleField::Type
    u32 mAddType;  // 0 the base velocity, 1 the own one, 2 the field's
    u32 mStatus;   // ParticleField::checkStatus bits
    Vec3f mPos;
    Vec3f mDir;
    f32 mMag;
    f32 mMagRndm;
    f32 mVal1;
    f32 mFadeInTime;
    f32 mFadeOutTime;
    f32 mEnTime;
    f32 mDisTime;
    u32 mCycle;
    // Derived at cook.
    f32 mFadeInRate;
    f32 mFadeOutRate;
};
static_assert(sizeof(ParticleFieldData) == 0x4C);

// An emitter value over its frames: mKeyCount keys of (frame, value, in slope, out slope).
struct ParticleKeyData {
    u32 mId;  // which emitter value (ParticleResource::calcKey)
    u32 mKeyCount;
    u32 mLoop;
    u32 mReserved;
    f32 mKeys[1][4];
};

struct ParticleTextureEntry {
    u32 mName;  // NUL-terminated
    u32 mInfo;  // NebulaTextureInfo; its image_at counts from the info
};
static_assert(sizeof(ParticleTextureEntry) == 8);

// The cooked file's records by offset.
template < typename T >
inline const T* particleRecord(const void* pFile, u32 offset) {
    return offset != 0 ? reinterpret_cast< const T* >(static_cast< const u8* >(pFile) + offset) : nullptr;
}
