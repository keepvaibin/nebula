// A Mii's face texture (eyes, eyebrows, mouth, mustache, mole: textured quads into a target pass, through
// runtime/src/render/materials/mii_texture.hlsl) and its icon (the head drawn into a target pass).
#include "Game/Mii/MiiHead.hpp"
#include "Game/Mii/MiiParts.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <ModelFileNumbers.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_unit_f1, "unit.f1", 64.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_dimension_f1, "dimension.f1", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_angle_f1, "angle.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_angle_f2, "angle.f2", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f1, "layout.f1", 18.451525f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f2, "layout.f2", 16.549807f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f3, "layout.f3", 29.25885f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f4, "layout.f4", 31.763554f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f5, "layout.f5", 17.766165f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f6, "layout.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f7, "layout.f7", 17.95986f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f8, "layout.f8", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f9, "layout.f9", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f10, "layout.f10", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f11, "layout.f11", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f12, "layout.f12", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f13, "layout.f13", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f14, "layout.f14", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f15, "layout.f15", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_layout_f16, "layout.f16", 32.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f1, "drawQuad.f1", 3.141592653589793f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f2, "drawQuad.f2", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f3, "drawQuad.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f4, "drawQuad.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f5, "drawQuad.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f6, "drawQuad.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawQuad_f7, "drawQuad.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f1, "makeIcon.f1", 43.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f2, "makeIcon.f2", 43.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f3, "makeIcon.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f4, "makeIcon.f4", 57.29578f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f5, "makeIcon.f5", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f6, "makeIcon.f6", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f7, "makeIcon.f7", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f8, "makeIcon.f8", 34.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f9, "makeIcon.f9", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f10, "makeIcon.f10", 34.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f11, "makeIcon.f11", 1600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f12, "makeIcon.f12", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeIcon_f13, "makeIcon.f13", 6000.0f);

namespace {
    // The layout's units: a 64-unit face, its parts' sizes and offsets in those units.
    NEBULA_TUNABLE(f32, cScaleX, 0.88961464f);
    const f32 cScaleY = 0.9276675f;
    const f32 cStepY = 1.16f * cScaleY;

    f32 unit(f32 x) {
        return x / cNum_unit_f1;
    }

    f32 dimension(u32 scale) {
        return 1.0f + cNum_dimension_f1 * static_cast< f32 >(scale);
    }

    f32 angle(u32 rotate) {
        return (cNum_angle_f1 / cNum_angle_f2) * static_cast< f32 >(rotate % 32);
    }

    // Each eye and eyebrow type's own tilt, added to the Mii's.
    const u8 cEyeTilt[50] = {29, 28, 28, 28, 29, 28, 28, 28, 29, 28, 28, 28, 28, 29, 29, 28, 28, 28, 29, 29, 28, 29, 28, 29, 29,
                             28, 29, 28, 28, 29, 28, 28, 28, 29, 29, 29, 28, 28, 29, 29, 29, 28, 28, 29, 29, 29, 29, 29, 28, 28};
    NEBULA_TUNABLE_LIST(u8, cEyebrowTilt, 24, {26, 26, 27, 25, 26, 25, 26, 25, 28, 25, 26, 24, 27, 27, 26, 26, 25, 25, 26, 26, 27, 26, 25, 27});

    const NebulaColor8 cMoleColor = {18, 15, 15, 255};
    const NebulaColor8 cEyeColors[6] = {{0, 0, 0, 255},     {124, 128, 128, 255}, {112, 80, 64, 255},
                                        {112, 110, 64, 255}, {88, 104, 184, 255},  {72, 128, 104, 255}};
    const NebulaColor8 cHairColors[8] = {{30, 26, 24, 255},   {56, 32, 21, 255}, {85, 38, 23, 255},  {112, 64, 36, 255},
                                         {114, 114, 120, 255}, {73, 54, 26, 255}, {122, 89, 40, 255}, {193, 159, 100, 255}};
    const NebulaColor8 cLipColors[3] = {{190, 78, 38, 255}, {216, 48, 40, 255}, {207, 68, 71, 255}};
    const NebulaColor8 cLipEdges[3] = {{113, 42, 4, 255}, {120, 21, 16, 255}, {126, 37, 40, 255}};

    // The texture shader's forms: a solid colour, a mouth (lips and edge), an eye (white and iris).
    enum { TextureFormSolid = 0, TextureFormMouth = 1, TextureFormEye = 2 };

    // Where a quad hangs from its point: its middle, its right edge, its left edge.
    enum Origin { OriginCenter, OriginRight, OriginLeft };

    struct Quad {
        f32 x, y, width, height, angle;
        Origin origin;
        const NebulaTextureInfo* pTexture;
    };

    struct FaceLayout {
        Quad mustacheR, mustacheL, mouth, eyebrowR, eyebrowL, eyeR, eyeL, mole;
    };

    u32 clampedTilt(s32 value, s32 most) {
        return static_cast< u32 >(value < 0 ? 0 : value > most ? most : value);
    }

    // The Mii as the expression shows it: other eyes or mouth, its tilts moved.
    Mii::Character expressed(const Mii::Character& rMii, Mii::Expression expression) {
        Mii::Character m = rMii;
        const auto eyes = [&m, &rMii](u8 type) {
            m.eye_type = type;
            m.eye_rotate = static_cast< u8 >(clampedTilt(cEyeTilt[rMii.eye_type] - cEyeTilt[type] + m.eye_rotate, 7));
        };
        switch (expression) {
        case Mii::ExpressionSmile:
            eyes(48);
            break;
        case Mii::ExpressionAnger:
            m.eyebrow_rotate = static_cast< u8 >(clampedTilt(m.eyebrow_rotate + 2, 11));
            m.eye_rotate = static_cast< u8 >(clampedTilt(m.eye_rotate + 2, 7));
            m.mouth_type = 10;
            break;
        case Mii::ExpressionSorrow:
            m.eyebrow_rotate = static_cast< u8 >(clampedTilt(m.eyebrow_rotate - 2, 11));
            m.eye_rotate = static_cast< u8 >(clampedTilt(m.eye_rotate - 2, 7));
            m.mouth_type = 12;
            break;
        case Mii::ExpressionSurprise:
            m.eyebrow_y = static_cast< u8 >(m.eyebrow_y - 2);
            eyes(49);
            break;
        case Mii::ExpressionBlink:
            eyes(26);
            break;
        case Mii::ExpressionOpenMouth:
            m.mouth_type = 24;
            break;
        default:
            break;
        }
        return m;
    }

    FaceLayout layout(const Mii::Character& m, const Mii::Parts& rParts, u32 size) {
        const f32 eyeX = cScaleX * m.eye_x;
        const f32 eyeY = cNum_layout_f1 + cStepY * m.eye_y;
        const f32 eyeW = unit(342) * dimension(m.eye_scale);
        const f32 eyeH = unit(288) * dimension(m.eye_scale);
        const f32 eyeA = angle(m.eye_rotate + cEyeTilt[m.eye_type]);

        const f32 eyebrowX = cScaleX * m.eyebrow_x;
        const f32 eyebrowY = cNum_layout_f2 + cStepY * m.eyebrow_y;
        const f32 eyebrowW = unit(324) * dimension(m.eyebrow_scale);
        const f32 eyebrowH = unit(288) * dimension(m.eyebrow_scale);
        const f32 eyebrowA = angle(m.eyebrow_rotate + cEyebrowTilt[m.eyebrow_type]);

        const f32 mouthY = cNum_layout_f3 + cStepY * m.mouth_y;
        const f32 mouthW = unit(396) * dimension(m.mouth_scale);
        const f32 mouthH = unit(288) * dimension(m.mouth_scale);

        const f32 mustacheY = cNum_layout_f4 + cStepY * m.beard_y;
        const f32 mustacheW = unit(288) * dimension(m.beard_scale);
        const f32 mustacheH = unit(576) * dimension(m.beard_scale);

        const f32 moleX = cNum_layout_f5 + cNum_layout_f6 * cScaleX * m.mole_x;
        const f32 moleY = cNum_layout_f7 + cStepY * m.mole_y;
        const f32 moleSize = dimension(m.mole_scale);

        const f32 r = unit(static_cast< f32 >(size));
        const NebulaTextureInfo* pEye = rParts.texture(Mii::TextureEye, m.eye_type);
        const NebulaTextureInfo* pEyebrow = rParts.texture(Mii::TextureEyebrow, m.eyebrow_type);
        const NebulaTextureInfo* pMouth = rParts.texture(Mii::TextureMouth, m.mouth_type);
        const NebulaTextureInfo* pMustache = rParts.texture(Mii::TextureMustache, m.mustache);
        const NebulaTextureInfo* pMole = rParts.texture(Mii::TextureMole, m.mole_type);

        FaceLayout f;
        f.eyeR = {r * (cNum_layout_f8 - eyeX), r * eyeY, r * eyeW, r * eyeH, eyeA, OriginRight, pEye};
        f.eyeL = {r * (cNum_layout_f9 + eyeX), r * eyeY, r * eyeW, r * eyeH, cNum_layout_f10 - eyeA, OriginLeft, pEye};
        f.eyebrowR = {r * (cNum_layout_f11 - eyebrowX), r * eyebrowY, r * eyebrowW, r * eyebrowH, eyebrowA, OriginRight, pEyebrow};
        f.eyebrowL = {r * (cNum_layout_f12 + eyebrowX), r * eyebrowY, r * eyebrowW, r * eyebrowH, cNum_layout_f13 - eyebrowA, OriginLeft, pEyebrow};
        f.mouth = {r * cNum_layout_f14, r * mouthY, r * mouthW, r * mouthH, 0.0f, OriginCenter, pMouth};
        f.mustacheR = {r * cNum_layout_f15, r * mustacheY, r * mustacheW, r * mustacheH, 0.0f, OriginRight, pMustache};
        f.mustacheL = {r * cNum_layout_f16, r * mustacheY, r * mustacheW, r * mustacheH, 0.0f, OriginLeft, pMustache};
        f.mole = {r * moleX, r * moleY, r * moleSize, r * moleSize, 0.0f, OriginCenter, pMole};
        return f;
    }

    struct Ink {
        u32 form;
        NebulaColor8 c0, c1;
    };

    void drawQuad(const Quad& rQuad, const Ink& rInk, const PassState& rPass) {
        if (rQuad.pTexture == nullptr) {
            return;
        }
        SiteTexture texture;
        SiteTextures::init(&texture, reinterpret_cast< const u8* >(rQuad.pTexture) + rQuad.pTexture->image_at, rQuad.pTexture->width,
                           rQuad.pTexture->height, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
        SiteTextures::setFilter(&texture, rQuad.pTexture->min_filter, rQuad.pTexture->mag_filter, rQuad.pTexture->min_lod,
                                rQuad.pTexture->max_lod, rQuad.pTexture->lod_bias, false, rQuad.pTexture->anisotropy);

        SiteMaterial material;
        material.init(NEBULA_SHADER_MII_TEXTURE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
        material.takeState(rPass);
        material.setColor(0, rInk.c0.r, rInk.c0.g, rInk.c0.b, rInk.c0.a);
        material.setColor(4, rInk.c1.r, rInk.c1.g, rInk.c1.b, rInk.c1.a);
        material.constants[8] = static_cast< f32 >(rInk.form);
        material.setTexture(0, texture);

        Mtx rot;
        Mtx pos;
        NebulaMtxIdentity(pos);
        NebulaMtxScaleApply(pos, pos, rQuad.width, rQuad.height, 1.0f);
        NebulaMtxRotRad(rot, 'z', (cNum_drawQuad_f1 / cNum_drawQuad_f2) * rQuad.angle);
        NebulaMtxConcat(rot, pos, pos);
        NebulaMtxScaleApply(pos, pos, cScaleX, cScaleY, 1.0f);
        NebulaMtxTransApply(pos, pos, rQuad.x, rQuad.y, 0.0f);

        // The quad's left edge and its texture's side: a left part is the right one mirrored.
        const f32 left = rQuad.origin == OriginCenter ? -cNum_drawQuad_f3 : rQuad.origin == OriginRight ? -1.0f : 0.0f;
        const f32 s0 = rQuad.origin == OriginLeft ? 0.0f : 1.0f;
        const f32 s1 = 1.0f - s0;

        TypedDraw site = TypedDraw::eyeSpace(material, pos);
        VertexPCT* pOut = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, 4);
        const Rgba8 black(0x000000FFu);
        *pOut++ = {1.0f + left, -cNum_drawQuad_f4, 0.0f, black, s0, 0.0f};
        *pOut++ = {1.0f + left, cNum_drawQuad_f5, 0.0f, black, s0, 1.0f};
        *pOut++ = {left, cNum_drawQuad_f6, 0.0f, black, s1, 1.0f};
        *pOut++ = {left, -cNum_drawQuad_f7, 0.0f, black, s1, 0.0f};
    }

    void drawFace(const Mii::Character& m, const FaceLayout& rFace, const PassState& rPass) {
        const NebulaColor8 mustache = cHairColors[m.beard_color & 7];
        const NebulaColor8 eyebrow = cHairColors[m.eyebrow_color & 7];
        const u32 lips = m.mouth_color < 3 ? m.mouth_color : 0;
        const NebulaColor8 eyeWhite = m.eye_type == 9 ? NebulaColor8{255, 130, 0, 255} : m.eye_type == 20 ? NebulaColor8{0, 255, 255, 255} : NebulaColor8{0, 0, 0, 255};
        const Ink mustacheInk = {TextureFormSolid, mustache, mustache};
        const Ink mouthInk = {TextureFormMouth, cLipColors[lips], cLipEdges[lips]};
        const Ink eyebrowInk = {TextureFormSolid, eyebrow, eyebrow};
        const Ink eyeInk = {TextureFormEye, eyeWhite, cEyeColors[m.eye_color < 6 ? m.eye_color : 0]};
        const Ink moleInk = {TextureFormSolid, cMoleColor, cMoleColor};

        drawQuad(rFace.mustacheR, mustacheInk, rPass);
        drawQuad(rFace.mustacheL, mustacheInk, rPass);
        drawQuad(rFace.mouth, mouthInk, rPass);
        drawQuad(rFace.eyebrowR, eyebrowInk, rPass);
        drawQuad(rFace.eyebrowL, eyebrowInk, rPass);
        drawQuad(rFace.eyeR, eyeInk, rPass);
        drawQuad(rFace.eyeL, eyeInk, rPass);
        drawQuad(rFace.mole, moleInk, rPass);
    }
}  // namespace

namespace Mii {
    void makeFaceTexture(const void* pKey, const Character& rMii, Expression expression, u32 size) {
        const Parts* pParts = parts();
        if (pParts == nullptr) {
            return;
        }
        const Character m = expressed(rMii, expression);
        const FaceLayout face = layout(m, *pParts, size);
        f32 sx, sy, sw, sh;
        f32 vp[6];
        Mtx44 proj;

        Frame::getScissor(&sx, &sy, &sw, &sh);
        Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
        // Twice the texels the parts are laid out in, whatever the frame's size.
        Frame::targetBeginPixels(pKey, size * 2u, size * 2u, 0x00000000u);
        Frame::getViewportv(vp);
        NebulaMtxOrtho(proj, 0.0f, static_cast< f32 >(size), 0.0f, static_cast< f32 >(size), 0.0f, 1.0f);
        Frame::setOrthographic(proj);
        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setZScaleOffset(1.0f, 0.0f);

        // The parts' colours over each other, then their alpha again on a cleared alpha channel.
        drawFace(m, face, PassStates::use(NEBULA_FACTOR_INV_DST_ALPHA, NEBULA_FACTOR_DST_ALPHA, 0, NEBULA_COMPARE_LEQUAL, 0, NEBULA_CULL_BACK, 1, 1));
        Frame::clear(0x00000000u, NEBULA_CLEAR_ALPHA | NEBULA_CLEAR_DEPTH);
        drawFace(m, face, PassStates::use(NEBULA_FACTOR_INV_DST_ALPHA, NEBULA_FACTOR_ONE, 0, NEBULA_COMPARE_LEQUAL, 0, NEBULA_CULL_BACK, 0, 1));

        Frame::setViewport(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
        Frame::targetEnd(pKey);
        Frame::setScissor(sx, sy, sw, sh);
    }

    void makeIcon(const void* pKey, const Character& rMii, u32 width, u32 height, const NebulaColor8* pBackground) {
        const u32 faceSize = width > 128 || height > 128 ? 256 : width > 64 || height > 64 ? 128 : 64;
        Head head;
        if (!head.build(rMii, expressionBit(ExpressionNormal), faceSize)) {
            return;
        }

        NebulaColor8 back = pBackground != nullptr ? *pBackground : favoriteColor(rMii.favorite_color);
        back.a = 0;
        f32 sx, sy, sw, sh;
        f32 vp[6];
        Frame::getScissor(&sx, &sy, &sw, &sh);
        Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
        Frame::targetBeginPixels(pKey, width * 2u, height * 2u,
                                 (u32(back.r) << 24) | (u32(back.g) << 16) | (u32(back.b) << 8) | back.a);
        Frame::getViewportv(vp);
        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

        // The head 600 units in front, its face filling the picture's height (43.2 units above and below its middle).
        const f32 aspect = static_cast< f32 >(width) / static_cast< f32 >(height);
        const f32 half = width < height ? cNum_makeIcon_f1 / aspect : cNum_makeIcon_f2;
        const f32 fovy = cNum_makeIcon_f3 * cNum_makeIcon_f4 * NebulaAtan2(half, cNum_makeIcon_f5);
        Mtx44 proj;
        NebulaMtxPerspective(proj, fovy, aspect, cNum_makeIcon_f6, cNum_makeIcon_f7);
        Frame::setPerspective(proj);

        Point3d camera = {0.0f, cNum_makeIcon_f8, cNum_makeIcon_f9};
        Point3d target = {0.0f, cNum_makeIcon_f10, 0.0f};
        Vec up = {0.0f, 1.0f, 0.0f};
        Mtx view;
        NebulaMtxLookAt(view, &camera, &up, &target);
        NebulaLight light = {};
        LightValues::color(&light, NebulaColor8{255, 255, 255, 255});
        Vec lightPos = {cNum_makeIcon_f11, cNum_makeIcon_f12, cNum_makeIcon_f13};
        NebulaMtxMultVec(view, &lightPos, &lightPos);
        LightValues::pos(&light, lightPos.x, lightPos.y, lightPos.z);
        Frame::setLight(0, light);
        // Fixed to the face's own camera, which never moves.
        Frame::setLightFollows(0, true);
        head.setMatrix(view);

        Style style = {};
        style.chain = ChainLight;
        style.lightMask = MODEL_FILE_LIGHT0;
        style.diffuse = MODEL_FILE_DIFFUSE_CLAMP;
        style.atten = MODEL_FILE_ATTEN_NONE;
        style.ambient = NebulaColor8{160, 160, 160, 255};
        style.material = NebulaColor8{255, 255, 255, 255};
        style.pass = PassStates::use(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 1, NEBULA_CULL_NONE, 1, 1);
        head.drawOpa(style);
        // The face's colour over the head, then its alpha.
        style.pass = PassStates::use(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 0, NEBULA_CULL_NONE, 1, 0);
        head.drawXlu(style);
        style.pass = PassStates::use(NEBULA_FACTOR_ONE, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 0, NEBULA_CULL_NONE, 0, 1);
        head.drawXlu(style);

        Frame::targetEnd(pKey);
        Frame::setViewport(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5]);
        Frame::setScissor(sx, sy, sw, sh);
    }
}  // namespace Mii
