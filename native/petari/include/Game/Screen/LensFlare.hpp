#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class BrightObjBase;
class TriggerChecker;

class LensFlareModel : public LiveActor {
public:
    /// @brief Creates a new `LensFlareModel`.
    /// @param pName The pointer to the null-terminated name of the object.
    /// @param pArcName The pointer to the null-terminated base name of the resource archive.
    LensFlareModel(const char* pName, const char* pArcName);

    virtual void appear();
    virtual void control();
    virtual void appearAnim() {
    }
    virtual void controlAnim() {
    }

    void update(bool, bool);

    void exeKill();
    void exeHide();
    void exeShow();
    void exeFadeIn();
    void exeFadeOut();

    void notifyInArea();

    f32 _8C;
    f32 _90;
    f32 _94;
    TriggerChecker* _98;
    TriggerChecker* _9C;
};

class LensFlareRing : public LensFlareModel {
public:
    /// @brief Creates a new `LensFlareRing`.
    LensFlareRing();

    virtual void appearAnim();
    virtual void controlAnim();

    f32 _A0;
};

class LensFlareGlow : public LensFlareModel {
public:
    /// @brief Creates a new `LensFlareGlow`.
    LensFlareGlow();

    virtual void appearAnim();
    virtual void controlAnim();
};

class LensFlareLine : public LensFlareModel {
public:
    /// @brief Creates a new `LensFlareLine`.
    LensFlareLine();

    virtual void appearAnim();
    virtual void controlAnim();
};

class LensFlareDirector : public NameObj {
public:
    /// @brief Creates a new `LensFlareDirector`.
    LensFlareDirector();

    virtual ~LensFlareDirector();

    /// @brief Intializes the `LensFlareDirector` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `JMapInfo`.
    virtual void init(const JMapInfoIter& rIter);

    virtual void movement();
    void pauseOff();
    // The picture where the drawing has reached, as this frame's readback; the bright objects measure their
    // visibility in it.
    void setDrawSyncToken();
    s32 checkArea();
    bool checkBrightObj(bool);
    void controlFlare(s32, bool);

    LensFlareRing* mRing;
    LensFlareGlow* mGlow;
    LensFlareLine* mLine;
    MR::FixedArray< BrightObjBase*, 16 > mBrightObjArray;
    u32 mBrightObjCount;
    struct NebulaScreenProbe* mProbes = nullptr;
    u32 mProbeCapacity = 0;
    TVec2f _60;
    f32 _68;
    TVec2f _6C;
    TVec2f _74;
    u16 _7C;
};

namespace MR {
    void addBrightObj(BrightObjBase* pBrightObj);
    void setLensFlareDrawSyncToken();
};  // namespace MR
