#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <MiiTypes.h>

class MiiFaceRecipe;

/// @brief The resource creator for the icon of a Mii character.
class MiiFaceIcon : public NameObj {
public:
    /// @brief Creates a new `MiiFaceIcon`.
    /// @param width The width of the texture, in pixels.
    /// @param height The height of the texture, in pixels.
    /// @param rRecipe The reference to the resource creator for the head of a Mii character.
    /// @param pName The pointer to the null-terminated name of the object.
    MiiFaceIcon(u16 width, u16 height, const MiiFaceRecipe& rRecipe, const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    /// @brief Creates the icon texture.
    void drawIcon();

    /// @brief Opens a request to create the icon texture.
    void requestToMakeIcon();

    /// @brief Updates the position of the Mii data in the database.
    /// @param index The position of the Mii data in the database.
    void setIndex(u16 index);

    /// @brief Initializes the texture data buffer.
    void createImageBuffer();

    /// @brief The position of the Mii data in the database.
    u16 mIndex;

    /// @brief Determines if the background of the icon texture should be colored with the Mii character's favorite color.
    bool mIsFavoriteColorBG;

    /// @brief The width of the texture, in pixels.
    u16 mWidth;

    /// @brief The height of the texture, in pixels.
    u16 mHeight;

    /// @brief The pointer to the resource creator for the head of a Mii character.
    MiiFaceRecipe* mRecipe;

    /// @brief The pointer to the texture data buffer.
    u8* mImageBuffer;

    /// @brief Determines if the icon texture has been created.
    bool mIsCreated;

    /// @brief The error status for the creation of the icon texture.
    MiiErrcode mMakeIconResult;

    /// @brief Determines if a request is open to create the icon texture.
    bool mIsRequestMakeIcon;
};
