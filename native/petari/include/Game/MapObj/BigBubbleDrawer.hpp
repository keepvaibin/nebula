#pragma once


class JUTTexture;

class BigBubbleDrawer {
public:
    BigBubbleDrawer();

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
