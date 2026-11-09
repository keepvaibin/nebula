#pragma once


class GameTexture;
struct SiteMaterial;

#include "Game/Util/Color.hpp"

class BigBubbleDrawer {
public:
    BigBubbleDrawer();

    // The warning colour over the bubble drawn next (the material setUpStage sent).
    static void setWarningColor(const Color8& rColor);
    static const SiteMaterial& material();


    void setUpDraw() const;
    void setUpVertexFormat() const;
    void setUpTexture() const;
    void setUpStage() const;
    void setUpPixelFormat() const;
    void setUpMaterial() const;

    GameTexture* mBubbleTexture;
    GameTexture* mRainbowTexture;
};
