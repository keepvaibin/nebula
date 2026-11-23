#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class BrightObjBase;
class GameTexture;
class TriggerChecker;

class LensFlareModel : public LiveActor {
public:
    /// @brief Creates a new `LensFlareModel`.
    /// @param pName The pointer to the null-terminated name of the object.
    /// @param pArcName The pointer to the null-terminated base name of the resource archive.
    LensFlareModel(const char* pName, const char* pArcName);

    virtual void appear() override;
    virtual void control() override;
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

    f32 mFlareStrength;  // from the bright object's distance to the screen centre (how much of it shows: the sight)
    f32 mFadeLevel;  // 0 to 1
    f32 mFadeStep;  // fade level change per frame
    TriggerChecker* mAreaChecker;  // inside the lens flare area
    TriggerChecker* mVisibleChecker;  // inside the area with a bright object showing
};

class LensFlareRing : public LensFlareModel {
public:
    /// @brief Creates a new `LensFlareRing`.
    LensFlareRing();

    virtual void appearAnim() override;
    virtual void controlAnim() override;

    f32 mRingFrame;  // ring animation position, 0 to 1
};

class LensFlareGlow : public LensFlareModel {
public:
    /// @brief Creates a new `LensFlareGlow`.
    LensFlareGlow();

    virtual void appearAnim() override;
    virtual void controlAnim() override;
};

class LensFlareLine : public LensFlareModel {
public:
    /// @brief Creates a new `LensFlareLine`.
    LensFlareLine();

    virtual void appearAnim() override;
    virtual void controlAnim() override;
};

class LensFlareDirector : public NameObj {
public:
    /// @brief Creates a new `LensFlareDirector`.
    LensFlareDirector();

    virtual ~LensFlareDirector() override;

    /// @brief Intializes the `LensFlareDirector` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter) override;

    virtual void movement() override;
    void pauseOff();
    s32 checkArea();
    bool checkBrightObj(bool);
    void controlFlare(s32, bool);

    // How much of the flare shows is decided on the graphics card, in the picture, and never read back: the sight pass
    // after the scene writes the share of the object's samples nothing stands in front of, eased from the last frame's,
    // into a 1 by 1 target (flare_sight.hlsl), and the flare is drawn as much as that share
    // (beginFlareDraw/endFlareDraw, flare_blend.hlsl). Draw state alone: the step reads none of it.
    bool isFlareShown() const;
    void drawSight() const;
    bool beginFlareDraw() const;
    void endFlareDraw() const;

    LensFlareRing* mRing;
    LensFlareGlow* mGlow;
    LensFlareLine* mLine;
    MR::InlineArray< BrightObjBase*, 16 > mBrightObjArray;
    u32 mBrightObjCount;
    s32 mSightObj;  // the bright object the flare is placed from, or -1
    Vec2f mNowCenter;  // its centre on the screen
    GameTexture* mSightTargets[2];  // the sight's share, written in turn: each frame reads the other's
    GameTexture* mDepthKey;  // the frame's depth for the sight pass
    GameTexture* mWithoutKey;  // the frame before the flare
    GameTexture* mWithKey;  // the frame with it
};

namespace MR {
    void addBrightObj(BrightObjBase* pBrightObj);
    // After the scene, while its depth stands: the sight pass of the flare (LensFlareDirector::drawSight).
    void drawLensFlareSight();
    // Around the flare's models: the frame before them is kept, and they are mixed into it by the sight's share.
    bool beginLensFlareDraw();
    void endLensFlareDraw();
};  // namespace MR
