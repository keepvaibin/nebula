#pragma once

#include "Game/NameObj/NameObj.hpp"

class JUTTexture;

class ScreenAlphaCapture : public NameObj {
public:
    /// @brief Creates a new `ScreenAlphaCapture`.
    /// @param pName A pointer to the null-terminated name of the object.
    ScreenAlphaCapture(const char* pName);

    virtual void init(const JMapInfoIter& rIter);

    void createScreenAlpha(s32, f32);
    void captureScreenAlpha(s32);
    JUTTexture* getTexture(s32);

private:
    JUTTexture* mTexture[5];
};

namespace MR {
    void createScreenAlphaSceneObj(s32, f32);
    void captureScreenAlpha(s32);
    JUTTexture* getScreenAlphaTexture(s32);
};  // namespace MR
