#pragma once


class JUTTexture;

#include "Game/Util/Color.hpp"

class BigBubbleDrawer {
public:
    BigBubbleDrawer();

    // The warning colour over the bubble drawn next (the material setUpStage sent).
    static void setWarningColor(const Color8& rColor);


    void setUpDraw() const;
    void setUpVertexFormat() const;
    void setUpTexture() const;
    void setUpStage() const;
    void setUpPixelFormat() const;
    void setUpMaterial() const;

    JUTTexture* mBubbleTexture;
    JUTTexture* mRainbowTexture;
    f32 _8;
};
