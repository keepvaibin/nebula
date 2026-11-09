#pragma once

#include "Game/Camera/Camera.hpp"

// A camera animation's track: mCount entries from value mFirst. Stride 1: a value per frame, linear between frames;
// 3: keys (frame, value, slope); 4: keys (frame, value, slope in, slope out). A track of one entry is its first value.
struct CamAnmTrack {
    u32 mCount;
    u32 mFirst;
    u32 mStride;
};

enum CamAnmTrackId {
    CamAnmTrack_PosX,
    CamAnmTrack_PosY,
    CamAnmTrack_PosZ,
    CamAnmTrack_WatchPosX,
    CamAnmTrack_WatchPosY,
    CamAnmTrack_WatchPosZ,
    CamAnmTrack_Twist,
    CamAnmTrack_Fovy,
    CamAnmTrack_Num
};

// A camera animation as the importer cooks it (crates/nebula-import cook/camera.rs, NCAM); the values follow.
struct CamAnmHeader {
    char mMagic[4];  // "NCAM"
    u32 mNrFrames;
    u32 mNrValues;
    CamAnmTrack mTracks[CamAnmTrack_Num];

    const f32* values() const {
        return reinterpret_cast< const f32* >(this + 1);
    }
};
static_assert(sizeof(CamAnmHeader) == 12 + 12 * CamAnmTrack_Num, "the cook's NCAM header and tracks");

class CameraAnim : public Camera {
public:
    CameraAnim(const char* pName = "アニメーションカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isInterpolationOff() const {
        return true;
    }
    virtual bool isCollisionOff() const {
        return true;
    }
    virtual bool isZeroFrameMoveOff() const {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(u8*, f32);
    bool isAnimEnd() const;
    static u32 getAnimFrame(u8*);
    bool loadBin(u8*);

    f32 getTrack(u32 track, f32 key) const;
    void getPos(Vec3f*, f32) const;
    void getWatchPos(Vec3f*, f32) const;

    u32 mNrFrames;
    f32 mSpeed;
    f32 mCurrentFrame;
    const CamAnmHeader* mClip;
    bool mIsPaused;
};
