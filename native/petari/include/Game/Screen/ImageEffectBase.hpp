#pragma once

#include "Game/NameObj/NameObj.hpp"

class ImageEffectBase : public NameObj {
public:
    /// @brief Creates a new `ImageEffectBase`.
    /// @param pName A pointer to the null-terminated name of the object.
    ImageEffectBase(const char* pName);

    virtual void calcAnim() override;

    virtual void calcAnimSub() {
    }

    virtual void notifyTurnOn() {
    }

    virtual void notifyTurnOff() {
    }

    virtual void notifyForceOn() {
    }

    virtual void notifyForceOff() {
    }

    // On, or still fading out.
    bool isSomething() const {
        return mIsOn || mIsShown;
    }

    f32 getFadeLevel() const {
        return mFadeLevel;
    }

    bool mIsOn;     // turned on: fades in
    bool mIsShown;  // drawn: set while on, cleared when the fade-out reaches 0
    f32 mFadeLevel;
};
