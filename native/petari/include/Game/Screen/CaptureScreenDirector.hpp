#pragma once

#include "Game/NameObj/NameObj.hpp"

class GameTexture;
struct ResTIMG;

struct TimingInfo {
    const char* mName;
    s32 mTiming;
    bool _8;
    u32 mRequestOnly;
};

class CaptureScreenDirector : public NameObj {
public:
    /// @brief Creates a new `CaptureScreenDirector`.
    CaptureScreenDirector();

    void captureIfAllow(const char* pName);
    void capture();
    void requestCaptureTiming(const char* pName);
    void invalidateCaptureTiming(const char* pName);
    const ResTIMG* getResTIMG() const;
    const TimingInfo* getUsingTiming() const;
    const TimingInfo* getCurrentTiming() const;
    const TimingInfo* findFromName(const char* pName) const;

private:
    const char* mRequestedTiming;
    const char* mTimingType;
    GameTexture* mTexture;
    bool mIsCaptureRequested;
    // The entries mRequestedTiming and mTimingType name, found when those are set.
    const TimingInfo* mRequestedInfo;
    const TimingInfo* mTimingInfo;
};

class CaptureScreenActor : public NameObj {
public:
    /// @brief Creates a new `CaptureScreenActor`.
    /// @param drawType TODO.
    /// @param pCameraName TODO.
    CaptureScreenActor(u32 drawType, const char* pCameraName);

    /// @brief Draws the `CaptureScreenActor` to the screen.
    virtual void draw() const;

private:
    const char* mCameraName;
};
