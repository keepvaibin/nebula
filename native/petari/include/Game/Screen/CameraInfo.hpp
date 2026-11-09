#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CameraInfo : public LayoutActor {
public:
    /// @brief Creates a new `CameraInfo`.
    /// @param pName The pointer to the null-terminated name of the object.
    CameraInfo(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void draw() const override;

    void activate();
    void deactivate();
    void exeHide();
    void exeAppear();
    void exeNormal();
    void exeFirstPerson();
    void exeEnd();
    void updateCameraNG();
    void hideCrossLineAll();
    bool tryAnimShortCut();
    bool tryShow();
    bool tryHide();
    bool tryChangeCameraStateNerve();

private:
    s32 mStep;
    bool mIsActive;
    bool mCanSkipAppearAnim;  // the appear animation is playing and can be cut to its end
};
