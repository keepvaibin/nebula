#include "Game/MapObj/GCaptureRibbon.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcLineWidth_f1, "calcLineWidth.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcLineWidth_f2, "calcLineWidth.f2", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcLineWidth_f3, "calcLineWidth.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcLineWidth_f4, "calcLineWidth.f4", 90.0f);

namespace {
    // One quad from start to end, `rOffset` to each side; s runs across the ribbon and t along it.
    void drawStrip(TypedDraw& rSite, const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rOffset) {
        VertexPT* vtx = rSite.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        *vtx++ = {rOffset.x + rStart.x, rOffset.y + rStart.y, rOffset.z + rStart.z, 0.0f, 0.0f};
        *vtx++ = {rStart.x - rOffset.x, rStart.y - rOffset.y, rStart.z - rOffset.z, 1.0f, 0.0f};
        *vtx++ = {rOffset.x + rEnd.x, rOffset.y + rEnd.y, rOffset.z + rEnd.z, 0.0f, 1.0f};
        *vtx = {rEnd.x - rOffset.x, rEnd.y - rOffset.y, rEnd.z - rOffset.z, 1.0f, 1.0f};
    }
};  // namespace

GCaptureRibbon::GCaptureRibbon(const char* pName) : LiveActor(pName), mTexture(nullptr), mIsLineSet(false) {
}

void GCaptureRibbon::draw() const {
    if (!mIsLineSet) {
        return;
    }

    TDDraw::cameraInit3D();
    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 2, 0);
    f32 width = calcLineWidth();
    material.setTexture(0, mTexture);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);

    // Two crossed quads, so the ribbon shows from any side.
    TypedDraw site = TDDraw::site(material, 0);
    drawStrip(site, mStart, mEnd, mUp * width);
    drawStrip(site, mStart, mEnd, mSide * width);
}

void GCaptureRibbon::reset() {
    mIsLineSet = false;
}

void GCaptureRibbon::setLine(const Vec3f& rStart, const Vec3f& rEnd) {
    mStart.set(rStart);
    mEnd.set(rEnd);
    mIsLineSet = true;

    Vec3f dir = mEnd - mStart;
    MR::normalizeOrZero(&dir);

    if (MR::isNearZero(dir)) {
        mSide = Vec3f(1, 0, 0);
        mUp = Vec3f(0, 1, 0);
    } else {
        MR::makeAxisVerticalZX(&mSide, dir);
        mUp.cross(dir, mSide);
    }
}

// 90 wide up to 500 long, narrowing to 20 at 2000.
f32 GCaptureRibbon::calcLineWidth() const {
    f32 rate = MR::normalize(mStart.distance(mEnd), cNum_calcLineWidth_f1, cNum_calcLineWidth_f2);
    return (cNum_calcLineWidth_f3 * rate) + (cNum_calcLineWidth_f4 * (1.0f - rate));
}

void GCaptureRibbon::init(const TableIter& /*rIter*/) {
    mTexture = new GameTexture(MR::loadTexFromArc("GCaptureRibbon", "GCaptureRibbon.bti"));
    MR::invalidateClipping(this);
    makeActorAppeared();
}
