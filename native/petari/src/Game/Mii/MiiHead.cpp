#include "Game/Mii/MiiHead.hpp"
#include "Game/Mii/MiiParts.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <ModelFileNumbers.h>
#include <string.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_build_f1, "build.f1", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f2, "build.f2", 0.175f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f3, "build.f3", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f4, "build.f4", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f5, "build.f5", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f6, "build.f6", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f7, "build.f7", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f8, "build.f8", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f9, "build.f9", 11.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_build_f10, "build.f10", 2.0f);

namespace {
    Mii::Parts sParts;

    // The part forms (mii_face.hlsl params.x).
    enum {
        FormColor = 0,     // the constant colour: beard, nose, forehead, hair
        FormCap = 1,       // the cap: (K + K tex) / 2
        FormFace = 2,      // the face texture over the skin colour
        FormMask = 3,      // the face texture of the expression
        FormNoseLine = 4,  // black at the texture's alpha
        FormGlasses = 5    // the glasses colour at the texture's alpha
    };

    const NebulaColor8 cHairColors[8] = {
        {30, 26, 24, 255},   {56, 32, 21, 255},  {85, 38, 23, 255},  {112, 64, 36, 255},
        {114, 114, 120, 255}, {73, 54, 26, 255}, {122, 89, 40, 255}, {193, 159, 100, 255},
    };

    const NebulaColor8 cGlassColors[6] = {
        {16, 16, 16, 255}, {96, 56, 16, 255}, {152, 24, 16, 255}, {32, 48, 96, 255}, {144, 88, 0, 255}, {96, 88, 80, 255},
    };

    const NebulaColor8 cBlack = {0, 0, 0, 255};

    // A shape placed for one Mii: scaled, mirrored across x when `flip`, then moved to `place`.
    SiteMesh< VertexPNT >* placeShape(const Mii::Shape* pShape, f32 scale, const f32* place, bool flip) {
        if (pShape == nullptr) {
            return nullptr;
        }
        SiteMesh< VertexPNT >* pMesh = new SiteMesh< VertexPNT >();
        VertexPNT* pVertices;
        u32* pIndices;
        pMesh->initIndexed(pShape->vertexCount, pShape->indexCount, &pVertices, &pIndices);
        const Mii::PartVertex* pFrom = pShape->vertices();
        const f32 side = flip ? -1.0f : 1.0f;
        for (u32 i = 0; i < pShape->vertexCount; i++) {
            const Mii::PartVertex& v = pFrom[i];
            pVertices[i] = {place[0] + scale * side * v.x, place[1] + scale * v.y, place[2] + scale * v.z,
                            side * v.nx, v.ny, v.nz, v.s, v.t};
        }
        memcpy(pIndices, pShape->indices(), pShape->indexCount * sizeof(u32));
        pMesh->send();
        return pMesh;
    }

    void initTexture(SiteTexture* pTexture, const NebulaTextureInfo* pInfo, bool partFilters) {
        SiteTextures::init(pTexture, reinterpret_cast< const u8* >(pInfo) + pInfo->image_at, pInfo->width, pInfo->height,
                           partFilters ? NEBULA_WRAP_CLAMP : pInfo->wrap_s, partFilters ? NEBULA_WRAP_CLAMP : pInfo->wrap_t);
        if (partFilters) {
            SiteTextures::setFilter(pTexture, pInfo->min_filter, pInfo->mag_filter, pInfo->min_lod, pInfo->max_lod,
                                    pInfo->lod_bias, false, pInfo->anisotropy);
        }
    }
}  // namespace

namespace Mii {
    bool setParts(const void* pFile, u32 size) {
        return sParts.init(pFile, size);
    }

    void clearParts() {
        sParts = Parts();
    }

    const Parts* parts() {
        return sParts.isReady() ? &sParts : nullptr;
    }

    Head::Head() : mExpressions(0), mExpression(ExpressionNormal), mBuilt(false) {
        memset(mParts, 0, sizeof(mParts));
        memset(mFaceTextures, 0, sizeof(mFaceTextures));
        memset(mFaceKeys, 0, sizeof(mFaceKeys));
        NebulaMtxIdentity(mPosMtx);
    }

    Head::~Head() {
        release();
    }

    void Head::release() {
        for (u32 i = 0; i < PartCount; i++) {
            delete mParts[i].mesh;
            mParts[i].mesh = nullptr;
            mParts[i].present = false;
        }
        mBuilt = false;
    }

    bool Head::build(const Character& rMii, u32 expressions, u32 faceSize) {
        const Parts* pParts = parts();

        release();
        if (pParts == nullptr) {
            return false;
        }

        const Shape* pFace = pParts->shape(ShapeFaceLine, rMii.face_type);
        const f32 cZero[3] = {0.0f, 0.0f, 0.0f};
        const f32* pNose = pFace != nullptr ? pFace->nosePlace : cZero;
        const f32* pBeard = pFace != nullptr ? pFace->beardPlace : cZero;
        const f32* pHair = pFace != nullptr ? pFace->hairPlace : cZero;
        const bool flip = rMii.hair_flip != 0;
        const NebulaColor8 skin = skinColor(rMii);

        const f32 noseScale = cNum_build_f1 + cNum_build_f2 * rMii.nose_scale;
        const f32 nosePlace[3] = {pNose[0], pNose[1] - cNum_build_f3 * (static_cast< f32 >(rMii.nose_y) - cNum_build_f4), pNose[2]};
        const f32 glassScale = cNum_build_f5 + cNum_build_f6 * rMii.glass_scale;
        const f32 glassPlace[3] = {pNose[0], cNum_build_f7 + pNose[1] - cNum_build_f8 * (static_cast< f32 >(rMii.glass_y) - cNum_build_f9), cNum_build_f10 + pNose[2]};

        // A flipped hairstyle draws its forehead, hair and cap with the other side culled.
        const u8 front = MODEL_FILE_CULL_BACK;
        const u8 hairCull = flip ? MODEL_FILE_CULL_FRONT : MODEL_FILE_CULL_BACK;

        struct Plan {
            ShapeKind kind;
            u32 file;
            f32 scale;
            const f32* place;
            bool flip;
            u8 form;
            u8 cull;
            NebulaColor8 color;
        };
        const Plan plans[PartCount] = {
            {ShapeBeard, rMii.beard_type, 1.0f, pBeard, false, FormColor, front, cHairColors[rMii.beard_color & 7]},
            {ShapeNose, rMii.nose_type, noseScale, nosePlace, false, FormColor, front, skin},
            {ShapeForehead, rMii.hair_type, 1.0f, pHair, flip, FormColor, hairCull, skin},
            {ShapeHair, rMii.hair_type, 1.0f, pHair, flip, FormColor, hairCull, cHairColors[rMii.hair_color & 7]},
            {ShapeCap, rMii.hair_type, 1.0f, pHair, flip, FormCap, hairCull, favoriteColor(rMii.favorite_color)},
            {ShapeFaceLine, rMii.face_type, 1.0f, cZero, false, FormFace, front, skin},
            {ShapeMask, rMii.face_type, 1.0f, cZero, false, FormMask, front, cBlack},
            {ShapeNoseLine, rMii.nose_type, noseScale, nosePlace, false, FormNoseLine, front, cBlack},
            {ShapeGlasses, 0, glassScale, glassPlace, false, FormGlasses, MODEL_FILE_CULL_NONE,
             cGlassColors[rMii.glass_color < 6 ? rMii.glass_color : 0]},
        };
        for (u32 i = 0; i < PartCount; i++) {
            Part& rPart = mParts[i];
            const Plan& rPlan = plans[i];
            rPart.mesh = placeShape(pParts->shape(rPlan.kind, rPlan.file), rPlan.scale, rPlan.place, rPlan.flip);
            rPart.present = rPart.mesh != nullptr;
            rPart.form = rPlan.form;
            rPart.cull = rPlan.cull;
            rPart.color = rPlan.color;
            rPart.textured = false;
        }

        struct Skin {
            PartName part;
            TextureKind kind;
            u32 file;
        };
        const Skin skins[4] = {
            {PartFace, TextureFace, rMii.face_texture},
            {PartCap, TextureCap, rMii.hair_type},
            {PartNoseLine, TextureNoseLine, rMii.nose_type},
            {PartGlasses, TextureGlasses, rMii.glass_type},
        };
        for (u32 i = 0; i < 4; i++) {
            Part& rPart = mParts[skins[i].part];
            const NebulaTextureInfo* pInfo = pParts->texture(skins[i].kind, skins[i].file);
            if (rPart.present && pInfo != nullptr) {
                initTexture(&rPart.texture, pInfo, false);
                SiteTextures::setFilter(&rPart.texture, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0f, 0.0f, 0.0f, false, 0);
                rPart.textured = true;
            } else if (skins[i].part == PartCap || skins[i].part == PartNoseLine) {
                rPart.present = rPart.present && pInfo != nullptr;
            }
        }

        mExpressions = 0;
        for (u32 e = 0; e < ExpressionCount; e++) {
            if ((expressions & (1u << e)) == 0) {
                continue;
            }
            SiteTextures::init(&mFaceTextures[e], mFaceKeys[e], faceSize, faceSize, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
            makeFaceTexture(mFaceKeys[e], rMii, static_cast< Expression >(e), faceSize);
            if (mExpressions == 0) {
                mExpression = static_cast< Expression >(e);
            }
            mExpressions |= 1u << e;
        }
        mBuilt = true;
        return true;
    }

    void Head::setExpression(Expression expression) {
        if (hasExpression(expression)) {
            mExpression = expression;
        }
    }

    void Head::setMatrix(const Mtx viewMtx) {
        NebulaMtxCopy(viewMtx, mPosMtx);
    }

    void Head::drawPart(const Part& rPart, const Style& rStyle, const SiteTexture* pTexture) const {
        if (!rPart.present) {
            return;
        }
        // The light function's forms as the material shaders take them (channel_light: attenuation 1 specular, 2 none
        // (directional), 3 spot; diffuse 0 under specular attenuation).
        const u32 atten = rStyle.atten == MODEL_FILE_ATTEN_SPECULAR ? 1 : rStyle.atten == MODEL_FILE_ATTEN_SPOT ? 3 : 2;
        const u32 diffuse = rStyle.atten == MODEL_FILE_ATTEN_SPECULAR ? 0 : static_cast< u32 >(rStyle.diffuse);
        u8 cull = rPart.cull;
        if (rStyle.reverseCulling && cull != MODEL_FILE_CULL_NONE) {
            cull = cull == MODEL_FILE_CULL_FRONT ? MODEL_FILE_CULL_BACK : MODEL_FILE_CULL_FRONT;
        }

        SiteMaterial material;
        material.init(NEBULA_SHADER_MII_FACE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
        material.takeState(rStyle.pass);
        material.mState.cull = cull == MODEL_FILE_CULL_FRONT ? NEBULA_CULL_FRONT : cull == MODEL_FILE_CULL_BACK ? NEBULA_CULL_BACK : NEBULA_CULL_NONE;
        material.setColor(0, rPart.color.r, rPart.color.g, rPart.color.b, rPart.color.a);
        material.constants[4] = static_cast< f32 >(rPart.form);
        material.constants[5] = static_cast< f32 >(rStyle.chain);
        material.constants[6] = static_cast< f32 >(rStyle.alphaTest);
        material.setColor(8, rStyle.material.r, rStyle.material.g, rStyle.material.b, rStyle.material.a);
        material.setColor(12, rStyle.ambient.r, rStyle.ambient.g, rStyle.ambient.b, rStyle.ambient.a);
        material.constants[16] = static_cast< f32 >(rStyle.lightMask);
        material.constants[17] = static_cast< f32 >(diffuse);
        material.constants[18] = static_cast< f32 >(atten);
        if (pTexture != nullptr) {
            material.setTexture(0, *pTexture);
        }

        TypedDraw site = TypedDraw::eyeSpace(material, mPosMtx);
        site.setNormalsScaled();
        rPart.mesh->draw(site);
    }

    void Head::drawOpa(const Style& rStyle) const {
        if (!mBuilt) {
            return;
        }
        // A part that is not there (drawPart returns) does not renumber the parts after it.
        const u32 first = Frame::nextPart();
        for (u32 i = PartBeard; i <= PartFace; i++) {
            Frame::setPart(first + i);
            drawPart(mParts[i], rStyle, mParts[i].textured ? &mParts[i].texture : nullptr);
        }
    }

    void Head::drawXlu(const Style& rStyle) const {
        if (!mBuilt) {
            return;
        }
        const u32 first = Frame::nextPart();
        Frame::setPart(first + PartMask);
        drawPart(mParts[PartMask], rStyle, hasExpression(mExpression) ? &mFaceTextures[mExpression] : nullptr);
        Frame::setPart(first + PartNoseLine);
        drawPart(mParts[PartNoseLine], rStyle, mParts[PartNoseLine].textured ? &mParts[PartNoseLine].texture : nullptr);
        Frame::setPart(first + PartGlasses);
        drawPart(mParts[PartGlasses], rStyle, mParts[PartGlasses].textured ? &mParts[PartGlasses].texture : nullptr);
    }
}  // namespace Mii
