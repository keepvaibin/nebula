#pragma once

// A Mii's head: its parts' meshes, placed for this Mii once when it is built and handed to the renderer, and its face
// textures, one target pass per expression (MiiFaceTexture.cpp). It draws on the normal path: each part a site mesh with
// the Mii face material (runtime/src/render/materials/mii_face.hlsl).

#include "Game/Mii/Mii.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <NebulaGpuTypes.h>
#include <nebula_math.h>

namespace Mii {
    class Parts;

    // The parts every head is built from: MiiFacePartsHolder sets them at scene start. False when they cannot be read.
    bool setParts(const void* pFile, u32 size);
    void clearParts();
    const Parts* parts();

    // The lit chain after each part's own colour (mii_face.hlsl params.y).
    enum Chain {
        ChainUnlit = 0,   // the part's colour as it is
        ChainLight = 1,   // the colour times the lit channel (icons)
        ChainNpcOpa = 2,  // the game's Mii NPC stages, opaque pass
        ChainNpcXlu = 3   // the game's Mii NPC stages, translucent pass
    };

    // How a head's parts draw: its pass state and lit chain.
    struct Style {
        PassState pass;
        Chain chain;
        u32 lightMask;
        u8 diffuse;    // MODEL_FILE_DIFFUSE_*
        u8 atten;      // MODEL_FILE_ATTEN_*
        NebulaColor8 ambient;
        NebulaColor8 material;
        u32 alphaTest;  // 0 above 0, 1 not 255
        bool reverseCulling;
    };

    class Head {
    public:
        Head();
        ~Head();

        // Builds the head of `rMii` with the face textures of `expressions` (expressionBit), `faceSize` texels square.
        // False when the parts are not set. The first expression built is the one shown.
        bool build(const Character& rMii, u32 expressions, u32 faceSize);
        bool isBuilt() const { return mBuilt; }
        bool hasExpression(Expression expression) const { return (mExpressions & expressionBit(expression)) != 0; }
        void setExpression(Expression expression);

        // The head's place in eye space.
        void setMatrix(const Mtx viewMtx);
        void drawOpa(const Style& rStyle) const;
        void drawXlu(const Style& rStyle) const;

    private:
        // MiiHead.cpp walks PartBeard to PartFace by number.
        enum PartName { PartBeard, PartNose, PartForehead, PartHair, PartCap, PartFace, PartMask, PartNoseLine, PartGlasses, PartCount };

        struct Part {
            SiteMesh< VertexPNT >* mesh;
            bool present;
            u8 form;   // mii_face.hlsl params.x
            u8 cull;   // MODEL_FILE_CULL_*, before the style's reverse
            NebulaColor8 color;
            SiteTexture texture;
            bool textured;
        };

        void release();
        void drawPart(const Part& rPart, const Style& rStyle, const SiteTexture* pTexture) const;

        Part mParts[PartCount];
        SiteTexture mFaceTextures[ExpressionCount];
        // The face textures' target passes are named by these addresses.
        u8 mFaceKeys[ExpressionCount][16];
        Mtx mPosMtx;
        u32 mExpressions;
        Expression mExpression;
        bool mBuilt;
    };

    // Draws `rMii`'s face texture of `expression` into the target pass named by `pKey`, `size` texels square.
    void makeFaceTexture(const void* pKey, const Character& rMii, Expression expression, u32 size);

    // An icon of `rMii` drawn into the target pass named by `pKey`: the head lit from the front over `background`
    // (alpha 0), or over its favourite colour when `pBackground` is null.
    void makeIcon(const void* pKey, const Character& rMii, u32 width, u32 height, const NebulaColor8* pBackground);
}  // namespace Mii
