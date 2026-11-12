#include "Game/Render/Material.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "math_types.hpp"
#include "Game/Render/GameTexture.hpp"

namespace TDDraw {
    void close() {
        MR::loadProjectionMtx();
        MR::loadViewMtx();
    }

    void setupNative(SiteMaterial* pMaterial, u32 drawMode, u32 blendMode, u8 viewMode) {
        if (viewMode == 1) {
            MR::loadProjectionMtx();
        } else if (viewMode == 2) {
            cameraInit2D();
        }
        static const NebulaShaderId cShaders[3] = {NEBULA_SHADER_VERTEX_COLOR, NEBULA_SHADER_DD_TEXTURE, NEBULA_SHADER_DD_TEXTURE_COLOR};
        static const SiteMaterial::Blend cBlends[3] = {SiteMaterial::BLEND_OPAQUE, SiteMaterial::BLEND_ALPHA, SiteMaterial::BLEND_ADD_ALPHA};
        pMaterial->init(cShaders[drawMode], cBlends[blendMode], NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
        pMaterial->mState.color_write = 1;
        pMaterial->mState.alpha_write = 0;
        pMaterial->mState.line_width = 1.0f;
        // The alpha reference opens the shader's constants: mode 2 has its colour before it (dd_texture_color.hlsl), modes 0 and
        // 1 declare nothing else (vertex_color.hlsl, dd_texture.hlsl).
        pMaterial->constants[drawMode == 2 ? 4 : 0] = -1.0f;
    }

    TypedDraw site(const SiteMaterial& rMaterial, u8 viewMode) {
        if (viewMode == 2) {
            Mtx mtx;
            NebulaMtxTrans(mtx, -MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, 0.0f);
            mtx[1][1] = -1.0f;
            return TypedDraw::eyeSpace(rMaterial, mtx);
        }
        return TypedDraw(rMaterial, nullptr, MR::getCameraViewMtx());
    }

    void drawLine(TypedDraw& rDraw, const Vec3f& rStart, const Vec3f& rEnd, u32 color) {
        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_LINES, 2);
        {
            *vtx++ = {rStart.x, rStart.y, rStart.z, Rgba8(color)};
            *vtx++ = {rEnd.x, rEnd.y, rEnd.z, Rgba8(color)};
        }
    }

    void drawCircle(TypedDraw& rDraw, const Vec3f& rCenter, const Vec3f& rNormal, f32 radius, u32 color, u32 segments) {
        Vec3f axis;

        if (rNormal.x != 0.0f) {
            axis.set< f32 >(rNormal.y, -rNormal.x, 0.0f);
        } else {
            axis.set< f32 >(0.0f, -rNormal.z, rNormal.y);
        }

        MR::normalizeOrZero(&axis);
        drawCircle(rDraw, rCenter, rNormal, axis, radius, color, segments);
    }

    void drawCircle(TypedDraw& rDraw, const Vec3f& rCenter, const Vec3f& rNormal, const Vec3f& rAxis, f32 radius, u32 color, u32 segments) {
        Vec3f axis(rAxis);
        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, rNormal, 6.2831855f / segments);

        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_LINE_STRIP, segments + 1);
        for (u32 i = 0; i <= segments; i++) {
            Vec3f point(rCenter + axis * radius);
            *vtx++ = {point.x, point.y, point.z, Rgba8(color)};
            NebulaMtxMultVec(rotation, axis, axis);
        }
    }

    void drawFillCircle(TypedDraw& rDraw, const Vec3f& rCenter, f32 radius, u32 centerColor, u32 edgeColor, u32 segments) {
        Vec3f point;
        point.z = rCenter.z;
        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_FAN, segments + 2);
        *vtx++ = {rCenter.x, rCenter.y, rCenter.z, Rgba8(centerColor)};

        for (u32 i = 0; i <= segments; i++) {
            point.x = rCenter.x - radius * MR::cos(2.0f * ((static_cast< f32 >(i) / segments) * FloatUtil::PI()));
            point.y = rCenter.y + radius * MR::sin(2.0f * ((static_cast< f32 >(i) / segments) * FloatUtil::PI()));
            *vtx++ = {point.x, point.y, point.z, Rgba8(edgeColor)};
        }
    }

    void drawFillCircle(TypedDraw& rDraw, const Vec3f& rCenter, const Vec3f& rNormal, f32 radius, u32 color, u32 segments) {
        Vec3f axis;
        if (rNormal.x != 0.0f) {
            axis.set< f32 >(rNormal.y, -rNormal.x, 0.0f);
        } else {
            axis.set< f32 >(0.0f, -rNormal.z, rNormal.y);
        }

        MR::normalizeOrZero(&axis);
        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, rNormal, (6.2831855f / segments));

        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_FAN, segments + 2);
        {
            *vtx++ = {rCenter.x, rCenter.y, rCenter.z, Rgba8(color)};

            for (u32 i = 0; i <= segments; i++) {
                Vec3f point(rCenter + axis * radius);
                NebulaMtxMultVec(rotation, axis, axis);
                *vtx++ = {point.x, point.y, point.z, Rgba8(color)};
            }
        }
    }

    void drawFillFan(TypedDraw& rDraw, const Vec3f& rCenter, const Vec3f& rNormal, const Vec3f& rAxis, u32 color, f32 startAngle, f32 endAngle, u32 segments) {
        Mtx startRotation;
        NebulaMtxRotAxisRad(startRotation, rNormal, startAngle);
        Mtx stepRotation;
        NebulaMtxRotAxisRad(stepRotation, rNormal, (endAngle - startAngle) / (segments));
        Vec3f axis;
        NebulaMtxMultVec(startRotation, rAxis, axis);

        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_FAN, segments + 2);
        {
            *vtx++ = {rCenter.x, rCenter.y, rCenter.z, Rgba8(color)};

            for (u32 i = 0; i <= segments; i++) {
                Vec3f point(axis);
                point += rCenter;
                *vtx++ = {point.x, point.y, point.z, Rgba8(color)};
                NebulaMtxMultVec(stepRotation, axis, axis);
            }
        }
    }

    void drawCylinder(TypedDraw& rDraw, const Vec3f& rBase, const Vec3f& rHeight, f32 radius, u32 baseColor, u32 topColor, u32 segments) {
        Vec3f axis;

        if (rHeight.x != 0.0f) {
            axis.set< f32 >(rHeight.y, -rHeight.x, 0.0f);
        } else {
            axis.set< f32 >(0.0f, -rHeight.z, rHeight.y);
        }

        if (MR::isNearZero(axis)) {
            return;
        }

        MR::normalizeOrZero(&axis);
        Vec3f top(rBase);
        top += rHeight;
        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, rHeight, (TWO_PI / segments));

        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * (segments + 1));
        for (u32 i = 0; i <= segments; i++) {
            Vec3f basePoint(rBase + axis * radius);
            Vec3f topPoint(top + axis * radius);
            *vtx++ = {basePoint.x, basePoint.y, basePoint.z, Rgba8(baseColor)};
            *vtx++ = {topPoint.x, topPoint.y, topPoint.z, Rgba8(topColor)};
            NebulaMtxMultVec(rotation, axis, axis);
        }
    }

    void drawSpherePart(TypedDraw& rDraw, const Pos3f& rMtx, f32 radius, f32 startAzimuth, f32 endAzimuth, f32 startPolar, f32 endPolar, u32 color,
                        u32 azimuthSegments, u32 polarSegments) {
        Vec3f side;
        side.set< f32 >(rMtx(0, 0), rMtx(1, 0), rMtx(2, 0));
        Vec3f up;
        up.set< f32 >(rMtx(0, 1), rMtx(1, 1), rMtx(2, 1));
        Vec3f front;
        front.set< f32 >(rMtx(0, 2), rMtx(1, 2), rMtx(2, 2));
        Vec3f center;
        rMtx.getTrans(center);
        f32 azimuthSin, azimuthCos;
        azimuthSin = MR::sin(startAzimuth);
        azimuthCos = MR::cos(startAzimuth);
        Vec3f previousRadial((side * azimuthCos + up * azimuthSin) * radius);

        for (u32 i = 1; i <= azimuthSegments; i++) {
            f32 azimuth = startAzimuth + (static_cast< f32 >(i) / azimuthSegments) * (endAzimuth - startAzimuth);
            azimuthSin = MR::sin(azimuth);
            azimuthCos = MR::cos(azimuth);
            Vec3f radial((side * azimuthCos + up * azimuthSin) * radius);

            VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * (polarSegments + 1));
            for (u32 j = 0; j <= polarSegments; j++) {
                f32 polar = (startPolar + (static_cast< f32 >(j) / polarSegments) * (endPolar - startPolar));
                f32 polarSin = MR::sin(polar);
                f32 polarCos = MR::cos(polar);
                Vec3f ringCenter(center + front * (polarCos * radius));
                Vec3f previousPoint(previousRadial);
                previousPoint *= polarSin;
                Vec3f point(radial);
                point *= polarSin;
                previousPoint += ringCenter;
                point += ringCenter;
                *vtx++ = {previousPoint.x, previousPoint.y, previousPoint.z, Rgba8(color)};
                *vtx++ = {point.x, point.y, point.z, Rgba8(color)};
            }

            previousRadial = radial;
        }
    }

    void drawSphere(TypedDraw& rDraw, const Vec3f& rCenter, f32 radius, u32 color, u32 segments) {
        Vec3f normal;
        normal = MR::getCamZdir();
        drawFillCircle(rDraw, rCenter, normal, radius, color, segments);
    }

    void drawSphere3D(TypedDraw& rDraw, Vec3f center, f32 radius, u32 color, u32 segments) {
        Pos3f mtx;
        mtx.identity();
        mtx.setTrans(center);
        drawSpherePart(rDraw, mtx, radius, 0.0f, TWO_PI, 0.0f, PI, color, segments, segments);
    }

    // The site's native material holds the texture (`pTexture` as it set it).
    void drawTexture(TypedDraw& rDraw, const Vec2f& rPosition, GameTexture* /*pTexture*/, const Vec2f& rSize) {

        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        {
            *vtx++ = {rPosition.x, rPosition.y, 0.0f, 0.0f, 0.0f};
            *vtx++ = {rPosition.x + rSize.x, rPosition.y, 0.0f, 1.0f, 0.0f};
            *vtx++ = {rPosition.x + rSize.x, rPosition.y + rSize.y, 0.0f, 1.0f, 1.0f};
            *vtx++ = {rPosition.x, rPosition.y + rSize.y, 0.0f, 0.0f, 1.0f};
        }
    }

    void drawTexture3D(TypedDraw& rDraw, const Vec3f& rCenter, const Vec3f& rNormal, const Vec3f& rUp, f32 width, f32 height, GameTexture* /*pTexture*/, bool flipX,
                       bool flipY) {
        Vec3f p3;
        Vec3f p2;
        Vec3f p1;
        Vec3f p0;
        Vec3f right;
        Vec3f up;

        right.cross(rNormal, rUp);
        MR::normalizeOrZero(&right);
        up.cross(right, rNormal);
        MR::normalizeOrZero(&up);
        p0 = rCenter - right * height + up * width;
        p1 = rCenter + right * height + up * width;
        p2 = rCenter + right * height - up * width;
        p3 = rCenter - right * height - up * width;

        f32 left = flipX ? 1.0f : 0.0f;
        f32 rightTex = flipX ? 0.0f : 1.0f;
        f32 top = flipY ? 1.0f : 0.0f;
        f32 bottom = flipY ? 0.0f : 1.0f;
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p0.x, p0.y, p0.z, left, top};
        *vtx++ = {p1.x, p1.y, p1.z, rightTex, top};
        *vtx++ = {p2.x, p2.y, p2.z, rightTex, bottom};
        *vtx++ = {p3.x, p3.y, p3.z, left, bottom};
    }

    void drawFillBox(TypedDraw& rDraw, const Vec3f& rMin, const Vec3f& rMax, u32 color) {
        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        {
            *vtx++ = {rMin.x, rMin.y, rMin.z, Rgba8(color)};
            *vtx++ = {rMax.x, rMin.y, rMin.z, Rgba8(color)};
            *vtx++ = {rMax.x, rMax.y, rMax.z, Rgba8(color)};
            *vtx++ = {rMin.x, rMax.y, rMax.z, Rgba8(color)};
        }
    }

    void drawFillBox(TypedDraw& rDraw, const Vec2f& rMin, const Vec2f& rMax, u32 color) {
        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        {
            *vtx++ = {rMin.x, rMin.y, 0.0f, Rgba8(color)};
            *vtx++ = {rMax.x, rMin.y, 0.0f, Rgba8(color)};
            *vtx++ = {rMax.x, rMax.y, 0.0f, Rgba8(color)};
            *vtx++ = {rMin.x, rMax.y, 0.0f, Rgba8(color)};
        }
    }

    void drawFillBox3D(TypedDraw& rDraw, const Vec3f& rCenter, const Vec3f& rHeight, const Vec3f& rSide, const Vec3f& rFront, u32 color) {
        Vec3f p7, p6, p5, p4, p3, p2, p1, p0;
        p0 = rCenter + rSide - rFront;
        p1 = rCenter - rSide - rFront;
        p2 = rCenter - rSide + rFront;
        p3 = rCenter + rSide + rFront;
        p4 = p0 + rHeight;
        p5 = p1 + rHeight;
        p6 = p2 + rHeight;
        p7 = p3 + rHeight;

        VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p0.x, p0.y, p0.z, Rgba8(color)};
        *vtx++ = {p1.x, p1.y, p1.z, Rgba8(color)};
        *vtx++ = {p2.x, p2.y, p2.z, Rgba8(color)};
        *vtx++ = {p3.x, p3.y, p3.z, Rgba8(color)};

        vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p7.x, p7.y, p7.z, Rgba8(color)};
        *vtx++ = {p4.x, p4.y, p4.z, Rgba8(color)};
        *vtx++ = {p0.x, p0.y, p0.z, Rgba8(color)};
        *vtx++ = {p3.x, p3.y, p3.z, Rgba8(color)};

        vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p6.x, p6.y, p6.z, Rgba8(color)};
        *vtx++ = {p2.x, p2.y, p2.z, Rgba8(color)};
        *vtx++ = {p1.x, p1.y, p1.z, Rgba8(color)};
        *vtx++ = {p5.x, p5.y, p5.z, Rgba8(color)};

        vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p0.x, p0.y, p0.z, Rgba8(color)};
        *vtx++ = {p4.x, p4.y, p4.z, Rgba8(color)};
        *vtx++ = {p5.x, p5.y, p5.z, Rgba8(color)};
        *vtx++ = {p1.x, p1.y, p1.z, Rgba8(color)};

        vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p6.x, p6.y, p6.z, Rgba8(color)};
        *vtx++ = {p7.x, p7.y, p7.z, Rgba8(color)};
        *vtx++ = {p3.x, p3.y, p3.z, Rgba8(color)};
        *vtx++ = {p2.x, p2.y, p2.z, Rgba8(color)};

        vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {p6.x, p6.y, p6.z, Rgba8(color)};
        *vtx++ = {p5.x, p5.y, p5.z, Rgba8(color)};
        *vtx++ = {p4.x, p4.y, p4.z, Rgba8(color)};
        *vtx++ = {p7.x, p7.y, p7.z, Rgba8(color)};
    }

    void cameraInit3D() {
        MR::loadProjectionMtx();
    }

    void cameraInit2D() {
        const Vec3f camLoc(MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, -30.0f);
        const Vec3f objPt(MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, 0.0f);
        const Vec3f up(0, -10, 0);

        f32 width, nearZ, farZ, height;
        nearZ = 0.0f;
        farZ = 1.0f;
        width = MR::getScreenWidth() / 2;
        height = MR::getScreenHeight() / 2;
        Mtx44 projection;
        NebulaMtxOrtho(projection, height, -height, -width, width, nearZ, -farZ);
        Frame::setOrthographic(projection);
        MR::setDefaultViewportAndScissor();
    }

    void invProject(Vec3f* pDest, const Vec3f& rScreen, MtxPtr pView, const f32* pProjection, const f32* pViewport, bool normalizedZ) {
        f32 screenZ;
        if (!normalizedZ) {
            screenZ = rScreen.z / 16777215.0f;
        } else {
            screenZ = rScreen.z;
        }

        f32 depth = screenZ - pViewport[5];
        f32 w;
        f32 projectedZ = (depth * pProjection[6]) / (depth + pProjection[5] * (pViewport[5] - pViewport[4]));
        w = pProjection[5] / (pProjection[6] - projectedZ);
        const f32 x = ((rScreen.x / MR::getScreenWidth() - (pViewport[0] + pViewport[2] / 2.0f)) / w) / (pViewport[2] / 2.0f);
        const f32 y = (-(rScreen.y / MR::getScreenHeight() - (pViewport[1] + pViewport[3] / 2.0f)) / w) / (pViewport[3] / 2.0f);
        Vec3f viewPos;
        if (pProjection[0] == 0.0f) {
            viewPos.z = (projectedZ - pProjection[6]) / pProjection[5];
            viewPos.x = (x - viewPos.z * pProjection[2]) / pProjection[1];
            viewPos.y = (y - viewPos.z * pProjection[4]) / pProjection[3];
        } else {
            viewPos.z = (projectedZ - pProjection[6]) / pProjection[5];
            viewPos.x = (x - pProjection[2]) / pProjection[1];
            viewPos.y = (y - pProjection[4]) / pProjection[3];
        }

        Mtx inverseView;
        NebulaMtxInverse(pView, inverseView);
        NebulaMtxMultVec(inverseView, viewPos, pDest);
    }

    void project2D(Vec3f* pDest, const Vec3f& rPosition) {
        // The point in the camera's view, its perspective projection, then the viewport (left, top, width, height,
        // near, far).
        f32 viewport[6];
        Frame::getViewportv(viewport);
        const Proj3f& rProjection = MR::getCameraProjectionMtx();
        const Pos3f& rView = MR::getCameraViewMtx();
        const f32 eyeX = rView.mMtx[0][0] * rPosition.x + rView.mMtx[0][1] * rPosition.y + rView.mMtx[0][2] * rPosition.z + rView.mMtx[0][3];
        const f32 eyeY = rView.mMtx[1][0] * rPosition.x + rView.mMtx[1][1] * rPosition.y + rView.mMtx[1][2] * rPosition.z + rView.mMtx[1][3];
        const f32 eyeZ = rView.mMtx[2][0] * rPosition.x + rView.mMtx[2][1] * rPosition.y + rView.mMtx[2][2] * rPosition.z + rView.mMtx[2][3];
        const f32 clipX = eyeX * rProjection.mMtx[0][0] + eyeZ * rProjection.mMtx[0][2];
        const f32 clipY = eyeY * rProjection.mMtx[1][1] + eyeZ * rProjection.mMtx[1][2];
        const f32 clipZ = eyeZ * rProjection.mMtx[2][2] + rProjection.mMtx[2][3];
        const f32 invW = 1.0f / -eyeZ;
        pDest->x = (clipX * viewport[2] / 2 * invW + viewport[0] + viewport[2] / 2) * MR::getScreenWidth();
        pDest->y = (-clipY * viewport[3] / 2 * invW + viewport[1] + viewport[3] / 2) * MR::getScreenHeight();
        pDest->z = clipZ * (viewport[5] - viewport[4]) * invW + viewport[5];
    }

    void project2D(Vec2f* pDest, const Vec3f& rPosition) {
        Vec3f screen;
        project2D(&screen, rPosition);
        pDest->x = screen.x;
        pDest->y = screen.y;
    }

};  // namespace TDDraw

