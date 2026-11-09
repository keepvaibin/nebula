#pragma once

#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/NameObj/NameObj.hpp"

/// @brief A Mii's icon: a picture the renderer draws when asked (MiiFaceIconHolder at its draw), which a layout shows
/// through its image header.
class MiiFaceIcon : public NameObj {
public:
    /// @param width The picture's width, in pixels.
    /// @param height The picture's height, in pixels.
    /// @param rRecipe The Mii.
    MiiFaceIcon(u16 width, u16 height, const MiiFaceRecipe& rRecipe, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

    /// @brief Draws the icon, once after each change of Mii.
    void drawIcon();

    void requestToMakeIcon();

    /// @brief The library Mii the icon shows.
    void setIndex(u32 index);

    /// @brief The image header a layout's texture takes (ResTIMG); the picture is named by the image after it.
    u8* mImageBuffer;

    /// @brief The icon's background: its Mii's favourite colour, or black.
    bool mIsFavoriteColorBG;

    /// @brief A request to draw is open.
    bool mIsRequestMakeIcon;

    /// @brief The icon was drawn since its Mii last changed (when its Mii is not one the game takes, nothing was).
    bool mIsCreated;

private:
    void createImageBuffer();

    MiiFaceRecipe mRecipe;
    u16 mWidth;
    u16 mHeight;
};
