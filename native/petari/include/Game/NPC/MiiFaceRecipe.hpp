#pragma once

#include "Game/Mii/Mii.hpp"

/// @brief What a Mii head is made from: its Mii, its face textures' expressions and size.
class MiiFaceRecipe {
public:
    /// @param source Where the Mii comes from.
    /// @param index The Mii's index in its source.
    /// @param faceSize The face textures' texels, square.
    /// @param expressions The face textures made (Mii::expressionBit).
    MiiFaceRecipe(Mii::Source source, u32 index, u32 faceSize, u32 expressions)
        : mRef{source, index}, mFaceSize(faceSize), mExpressions(expressions) {}

    bool hasExpression(Mii::Expression expression) const { return (mExpressions & Mii::expressionBit(expression)) != 0; }

    /// @brief The Mii, when its source holds one the game takes.
    bool character(Mii::Character* pOut) const { return Mii::character(mRef, pOut); }

    Mii::Ref mRef;
    u32 mFaceSize;
    u32 mExpressions;
};
