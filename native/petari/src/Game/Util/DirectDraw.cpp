#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "math_types.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

namespace {
    static Mtx mViewMtx;

};  // namespace

namespace TDDraw {
    void setViewMtx(MtxPtr pMtx) {
        NebulaMtxCopy(pMtx, ::mViewMtx);
    }

    void loadViewMtx(MtxPtr pMtx) {
        J3DNativeShape::setPosition(0, pMtx);
    }

    void setModelMtx(MtxPtr pMtx) {
        MR::multMtx(::mViewMtx, pMtx, MR::getCameraViewMtx());
        J3DNativeShape::setPosition(0, ::mViewMtx);
    }

    void resetViewMtx() {
        NebulaMtxCopy(MR::getCameraViewMtx(), ::mViewMtx);
        J3DNativeShape::setPosition(0, ::mViewMtx);
    }

    void close() {
        MR::loadProjectionMtx();
        MR::loadViewMtx();
        J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);
    }

    void setupNative(NebulaNativeMaterial* pMaterial, u32 drawMode, u32 blendMode, u8 viewMode) {
        J3DPass::lineWidth(1.0f);

        Mtx mtx;
        NebulaMtxIdentity(mtx);

        switch (viewMode) {
        case 0:
            NebulaMtxCopy(MR::getCameraViewMtx(), ::mViewMtx);
            J3DNativeShape::setPosition(0, ::mViewMtx);
            NebulaMtxCopy(::mViewMtx, mtx);
            break;
        case 1:
            MR::loadProjectionMtx();
            J3DNativeShape::setPosition(0, ::mViewMtx);
            break;
        case 2:
            cameraInit2D();
            NebulaMtxTrans(mtx, -MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, 0.0f);
            mtx[1][1] = -1.0f;
            J3DNativeShape::setPosition(0, mtx);
            break;
        }

        J3DNativeShape::setNormal(0, mtx);
        J3DNativeShape::setCurrentIdentity();

        static const NebulaShaderId kShaders[3] = {NEBULA_SHADER_VERTEX_COLOR, NEBULA_SHADER_DD_TEXTURE,
                                                   NEBULA_SHADER_DD_TEXTURE_COLOR};
        static const NativeShader::Blend kBlends[3] = {NativeShader::BLEND_OPAQUE, NativeShader::BLEND_ALPHA,
                                                       NativeShader::BLEND_ADD_ALPHA};
        NativeShader::init(*pMaterial, kShaders[drawMode], kBlends[blendMode], NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        pMaterial->color_write = 1;
        pMaterial->alpha_write = 0;
        pMaterial->constants[4] = -1.0f;
    }

    void sendPoint(const TVec3f& rPoint, u32 color) {
        J3DNativeDraw::position(rPoint.x, rPoint.y, rPoint.z);
        J3DNativeDraw::color(color);
    }

    void sendPoint(const TVec3f& rVec) {
        J3DNativeDraw::position(rVec.x, rVec.y, rVec.z);
    }

    void drawLine(const TVec3f& rStart, const TVec3f& rEnd, u32 color) {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_LINES, 2, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rStart.x, rStart.y, rStart.z);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rEnd.x, rEnd.y, rEnd.z);
            J3DNativeDraw::color(color);
        }
    }

    void drawCircle(const TVec3f& rCenter, const TVec3f& rNormal, f32 radius, u32 color, u32 segments) {
        TVec3f axis;

        if (rNormal.x != 0.0f) {
            axis.set< f32 >(rNormal.y, -rNormal.x, 0.0f);
        } else {
            axis.set< f32 >(0.0f, -rNormal.z, rNormal.y);
        }

        MR::normalizeOrZero(&axis);
        drawCircle(rCenter, rNormal, axis, radius, color, segments);
    }

    void drawCircle(const TVec3f& rCenter, const TVec3f& rNormal, const TVec3f& rAxis, f32 radius, u32 color, u32 segments) {
        TVec3f axis(rAxis);
        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, rNormal, 6.2831855f / segments);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_LINE_STRIP, segments + 1, NEBULA_SHAPE_COLOR0);
        for (u32 i = 0; i <= segments; i++) {
            TVec3f point(rCenter + axis * radius);
            J3DNativeDraw::position(point.x, point.y, point.z);
            J3DNativeDraw::color(color);
            NebulaMtxMultVec(rotation, axis, axis);
        }
    }

    void drawFillCircle(const TVec3f& rCenter, f32 radius, u32 centerColor, u32 edgeColor, u32 segments) {
        TVec3f point;
        point.z = rCenter.z;
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_FAN, segments + 2, NEBULA_SHAPE_COLOR0);
        J3DNativeDraw::position(rCenter.x, rCenter.y, rCenter.z);
        J3DNativeDraw::color(centerColor);

        for (u32 i = 0; i <= segments; i++) {
            point.x = rCenter.x - radius * MR::cos(2.0f * ((static_cast< f32 >(i) / segments) * JGeometry::TUtil< f32 >::PI()));
            point.y = rCenter.y + radius * MR::sin(2.0f * ((static_cast< f32 >(i) / segments) * JGeometry::TUtil< f32 >::PI()));
            J3DNativeDraw::position(point.x, point.y, point.z);
            J3DNativeDraw::color(edgeColor);
        }
    }

    void drawFillCircle(const TVec3f& rCenter, const TVec3f& rNormal, f32 radius, u32 color, u32 segments) {
        TVec3f axis;
        if (rNormal.x != 0.0f) {
            axis.set< f32 >(rNormal.y, -rNormal.x, 0.0f);
        } else {
            axis.set< f32 >(0.0f, -rNormal.z, rNormal.y);
        }

        MR::normalizeOrZero(&axis);
        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, rNormal, (6.2831855f / segments));

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_FAN, segments + 2, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rCenter.x, rCenter.y, rCenter.z);
            J3DNativeDraw::color(color);

            for (u32 i = 0; i <= segments; i++) {
                TVec3f point(rCenter + axis * radius);
                NebulaMtxMultVec(rotation, axis, axis);
                J3DNativeDraw::position(point.x, point.y, point.z);
                J3DNativeDraw::color(color);
            }
        }
    }

    void drawFillFan(const TVec3f& rCenter, const TVec3f& rNormal, const TVec3f& rAxis, u32 color, f32 startAngle, f32 endAngle, u32 segments) {
        Mtx startRotation;
        NebulaMtxRotAxisRad(startRotation, rNormal, startAngle);
        Mtx stepRotation;
        NebulaMtxRotAxisRad(stepRotation, rNormal, (endAngle - startAngle) / (segments));
        TVec3f axis;
        NebulaMtxMultVec(startRotation, rAxis, axis);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_FAN, segments + 2, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rCenter.x, rCenter.y, rCenter.z);
            J3DNativeDraw::color(color);

            for (u32 i = 0; i <= segments; i++) {
                TVec3f point(axis);
                point += rCenter;
                J3DNativeDraw::position(point.x, point.y, point.z);
                J3DNativeDraw::color(color);
                NebulaMtxMultVec(stepRotation, axis, axis);
            }
        }
    }

    void drawCylinder(const TVec3f& rBase, const TVec3f& rHeight, f32 radius, u32 baseColor, u32 topColor, u32 segments) {
        TVec3f axis;

        if (rHeight.x != 0.0f) {
            axis.set< f32 >(rHeight.y, -rHeight.x, 0.0f);
        } else {
            axis.set< f32 >(0.0f, -rHeight.z, rHeight.y);
        }

        if (MR::isNearZero(axis)) {
            return;
        }

        MR::normalizeOrZero(&axis);
        TVec3f top(rBase);
        top += rHeight;
        Mtx rotation;
        NebulaMtxRotAxisRad(rotation, rHeight, (TWO_PI / segments));

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * (segments + 1), NEBULA_SHAPE_COLOR0);
        for (u32 i = 0; i <= segments; i++) {
            TVec3f basePoint(rBase + axis * radius);
            TVec3f topPoint(top + axis * radius);
            J3DNativeDraw::position(basePoint.x, basePoint.y, basePoint.z);
            J3DNativeDraw::color(baseColor);
            J3DNativeDraw::position(topPoint.x, topPoint.y, topPoint.z);
            J3DNativeDraw::color(topColor);
            NebulaMtxMultVec(rotation, axis, axis);
        }
    }

    void drawSpherePart(const TPos3f& rMtx, f32 radius, f32 startAzimuth, f32 endAzimuth, f32 startPolar, f32 endPolar, u32 color,
                        u32 azimuthSegments, u32 polarSegments) {
        TVec3f side;
        side.set< f32 >(rMtx(0, 0), rMtx(1, 0), rMtx(2, 0));
        TVec3f up;
        up.set< f32 >(rMtx(0, 1), rMtx(1, 1), rMtx(2, 1));
        TVec3f front;
        front.set< f32 >(rMtx(0, 2), rMtx(1, 2), rMtx(2, 2));
        TVec3f center;
        rMtx.getTrans(center);
        f32 azimuthSin, azimuthCos;
        azimuthSin = MR::sin(startAzimuth);
        azimuthCos = MR::cos(startAzimuth);
        TVec3f previousRadial((side * azimuthCos + up * azimuthSin) * radius);

        for (u32 i = 1; i <= azimuthSegments; i++) {
            f32 azimuth = startAzimuth + (static_cast< f32 >(i) / azimuthSegments) * (endAzimuth - startAzimuth);
            azimuthSin = MR::sin(azimuth);
            azimuthCos = MR::cos(azimuth);
            TVec3f radial((side * azimuthCos + up * azimuthSin) * radius);

            J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * (polarSegments + 1), NEBULA_SHAPE_COLOR0);
            for (u32 j = 0; j <= polarSegments; j++) {
                f32 polar = (startPolar + (static_cast< f32 >(j) / polarSegments) * (endPolar - startPolar));
                f32 polarSin = MR::sin(polar);
                f32 polarCos = MR::cos(polar);
                TVec3f ringCenter(center + front * (polarCos * radius));
                TVec3f previousPoint(previousRadial);
                previousPoint *= polarSin;
                TVec3f point(radial);
                point *= polarSin;
                previousPoint += ringCenter;
                point += ringCenter;
                J3DNativeDraw::position(previousPoint.x, previousPoint.y, previousPoint.z);
                J3DNativeDraw::color(color);
                J3DNativeDraw::position(point.x, point.y, point.z);
                J3DNativeDraw::color(color);
            }


            previousRadial = radial;
        }
    }

    void drawSphere(const TVec3f& rCenter, f32 radius, u32 color, u32 segments) {
        TVec3f normal;
        normal = MR::getCamZdir();
        drawFillCircle(rCenter, normal, radius, color, segments);
    }

    void drawSphere3D(TVec3f center, f32 radius, u32 color, u32 segments) {
        TPos3f mtx;
        mtx.identity();
        mtx.setTrans(center);
        drawSpherePart(mtx, radius, 0.0f, TWO_PI, 0.0f, PI, color, segments, segments);
    }

    // The site's native material holds the texture (`pTexture` as it set it).
    void drawTexture(const TVec2f& rPosition, JUTTexture* pTexture, const TVec2f& rSize) {

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
        {
            J3DNativeDraw::position(rPosition.x, rPosition.y, 0.0f);
            J3DNativeDraw::texCoord(0.0f, 0.0f);
            J3DNativeDraw::position(rPosition.x + rSize.x, rPosition.y, 0.0f);
            J3DNativeDraw::texCoord(1.0f, 0.0f);
            J3DNativeDraw::position(rPosition.x + rSize.x, rPosition.y + rSize.y, 0.0f);
            J3DNativeDraw::texCoord(1.0f, 1.0f);
            J3DNativeDraw::position(rPosition.x, rPosition.y + rSize.y, 0.0f);
            J3DNativeDraw::texCoord(0.0f, 1.0f);
        }
    }

    void drawTexture3D(const TVec3f& rCenter, const TVec3f& rNormal, const TVec3f& rUp, f32 width, f32 height, JUTTexture* pTexture, bool flipX,
                       bool flipY) {
        TVec3f p3;
        TVec3f p2;
        TVec3f p1;
        TVec3f p0;
        TVec3f right;
        TVec3f up;

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
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::texCoord(left, top);
        sendPoint(p1);
        J3DNativeDraw::texCoord(rightTex, top);
        sendPoint(p2);
        J3DNativeDraw::texCoord(rightTex, bottom);
        sendPoint(p3);
        J3DNativeDraw::texCoord(left, bottom);
    }

    void drawFillBox(const TVec3f& rMin, const TVec3f& rMax, u32 color) {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rMin.x, rMin.y, rMin.z);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rMax.x, rMin.y, rMin.z);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rMax.x, rMax.y, rMax.z);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rMin.x, rMax.y, rMax.z);
            J3DNativeDraw::color(color);
        }
    }

    void drawFillBox(const TVec2f& rMin, const TVec2f& rMax, u32 color) {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rMin.x, rMin.y, 0.0f);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rMax.x, rMin.y, 0.0f);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rMax.x, rMax.y, 0.0f);
            J3DNativeDraw::color(color);
            J3DNativeDraw::position(rMin.x, rMax.y, 0.0f);
            J3DNativeDraw::color(color);
        }
    }

    void drawFillBox3D(const TVec3f& rCenter, const TVec3f& rHeight, const TVec3f& rSide, const TVec3f& rFront, u32 color) {
        TVec3f p7, p6, p5, p4, p3, p2, p1, p0;
        p0 = rCenter + rSide - rFront;
        p1 = rCenter - rSide - rFront;
        p2 = rCenter - rSide + rFront;
        p3 = rCenter + rSide + rFront;
        p4 = p0 + rHeight;
        p5 = p1 + rHeight;
        p6 = p2 + rHeight;
        p7 = p3 + rHeight;

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::color(color);
        sendPoint(p1, color);
        sendPoint(p2, color);
        sendPoint(p3, color);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p7, color);
        sendPoint(p4, color);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::color(color);
        sendPoint(p3, color);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p6, color);
        sendPoint(p2, color);
        sendPoint(p1, color);
        sendPoint(p5, color);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::color(color);
        sendPoint(p4, color);
        sendPoint(p5, color);
        sendPoint(p1, color);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p6, color);
        sendPoint(p7, color);
        sendPoint(p3, color);
        sendPoint(p2, color);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p6, color);
        sendPoint(p5, color);
        sendPoint(p4, color);
        sendPoint(p7, color);
    }

    void cameraInit3D() {
        MR::loadProjectionMtx();
    }

    void cameraInit2D() {
        const TVec3f camLoc(MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, -30.0f);
        const TVec3f objPt(MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, 0.0f);
        const TVec3f up(0, -10, 0);

        f32 width, nearZ, farZ, height;
        nearZ = 0.0f;
        farZ = 1.0f;
        width = MR::getScreenWidth() / 2;
        height = MR::getScreenHeight() / 2;
        Mtx44 projection;
        NebulaMtxOrtho(projection, height, -height, -width, width, nearZ, -farZ);
        J3DNativeView::setOrthographic(projection);
        MR::setDefaultViewportAndScissor();
    }

    void mixFogColor(TVec3f position, f32 radius, u32 packedColor) {
        f32 nearZ = MR::getNearZ();
        f32 farZ = MR::getFarZ();
        NebulaColor8 color;
        setColor8(packedColor, &color);
        f32 start;
        f32 end;
        MR::calcFogStartEnd(position, radius, &start, &end);
        J3DPass::linearFog(start, end, nearZ, farZ, color);
    }

    void invProject(TVec3f* pDest, const TVec3f& rScreen, MtxPtr pView, const f32* pProjection, const f32* pViewport, bool normalizedZ) {
        f32 screenZ;
        if (normalizedZ == false) {
            screenZ = rScreen.z / 16777215.0f;
        } else {
            screenZ = rScreen.z;
        }

        f32 depth = screenZ - pViewport[5];
        f32 w;
        f32 projectedZ = (depth * pProjection[6]) / (depth + pProjection[5] * (pViewport[5] - pViewport[4]));
        w = pProjection[5] / (pProjection[6] - projectedZ);
        f32 x;
        if (MR::isScreen16Per9()) {
            x = ((rScreen.x * MR::getScreenUnitsAcross() / MR::getScreenWidth() - (pViewport[0] + pViewport[2] / 2.0f)) / w) / (pViewport[2] / 2.0f);
        } else {
            x = ((rScreen.x - (pViewport[0] + pViewport[2] / 2.0f)) / w) / (pViewport[2] / 2.0f);
        }

        f32 y = (-(rScreen.y - (pViewport[1] + pViewport[3] / 2.0f)) / w) / (pViewport[3] / 2.0f);
        TVec3f viewPos;
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

    void project2D(TVec3f* pDest, const TVec3f& rPosition) {
        // The point in the camera's view, its perspective projection, then the viewport (left, top, width, height,
        // near, far).
        f32 viewport[6];
        J3DNativeView::getViewportv(viewport);
        const TProj3f& rProjection = MR::getCameraProjectionMtx();
        const TPos3f& rView = MR::getCameraViewMtx();
        const f32 eyeX = rView.mMtx[0][0] * rPosition.x + rView.mMtx[0][1] * rPosition.y + rView.mMtx[0][2] * rPosition.z + rView.mMtx[0][3];
        const f32 eyeY = rView.mMtx[1][0] * rPosition.x + rView.mMtx[1][1] * rPosition.y + rView.mMtx[1][2] * rPosition.z + rView.mMtx[1][3];
        const f32 eyeZ = rView.mMtx[2][0] * rPosition.x + rView.mMtx[2][1] * rPosition.y + rView.mMtx[2][2] * rPosition.z + rView.mMtx[2][3];
        const f32 clipX = eyeX * rProjection.mMtx[0][0] + eyeZ * rProjection.mMtx[0][2];
        const f32 clipY = eyeY * rProjection.mMtx[1][1] + eyeZ * rProjection.mMtx[1][2];
        const f32 clipZ = eyeZ * rProjection.mMtx[2][2] + rProjection.mMtx[2][3];
        const f32 invW = 1.0f / -eyeZ;
        pDest->x = clipX * viewport[2] / 2 * invW + viewport[0] + viewport[2] / 2;
        pDest->y = -clipY * viewport[3] / 2 * invW + viewport[1] + viewport[3] / 2;
        pDest->z = clipZ * (viewport[5] - viewport[4]) * invW + viewport[5];
        if (MR::isScreen16Per9()) {
            pDest->x *= static_cast< f32 >(MR::getScreenWidth()) / MR::getScreenUnitsAcross();
        }
    }

    void project2D(TVec2f* pDest, const TVec3f& rPosition) {
        TVec3f screen;
        project2D(&screen, rPosition);
        pDest->x = screen.x;
        pDest->y = screen.y;
    }

    void fix2Dpos(TVec3f* pPosition) {
        if (MR::isScreen16Per9()) {
            pPosition->x *= static_cast< f32 >(MR::getScreenWidth()) / MR::getScreenUnitsAcross();
        }
    }

};  // namespace TDDraw

