#pragma once

#include "Game/NameObj/NameObj.hpp"

class ImageEffectArea;
class ImageEffectBase;
class ImageEffectState;

namespace ImageEffectStateImpl {
    class StateBloomNormal;
    class StateBloomSimple;
    class StateDepthOfField;
    class StateNull;
    class StateScreenBlur;
};  // namespace ImageEffectStateImpl

class ImageEffectDirector : public NameObj {
public:
    /// @brief Creates a new `ImageEffectDirector`.
    /// @param pName A pointer to the null-terminated name of the object.
    ImageEffectDirector(const char* pName);

    virtual void movement();

    void setCurrentEffect(ImageEffectBase*);
    void turnOnNormal();
    void turnOnDepthOfField(bool);
    void turnOff();
    void forceOff();
    void setAuto();
    void turnOffDOFInSubjective();
    void turnOnDOFInSubjective();
    void setNormalBloomIntensity(u8);
    void setNormalBloomThreshold(u8);
    void setNormalBloomBlurIntensity1(u8);
    void setNormalBloomBlurIntensity2(u8);
    void setDepthOfFieldIntensity(f32);
    void updateManual();
    void setState(ImageEffectState*);
    void setBloomNormalParams(ImageEffectArea*);
    void setBloomSimpleParams(ImageEffectArea*);
    void setScreenBlurParams(ImageEffectArea*);
    void setDepthOfFieldParams(ImageEffectArea*);
    void initPlayerSyncIntensity(ImageEffectArea*, ImageEffectState*);
    void updatePlayerSyncIntensity(ImageEffectArea*, ImageEffectState*);
    void updateSyncCounter();
    void incPlayerSyncIntensity();
    void decPlayerSyncIntensity();
    void setPlayerSync(bool);
    void updateAuto();

    bool mIsAuto;
    bool mIsPlayerSync;
    bool _E;
    bool _F;
    s32 mPlayerSyncIntensity;
    f32 mDepthOfFieldIntensity;
    ImageEffectStateImpl::StateNull* mStateNull;
    ImageEffectStateImpl::StateBloomNormal* mStateBloomNormal;
    ImageEffectStateImpl::StateBloomSimple* mStateBloomSimple;
    ImageEffectStateImpl::StateScreenBlur* mStateScreenBlur;
    ImageEffectStateImpl::StateDepthOfField* mStateDepthOfField;
    ImageEffectState* mState;
    ImageEffectBase* mCurrentEffect;
};
