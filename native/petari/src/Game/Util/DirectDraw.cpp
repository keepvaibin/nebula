#include "Game/Util/DirectDraw.hpp"
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
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXGeometry.h>
#include <revolution/gx/GXVert.h>

namespace {
    static Mtx mViewMtx;

};  // namespace

namespace TDDraw {
    void DirectDraw_FORCE_MATCH_SDATA2() {
        1.0f;
        0.0f;
        0.5f;
        -1.0f;
        3.1415927f;
        2.0f;
        6.2831855f;
        0.001f;
        -30.0f;
        16777215.0f;
    }

    void setViewMtx(MtxPtr pMtx) {
        PSMTXCopy(pMtx, ::mViewMtx);
    }

    void loadViewMtx(MtxPtr pMtx) {
        J3DNativeShape::sendPosition(pMtx, 0);
    }

    void setModelMtx(MtxPtr pMtx) {
        MR::multMtx(::mViewMtx, pMtx, MR::getCameraViewMtx());
        J3DNativeShape::sendPosition(::mViewMtx, 0);
    }

    void resetViewMtx() {
        PSMTXCopy(MR::getCameraViewMtx(), ::mViewMtx);
        J3DNativeShape::sendPosition(::mViewMtx, 0);
    }

    void close() {
        MR::loadProjectionMtx();
        MR::loadViewMtx();
        J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    }

    void setup(u32 drawMode, u32 blendMode, u8 viewMode) {
        GXColor color = {0, 0, 0, 0};

        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
        J3DNativeState::setCoPlanar(0);
        J3DNativeState::setZMode(0, GX_ALWAYS, 0);
        J3DNativeState::setZCompLoc(0);
        J3DNativeState::setColorUpdate(GX_TRUE);
        J3DNativeState::setAlphaUpdate(0);
        J3DNativeState::setLineWidth(6, GX_TO_ZERO);
        J3DNativeState::setChanMatColor(GX_COLOR0A0, color);
        J3DNativeState::setChanAmbColor(GX_COLOR0A0, color);
        J3DNativeState::setChanMatColor(GX_COLOR1A1, color);
        J3DNativeState::setChanAmbColor(GX_COLOR1A1, color);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, 0, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setNumChans(1);

        Mtx mtx;
        PSMTXIdentity(mtx);

        switch (viewMode) {
        case 0:
            PSMTXCopy(MR::getCameraViewMtx(), ::mViewMtx);
            J3DNativeShape::sendPosition(::mViewMtx, 0);
            PSMTXCopy(::mViewMtx, mtx);
            break;
        case 1:
            MR::loadProjectionMtx();
            J3DNativeShape::sendPosition(::mViewMtx, 0);
            break;
        case 2:
            cameraInit2D();
            PSMTXTrans(mtx, -MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, 0.0f);
            mtx[1][1] = -1.0f;
            J3DNativeShape::sendPosition(mtx, 0);
            break;
        }

        J3DNativeShape::sendNormal(mtx, 0);
        J3DNativeState::setCurrentMtx(0);
        J3DNativeState::setTevDirect(GX_TEVSTAGE0);
        J3DNativeState::setNumIndStages(0);

        switch (drawMode) {
        case 0:
            J3DNativeState::setNumTexGens(0);
            J3DNativeState::setNumTevStages(1);
            J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            break;
        case 1:
            J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
            J3DNativeState::setNumTexGens(1);
            J3DNativeState::setNumTevStages(1);
            J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
            J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_REPLACE);
            break;
        case 2:
            J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
            J3DNativeState::setNumTexGens(1);
            J3DNativeState::setNumTevStages(1);
            J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
            J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
            J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
            J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);
            J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);
            break;
        }

        switch (blendMode) {
        case 2:
            J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
            J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            break;
        case 1:
            J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
            J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            break;
        case 0:
            J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
            J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            break;
        }

        J3DNativeState::setFog(GX_FOG_NONE, 0.0, 0.0, 0.0, 0.0, color);
        J3DNativeState::setFogRangeAdj(GX_FALSE, 0, nullptr);
    }

    void sendPoint(const TVec3f& rPoint, u32 color) NO_INLINE {
        J3DNativeDraw::position(rPoint.x, rPoint.y, rPoint.z);
        J3DNativeDraw::color(color);
    }

    void sendPoint(const TVec3f& rVec) NO_INLINE {
        J3DNativeDraw::position(rVec.x, rVec.y, rVec.z);
    }

    void drawLine(const TVec3f& rStart, const TVec3f& rEnd, u32 color) {
        J3DNativeDraw::begin(GX_LINES, 2, NEBULA_SHAPE_COLOR0);
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
        PSMTXRotAxisRad(rotation, rNormal, 6.2831855f / segments);

        J3DNativeDraw::begin(GX_LINESTRIP, segments + 1, NEBULA_SHAPE_COLOR0);
        for (u32 i = 0; i <= segments; i++) {
            TVec3f point(rCenter + axis * radius);
            J3DNativeDraw::position(point.x, point.y, point.z);
            J3DNativeDraw::color(color);
            PSMTXMultVec(rotation, axis, axis);
        }
    }

    void drawFillCircle(const TVec3f& rCenter, f32 radius, u32 centerColor, u32 edgeColor, u32 segments) {
        TVec3f point;
        point.z = rCenter.z;
        J3DNativeDraw::begin(GX_TRIANGLEFAN, segments + 2, NEBULA_SHAPE_COLOR0);
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
        PSMTXRotAxisRad(rotation, rNormal, (6.2831855f / segments));

        J3DNativeDraw::begin(GX_TRIANGLEFAN, segments + 2, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rCenter.x, rCenter.y, rCenter.z);
            J3DNativeDraw::color(color);

            for (u32 i = 0; i <= segments; i++) {
                TVec3f point(rCenter + axis * radius);
                PSMTXMultVec(rotation, axis, axis);
                J3DNativeDraw::position(point.x, point.y, point.z);
                J3DNativeDraw::color(color);
            }
        }
    }

    void drawFillFan(const TVec3f& rCenter, const TVec3f& rNormal, const TVec3f& rAxis, u32 color, f32 startAngle, f32 endAngle, u32 segments) {
        Mtx startRotation;
        PSMTXRotAxisRad(startRotation, rNormal, startAngle);
        Mtx stepRotation;
        PSMTXRotAxisRad(stepRotation, rNormal, (endAngle - startAngle) / (segments));
        TVec3f axis;
        PSMTXMultVec(startRotation, rAxis, axis);

        J3DNativeDraw::begin(GX_TRIANGLEFAN, segments + 2, NEBULA_SHAPE_COLOR0);
        {
            J3DNativeDraw::position(rCenter.x, rCenter.y, rCenter.z);
            J3DNativeDraw::color(color);

            for (u32 i = 0; i <= segments; i++) {
                TVec3f point(axis);
                point += rCenter;
                J3DNativeDraw::position(point.x, point.y, point.z);
                J3DNativeDraw::color(color);
                PSMTXMultVec(stepRotation, axis, axis);
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
        PSMTXRotAxisRad(rotation, rHeight, (TWO_PI / segments));

        J3DNativeDraw::begin(GX_TRIANGLESTRIP, 2 * (segments + 1), NEBULA_SHAPE_COLOR0);
        for (u32 i = 0; i <= segments; i++) {
            TVec3f basePoint(rBase + axis * radius);
            TVec3f topPoint(top + axis * radius);
            J3DNativeDraw::position(basePoint.x, basePoint.y, basePoint.z);
            J3DNativeDraw::color(baseColor);
            J3DNativeDraw::position(topPoint.x, topPoint.y, topPoint.z);
            J3DNativeDraw::color(topColor);
            PSMTXMultVec(rotation, axis, axis);
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

            J3DNativeDraw::begin(GX_TRIANGLESTRIP, 2 * (polarSegments + 1), NEBULA_SHAPE_COLOR0);
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

    void drawTexture(const TVec2f& rPosition, JUTTexture* pTexture, const TVec2f& rSize) {
        pTexture->load(GX_TEXMAP0);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
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

        if (pTexture != nullptr) {
            pTexture->load(GX_TEXMAP0);
        }

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
        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
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
        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
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
        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
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

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::color(color);
        sendPoint(p1, color);
        sendPoint(p2, color);
        sendPoint(p3, color);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p7, color);
        sendPoint(p4, color);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::color(color);
        sendPoint(p3, color);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p6, color);
        sendPoint(p2, color);
        sendPoint(p1, color);
        sendPoint(p5, color);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
        J3DNativeDraw::position(p0.x, p0.y, p0.z);
        J3DNativeDraw::color(color);
        sendPoint(p4, color);
        sendPoint(p5, color);
        sendPoint(p1, color);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p6, color);
        sendPoint(p7, color);
        sendPoint(p3, color);
        sendPoint(p2, color);

        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
        sendPoint(p6, color);
        sendPoint(p5, color);
        sendPoint(p4, color);
        sendPoint(p7, color);
    }

    void cameraInit3D() {
        MR::loadProjectionMtx();
    }

    void cameraInit2D() {
        static TVec3f camLoc(MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, -30.0f);
        static TVec3f objPt(MR::getScreenWidth() / 2.0f, MR::getScreenHeight() / 2.0f, 0.0f);
        static TVec3f up(0, -10, 0);

        f32 width, nearZ, farZ, height;
        nearZ = 0.0f;
        farZ = 1.0f;
        width = MR::getScreenWidth() / 2;
        height = MR::getScreenHeight() / 2;
        Mtx44 projection;
        C_MTXOrtho(projection, height, -height, -width, width, nearZ, -farZ);
        J3DNativeView::setProjection(projection, GX_ORTHOGRAPHIC);
        MR::setDefaultViewportAndScissor();
    }

    void mixFogColor(TVec3f position, f32 radius, u32 packedColor) {
        f32 nearZ = MR::getNearZ();
        f32 farZ = MR::getFarZ();
        GXColor color;
        setGXColor(packedColor, &color);
        f32 start;
        f32 end;
        MR::calcFogStartEnd(position, radius, &start, &end);
        J3DNativeState::setFog(GX_FOG_PERSP_LIN, start, end, nearZ, farZ, color);
    }

    namespace {
        u8 tileBuffer[0x800] ATTRIBUTE_ALIGN(32);
    }

    inline void tileConversion8Impl(u8* pTexture, u32 width, u32 height, u8* pTileBuffer) {
        u8* pDest = pTexture;
        u8* pBuffer = pTileBuffer;
        u32 offset, row, y, x, sourceOffset;
        offset = 0;

        for (y = 0; y < height; y += 4) {
            for (x = 0; x < width; x += 8) {
                for (row = 0; row < 4; row++) {
                    sourceOffset = ((y + row) * width) + x;
                    *reinterpret_cast< u32* >(&pBuffer[offset]) = *reinterpret_cast< const u32* >(&pTexture[sourceOffset]);
                    *reinterpret_cast< u32* >(&pBuffer[offset + 4]) = *reinterpret_cast< const u32* >(&pTexture[sourceOffset + 4]);
                    offset += 8;
                }
            }

            MR::copyMemory(pDest, pBuffer, width * 4);
            pDest += width * 4;
            offset = 0;
        }
    }

    void tileConversion8(u8* pTexture, u32 width, u32 height) {
        tileConversion8Impl(pTexture, width, height, tileBuffer);
    }

    inline void tileConversion16Impl(u16* pTexture, u32 width, u32 height, u8* pBuffer) {
        u8* pDest = reinterpret_cast< u8* >(pTexture);
        u8* const pBufferLocal = pBuffer;
        u32 offset, y, row, column, x;
        offset = 0;
        for (y = 0; y < height; y += 4, pDest += 32) {
            for (x = 0; x < width; x += 4) {
                for (row = 0; row < 4; row++) {
                    for (column = 0; column < 4; column++) {
                        // The texels are GX data, in the console's byte order.
                        pBufferLocal[offset] = reinterpret_cast< NEBULA_BE(u16)* >(pTexture)[(y + row) * width + (x + column)];
                        offset++;
                    }
                }
            }

            MR::copyMemory(pDest, pBufferLocal, 32);
            offset = 0;
        }
    }

    void tileConversion16(u16* pTexture, u32 width, u32 height) {
        tileConversion16Impl(pTexture, width, height, tileBuffer);
    }

    u32 getTexel32(const JUTTexture* pTexture, u32 x, u32 y) {
        return getTexel32(pTexture->mImage, pTexture->getWidth(), x, y);
    }

    u32 getTexel32(const u8* pTexture, u32 width, u32 x, u32 y) {
        u32 offset = ((width << 4) & ~0x3F) * (y >> 2) + (((x & 3) << 1) + (((y & 3) << 3) + ((x << 4) & ~0x3F)));
        return (pTexture[offset] | (pTexture[offset + 0x21] << 8)) | ((pTexture[offset + 1] << 24) | (pTexture[offset + 0x20] << 16));
    }

    void setTexel32(JUTTexture* pTexture, u32 x, u32 y, u32 color) {
        setTexel32(pTexture->mImage, pTexture->getWidth(), x, y, color);
    }

    void setTexel32(u8* pTexture, u32 width, u32 x, u32 y, u32 color) {
        u32 offset = ((width << 4) & ~0x3F) * (y >> 2) + (((x & 3) << 1) + (((y & 3) << 3) + ((x << 4) & ~0x3F)));
        pTexture[offset] = color;
        pTexture[offset + 1] = color >> 24;
        pTexture[offset + 0x20] = color >> 16;
        pTexture[offset + 0x21] = color >> 8;
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
            x = ((rScreen.x * MR::getFrameBufferWidth() / MR::getScreenWidth() - (pViewport[0] + pViewport[2] / 2.0f)) / w) / (pViewport[2] / 2.0f);
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
        PSMTXInverse(pView, inverseView);
        PSMTXMultVec(inverseView, viewPos, pDest);
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
            pDest->x *= static_cast< f32 >(MR::getScreenWidth()) / MR::getFrameBufferWidth();
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
            pPosition->x *= static_cast< f32 >(MR::getScreenWidth()) / MR::getFrameBufferWidth();
        }
    }

};  // namespace TDDraw

void DirectDraw_FORCE_MATCH(TPos3f* pMtx, TVec3f* pVec) {
    pMtx->getTrans(*pVec);
    pMtx->setTrans(*pVec);
}

void DirectDraw_FORCE_MATCH_CTOR(int x, int y, int z) {
    TVec3f v(x, y, z);
}
