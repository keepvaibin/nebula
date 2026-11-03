#pragma once

#include <MiiLib.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

/// @brief The resource creator for the head of a Mii character.
class MiiFaceRecipe {
public:
    /// @brief Creates a new `MiiFaceRecipe`.
    /// @param dataSource The type of database to obtain Mii data from.
    /// @param index The position of the Mii data in the database.
    /// @param resolution The resolution of the icon texture.
    /// @param expressionFlag The combination of facial expressions.
    MiiFaceRecipe(MiiDataSource dataSource, int index, MiiResolution resolution, u32 expressionFlag);

    /// @brief Updates the pointer to the intermediate database.
    /// @param pMiddleDB The pointer to the intermediate database.
    void setDatabase(MiiMiddleDB* pMiddleDB);

    /// @brief Determines if the Mii character is assuming the given facial expression.
    /// @param expression The facial expression to test.
    /// @return `true` if the Mii character is assuming the facial expression, `false` otherwise.
    bool hasExpression(MiiExpression expression) const;

    /// @brief Computes the required size of the head model data buffer, in bytes.
    /// @return The required size of the data buffer, in bytes.
    u32 getModelBufferSize() const;

    /// @brief Creates the head model.
    /// @param pCharModel The pointer to the descriptor of the head model.
    /// @param pBuffer The output pointer for the model data buffer.
    void initFaceModel(MiiCharModel* pCharModel, void* pBuffer) const;

    /// @brief Creates the icon texture.
    /// @param pBuffer The output pointer for the texture data buffer.
    /// @param width The width of the texture, in pixels.
    /// @param height The height of the texture, in pixels.
    /// @param bgColor The color of the background.
    /// @retval MiiErrcode_Success The function was executed successfully.
    /// @retval MiiErrcode_NotAvailable The function is not ready to be executed.
    /// @retval MiiErrcode_Broken The Mii data is corrupted.
    MiiErrcode makeIcon(void* pBuffer, int width, int height, NebulaColor8 bgColor) const;

    /// @brief Creates the icon texture with the background colored using the Mii character's favorite color.
    /// @param pBuffer The output pointer for the texture data buffer.
    /// @param width The width of the texture, in pixels.
    /// @param height The height of the texture, in pixels.
    /// @retval MiiErrcode_Success The function was executed successfully.
    /// @retval MiiErrcode_NotAvailable The function is not ready to be executed.
    /// @retval MiiErrcode_Broken The Mii data is corrupted.
    MiiErrcode makeIconWithFavoriteColor(void* pBuffer, int width, int height) const;

    /// @brief The type of database to obtain Mii data from.
    MiiDataSource mDataSource;

    /// @brief The pointer to the intermediate database.
    MiiMiddleDB* mMiddleDB;

    /// @brief The position of the Mii data in the database.
    u16 mIndex;

    /// @brief The resolution of the icon texture.
    MiiResolution mResolution;

    /// @brief The combination of facial expressions.
    u32 mExpressionFlag;
};
