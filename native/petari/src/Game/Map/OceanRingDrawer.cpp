#include "Game/Map/OceanRingDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    const f32 sDistancePartMesh = 13000.0f;
    const f32 sTexSpeed0U = -0.003f;
    const f32 sTexSpeed0V = -0.001f;
    const f32 sTexSpeed1U = -0.002f;
    const f32 sTexSpeed1V = 0.001f;
    const f32 sTexSpeed2V = 0.003f;
    const f32 sIndirectScale = 0.1f;
    const f32 sBloomCameraOffsetZ = 3000.0f;
    const f32 sDistancePartDrawBloom = 4000.0f;

    // Draw numbers a part keeps for itself: one strip per segment of the part, a part has fewer segments than this.
    const u32 cPartsPerDrawer = 256;

    // Material colours 0 and 4.
    const NebulaColor8 cBaseColor = {0x28, 0x28, 0x28, 0x14};
    const NebulaColor8 cWaterColor = {0x76, 0xD7, 0xFF, 0xFF};

    // Texture steps: U per segment along the rail, V per point across it. Textures 0 and 1 share the step.
    const f32 cTexStep01 = 0.05f;
    const f32 cTexStep2 = 0.1f;

    struct OceanRingUV {
        f32 u0, u1, u2;
        f32 v01, v2;
    };

    // Points on the ring's whole strip: one per segment, plus point 0 again on a loop rail.
    s32 getRingStripPoints(const OceanRing* pRing) {
        return (MR::isLoopRail(pRing) ? 1 : 0) + pRing->mSegCount;
    }

    // Per segment of the part: beginStrip(vertex count), then emitPair(near point, its UV, far point, its UV) for each point
    // across the ring. Coordinates are summed step by step; the order fixes their rounding.
    template < typename BeginStrip, typename EmitPair >
    void walkPartSegments(const OceanRingPartDrawer& rPart, BeginStrip beginStrip, EmitPair emitPair) {
        const OceanRing* pRing = rPart.mOceanRing;
        OceanRingUV nearUV = {rPart.mTexU0, rPart.mTexU1, rPart.mTexU2, 0.0f, 0.0f};
        OceanRingUV farUV = nearUV;

        for (s32 i = 0; i < rPart.mNumPoints - 1; i++) {
            const s32 index = rPart.mStartIndex + i;
            s32 nextIndex = index + 1;
            if (i == rPart.mNumPoints - 2 && rPart.mIsClosed) {
                nextIndex = 0;
            }

            farUV.u0 += cTexStep01;
            farUV.u1 += cTexStep01;
            farUV.u2 += cTexStep2;
            nearUV.v01 = 0.0f;
            nearUV.v2 = 0.0f;
            farUV.v01 = 0.0f;
            farUV.v2 = 0.0f;
            beginStrip(pRing->mStride * 2);

            for (s32 j = 0; j < pRing->mStride; j++) {
                emitPair(pRing->getPoint(j, index), nearUV, pRing->getPoint(j, nextIndex), farUV);
                nearUV.v01 += cTexStep01;
                nearUV.v2 += cTexStep2;
                farUV.v01 += cTexStep01;
                farUV.v2 += cTexStep2;
            }

            nearUV.u0 = farUV.u0;
            nearUV.u1 = farUV.u1;
            nearUV.u2 = farUV.u2;
        }
    }

    VertexPCT3 makeVertexPCT3(const WaterPoint* pPoint, const OceanRingUV& rUV) {
        return {pPoint->mPosition.x, pPoint->mPosition.y, pPoint->mPosition.z, Rgba8(0xFF, 0xFF, 0xFF, pPoint->mAlpha),
                rUV.u0, rUV.v01, rUV.u1, rUV.v01, rUV.u2, rUV.v2};
    }
}  // namespace

OceanRingPartDrawer::OceanRingPartDrawer(const OceanRing* pRing, int startIndex, int numPoints, bool isClosed, f32* pTexCoordU0,
                                         f32* pTexCoordU1, f32* pTexCoordU2)
    : mPosition(0.0f, 0.0f, 0.0f) {
    mOceanRing = pRing;
    mStartIndex = startIndex;
    mNumPoints = numPoints;
    mIsClosed = isClosed;
    mTexU0 = *pTexCoordU0;
    mTexU1 = *pTexCoordU1;
    mTexU2 = *pTexCoordU2;
    initMesh(pTexCoordU0, pTexCoordU1, pTexCoordU2);

    // The centre of the part: the mean of its rail points' middle water points (a closed part ends on point 0).
    for (int i = 0; i < numPoints; i++) {
        int pointIndex = startIndex + i;

        if (i == numPoints - 1 && mIsClosed) {
            pointIndex = 0;
        }

        mPosition.add(((mOceanRing->mWaterPoints[(pointIndex * mOceanRing->mStride - 1) + mOceanRing->mStride / 2])->mOrigPos));
    }

    mPosition.scale(1.0f / numPoints);
}

void OceanRingPartDrawer::initMesh(f32* pTexCoordU0, f32* pTexCoordU1, f32* pTexCoordU2) {
    mMesh.init( 2 * mOceanRing->mStride * (mNumPoints - 1));
    drawGD(pTexCoordU0, pTexCoordU1, pTexCoordU2);
    mMesh.send();
}

void OceanRingPartDrawer::draw(TypedDraw& rDraw) const {
    if (mPosition.distance(*MR::getPlayerPos()) < ::sDistancePartMesh) {
        drawDynamic(rDraw);
    } else {
        mMesh.draw(rDraw);
    }
}

void OceanRingPartDrawer::drawGD(f32* pTexCoordU0, f32* pTexCoordU1, f32* pTexCoordU2) {
    VertexPCT3* vtx = nullptr;
    walkPartSegments(
        *this, [&](s32 count) { vtx = mMesh.strip(count); },
        [&](const WaterPoint* pNear, const OceanRingUV& rNearUV, const WaterPoint* pFar, const OceanRingUV& rFarUV) {
            *vtx++ = makeVertexPCT3(pNear, rNearUV);
            *vtx++ = makeVertexPCT3(pFar, rFarUV);
        });

    // The next part's U starts where this one ends.
    for (s32 i = 0; i < mNumPoints - 1; i++) {
        *pTexCoordU0 += cTexStep01;
        *pTexCoordU1 += cTexStep01;
        *pTexCoordU2 += cTexStep2;
    }
}

void OceanRingPartDrawer::drawDynamic(TypedDraw& rDraw) const {
    VertexPCT3* vtx = nullptr;
    walkPartSegments(
        *this, [&](s32 count) { vtx = rDraw.vertices< VertexPCT3 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, count); },
        [&](const WaterPoint* pNear, const OceanRingUV& rNearUV, const WaterPoint* pFar, const OceanRingUV& rFarUV) {
            *vtx++ = makeVertexPCT3(pNear, rNearUV);
            *vtx++ = makeVertexPCT3(pFar, rFarUV);
        });
}

void OceanRingPartDrawer::drawDynamicBloom(TypedDraw& rDraw) const {
    VertexPT2* vtx = nullptr;
    walkPartSegments(
        *this, [&](s32 count) { vtx = rDraw.vertices< VertexPT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, count); },
        [&](const WaterPoint* pNear, const OceanRingUV& rNearUV, const WaterPoint* pFar, const OceanRingUV& rFarUV) {
            *vtx++ = {pNear->mPosition.x, pNear->mPosition.y, pNear->mPosition.z, rNearUV.u0, rNearUV.v01, rNearUV.u1, rNearUV.v01};
            *vtx++ = {pFar->mPosition.x, pFar->mPosition.y, pFar->mPosition.z, rFarUV.u0, rFarUV.v01, rFarUV.u1, rFarUV.v01};
        });
}

OceanRingDrawer::OceanRingDrawer(const OceanRing* pOceanRing) {
    mRing = pOceanRing;
    mDrawerCount = 0;
    mPartDrawers = nullptr;
    mTex0U = 0.0f;
    mTex0V = 0.0f;
    mTex1U = 0.0f;
    mTex1V = 0.0f;
    mTex2V = 0.0f;
    mWaterTex = nullptr;
    mWaterIndTex = nullptr;

    mWaterTex = new GameTexture(MR::loadTexFromArc("WaterWave", "Water.bti"));
    mWaterIndTex = new GameTexture(MR::loadTexFromArc("WaterWave", "WaterIndirect.bti"));
    initParts();
    initMesh();
}

void OceanRingDrawer::update() {
    mTex0U = MR::repeat(mTex0U + ::sTexSpeed0U, 0.0f, 1.0f);
    mTex0V = MR::repeat(mTex0V + ::sTexSpeed0V, 0.0f, 1.0f);
    mTex1U = MR::repeat(mTex1U + ::sTexSpeed1U, 0.0f, 1.0f);
    mTex1V = MR::repeat(mTex1V + ::sTexSpeed1V, 0.0f, 1.0f);
    mTex2V = MR::repeat(mTex2V + ::sTexSpeed2V, 0.0f, 1.0f);
}

void OceanRingDrawer::draw() const {
    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    if (mRing->mIsClipped) {
        mMesh.draw(site);
    } else {
        for (s32 i = 0; i < mDrawerCount; i++) {
            // Each part numbers its draws from its own range: one switching between its sent mesh and its strips leaves the
            // parts after it on their numbers.
            site.setPart(static_cast< u32 >(i) * cPartsPerDrawer);
            getDrawer(i)->draw(site);
        }
    }
}

void OceanRingDrawer::drawBloom() const {
    const SiteMaterial material = loadMaterialBloom();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    Vec3f zDir = MR::getCamZdir();
    zDir.scale(::sBloomCameraOffsetZ);
    Vec3f camPos = MR::getCamPos();
    zDir.add(camPos);

    if (!mRing->mIsClipped) {
        for (s32 i = 0; i < mDrawerCount; i++) {
            OceanRingPartDrawer* drwr = getDrawer(i);

            if (drwr->mPosition.distance(zDir) < ::sDistancePartDrawBloom) {
                site.setPart(static_cast< u32 >(i) * cPartsPerDrawer);
                drwr->drawDynamicBloom(site);
            }
        }
    }
}

// One part per ten segments or so, each sharing its end point with the next; the last takes the rest and, on a loop rail,
// closes on point 0. The texture U runs on across parts.
void OceanRingDrawer::initParts() {
    mDrawerCount = (mRing->mSegCount / 10) + 1;
    const s32 segmentsPerPart = mRing->mSegCount / mDrawerCount;
    f32 texU0 = 0.0f, texU1 = 0.0f, texU2 = 0.0f;
    mPartDrawers = new OceanRingPartDrawer*[mDrawerCount];
    for (s32 i = 0; i < mDrawerCount - 1; i++) {
        mPartDrawers[i] = new OceanRingPartDrawer(mRing, segmentsPerPart * i, segmentsPerPart + 1, false, &texU0, &texU1, &texU2);
    }

    const s32 last = mDrawerCount - 1;
    const s32 lastStart = segmentsPerPart * last;
    s32 lastNumPoints = mRing->mSegCount - lastStart;
    bool isClosed = false;

    if (MR::isLoopRail(mRing)) {
        isClosed = true;
        lastNumPoints += 1;
    }

    mPartDrawers[last] = new OceanRingPartDrawer(mRing, lastStart, lastNumPoints, isClosed, &texU0, &texU1, &texU2);
}

void OceanRingDrawer::initMesh() {
    mMesh.init(2 * getRingStripPoints(mRing));
    drawGD();
    mMesh.send();
}

// The whole ring as one flat strip between its two edge lines (last and first point across) at rest; V matches the
// part strips' at those points.
void OceanRingDrawer::drawGD() {
    const s32 numPoints = getRingStripPoints(mRing);
    const s32 lastAcross = mRing->mStride - 1;
    const f32 edgeV01 = cTexStep01 * lastAcross;
    const f32 edgeV2 = cTexStep2 * lastAcross;
    f32 u01 = 0.0f;
    f32 u2 = 0.0f;

    VertexPCT3* vtx = mMesh.strip(numPoints * 2);
    for (s32 i = 0; i < numPoints; i++) {
        const s32 seg = i == mRing->mSegCount ? 0 : i;
        const WaterPoint* pLast = mRing->getPoint(lastAcross, seg);
        const WaterPoint* pFirst = mRing->getPoint(0, seg);
        *vtx++ = {pLast->mOrigPos.x, pLast->mOrigPos.y, pLast->mOrigPos.z, Rgba8(0xFF, 0xFF, 0xFF, pLast->mAlpha), u01, edgeV01, u01, edgeV01, u2, edgeV2};
        *vtx++ = {pFirst->mOrigPos.x, pFirst->mOrigPos.y, pFirst->mOrigPos.z, Rgba8(0xFF, 0xFF, 0xFF, pFirst->mAlpha), u01, 0.0f, u01, 0.0f, u2, 0.0f};

        u01 += cTexStep01;
        u2 += cTexStep2;
    }
}

SiteMaterial OceanRingDrawer::loadMaterial() const {

    // runtime/src/render/materials/ocean_ring.hlsl: the water (texture 0) at the first two scrolls, color1's highlight,
    // the screen behind (texture 1, by texture matrix 3) tinted by color2 and warped by the indirect texture (2, the third
    // scroll) unless the camera is in the water; blended by the vertex alpha where above zero, depth tested and not
    // written, no culling.
    GameTexture screen(MR::getScreenResTIMG());
    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_RING, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                       NEBULA_CULL_NONE);
    Mtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTex0U;
    mtx.mMtx[1][2] = mTex0V;
    material.setMatrix(3, 0, mtx.toMtxPtr());
    mtx.mMtx[0][2] = mTex1U;
    mtx.mMtx[1][2] = mTex1V;
    material.setMatrix(3, 1, mtx.toMtxPtr());
    mtx.mMtx[0][2] = 0.0f;
    mtx.mMtx[1][2] = mTex2V;
    material.setMatrix(3, 2, mtx.toMtxPtr());
    Pos3f projection;
    MR::calcTexProjectionMtx(&projection);
    material.setMatrix(3, 3, projection);
    material.setColor(0, ::cBaseColor.r, ::cBaseColor.g, ::cBaseColor.b, ::cBaseColor.a);
    material.setColor(4, ::cWaterColor.r, ::cWaterColor.g, ::cWaterColor.b, ::cWaterColor.a);
    material.constants[8] = ::sIndirectScale;
    material.constants[9] = MR::isCameraInWater() ? 0.0f : 1.0f;
    material.constants[10] = static_cast< f32 >(screen.getWidth());
    material.constants[11] = static_cast< f32 >(screen.getHeight());
    material.setTexture(0, mWaterTex);
    material.setTexture(1, &screen);
    material.setTexture(2, mWaterIndTex);
    return material;
}

SiteMaterial OceanRingDrawer::loadMaterialBloom() const {

    // The bloom fades in as the camera looks up (-0.2 to 0.1) and sinks (150 to 300 deep); the lower of the two wins.
    Vec3f zDir = MR::getCamZdir();
    const f32 lookUpRate = ((MR::clamp(zDir.y, -0.2f, 0.1f) - -0.2f) / 0.3f);
    f32 rate = ((MR::clamp(WaterAreaFunction::getCameraWaterDepth(), 150.0f, 300.0f) - 150.0f) / 150.0f);

    if (lookUpRate < rate) {
        rate = lookUpRate;
    }

    const f32 alphaF = (150.0f + (105.0f * rate));
    const u8 alpha = (s32)(alphaF);
    // runtime/src/render/materials/ocean_ring_bloom.hlsl: (80, 80, 80) plus the water (texture 0) at the first two
    // scrolls times (80, 80, 80, alpha); blended by alpha where above zero, depth tested and written, no culling.
    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_RING_BLOOM, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    Mtx34f mtx;
    mtx.identity();
    mtx.mMtx[0][2] = mTex0U;
    mtx.mMtx[1][2] = mTex0V;
    material.setMatrix(2, 0, mtx.toMtxPtr());
    mtx.mMtx[0][2] = mTex1U;
    mtx.mMtx[1][2] = mTex1V;
    material.setMatrix(2, 1, mtx.toMtxPtr());
    material.setColor(0, 80, 80, 80, alpha);
    material.setColor(4, 80, 80, 80, 255);
    material.setTexture(0, mWaterTex);
    return material;
}
