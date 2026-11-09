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

// A camera animation as the importer cooks it (crates/nebula-import cook/camera.rs, NCAM); the values follow, then the
// cuts: a count and each cut's step s, ascending (the shot changes between frames s and s + 1).
struct CamAnmHeader {
    char mMagic[4];  // "NCAM"
    u32 mNrFrames;
    u32 mNrValues;
    CamAnmTrack mTracks[CamAnmTrack_Num];

    const f32* values() const {
        return reinterpret_cast< const f32* >(this + 1);
    }

    u32 cutCount() const {
        return *reinterpret_cast< const u32* >(values() + mNrValues);
    }

    const u32* cuts() const {
        return reinterpret_cast< const u32* >(values() + mNrValues) + 1;
    }
};
static_assert(sizeof(CamAnmHeader) == 12 + 12 * CamAnmTrack_Num, "the cook's NCAM header and tracks");

class CameraAnim : public Camera {
public:
    CameraAnim(const char* pName = "アニメーションカメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isInterpolationOff() const override {
        return true;
    }
    virtual bool isCollisionOff() const override {
        return true;
    }
    virtual bool isZeroFrameMoveOff() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(u8*, f32);
    bool isAnimEnd() const;
    static u32 getAnimFrame(u8*);
    bool loadBin(u8*);

    f32 getTrack(u32 track, f32 key) const;
    void getPos(Vec3f*, f32) const;
    void getWatchPos(Vec3f*, f32) const;
    // From the frame shown last to `key`: back, or across one of the clip's cuts.
    bool isCutTo(f32 key) const;

    u32 mNrFrames;
    f32 mSpeed;
    f32 mCurrentFrame;
    const CamAnmHeader* mClip;
    bool mIsPaused;
    // The frame shown last; below 0 when none was since the reset.
    f32 mShownFrame;
};
