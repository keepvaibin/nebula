#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class GameTexture;

class StarPointerBlur : public NameObj {
public:
    class BlurPoint {
    public:
        BlurPoint();

        Vec2f mPos;
        Vec2f mDir;
        bool mIsHidden;
    };

    StarPointerBlur(GameTexture* pTexture);

    virtual ~StarPointerBlur();

    virtual void movement();
    virtual void draw() const;

    void show();
    void hide();
    void hideAll();
    void updatePoint(const Vec2f&, bool, Vec2f*);
    u16 findDrawPointRange(u16*, u16*) const;
    void draw0(NebulaColor8) const;
    void draw1() const;
    void draw2(TypedDraw& rDraw) const;
    SiteMaterial loadMaterial() const;

    void setPortAndColor(s32 padChannel, const NebulaColor8* pMainColor, const NebulaColor8* pSubColor) {
        mPadChannel = padChannel;
        mMainColor = pMainColor;
        mSubColor = pSubColor;
    }

    GameTexture* mTexture;
    s32 mPadChannel;
    const NebulaColor8* mMainColor;
    const NebulaColor8* mSubColor;
    BlurPoint* mBlurPoints;
    f32* mBlurThicks;
    f32* mBlurTexCoords;
    s32 mNumHiddenPoints;
};
