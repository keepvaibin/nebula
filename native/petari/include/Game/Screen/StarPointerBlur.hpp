#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Color.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class JUTTexture;

class StarPointerBlur : public NameObj {
public:
    class BlurPoint {
    public:
        BlurPoint();

        TVec2f mPos;
        TVec2f mDir;
        bool mIsHidden;
    };

    StarPointerBlur(JUTTexture* pTexture);

    virtual ~StarPointerBlur();

    virtual void movement();
    virtual void draw() const;

    void show();
    void hide();
    void hideAll();
    void updatePoint(const TVec2f&, bool, TVec2f*);
    u16 findDrawPointRange(u16*, u16*) const;
    void draw0(NebulaColor8) const;
    void draw1() const;
    void draw2() const;
    void initDraw() const;

    void setPortAndColor(s32 padChannel, const NebulaColor8* pMainColor, const NebulaColor8* pSubColor) {
        mPadChannel = padChannel;
        mMainColor = pMainColor;
        mSubColor = pSubColor;
    }

    JUTTexture* mTexture;
    s32 mPadChannel;
    const NebulaColor8* mMainColor;
    const NebulaColor8* mSubColor;
    BlurPoint* mBlurPoints;
    f32* mBlurThicks;
    f32* mBlurTexCoords;
    s32 mNumHiddenPoints;
    bool _2C;
    bool _2D;
    f32 _30;
    f32 _34;
    f32 _38;
    f32 _3C;
};
