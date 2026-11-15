#include "Game/Particle/ParticleDraw.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleResources.hpp"
#include "Game/Particle/ParticleShape.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_math.h>
#include <string.h>

// What the emitters draw, as runs of one material: quads as instances in the frame's particles, other shapes as corners
// in the frame's ring. Everything a particle changes rides on it; a new image, size or material ends the run.
namespace {
    // Corners (points, lines, stripes) in the world, each with where it was a game frame ago; the vertex stage places them
    // at the presented frame's time, through the camera at that time.
    // The three lists grow together to the run's corner count.
    MR::GrowBuffer< VertexCorner > sBatch;
    // For shapes that read them: coordinate 1 (the corner's own) and 2 (its projection's q).
    MR::GrowBuffer< f32 > sBatchUv1;
    MR::GrowBuffer< f32 > sBatchQ;
    bool sBatchExtras = false;
    u32 sBatchCount = 0;
    // NEBULA_PRIMITIVE_QUADS: the run is of instances.
    u8 sBatchPrimitive = NEBULA_PRIMITIVE_QUADS;
    // The instances' run: the first of the frame's particles, how many, and the vertices each draws (6 a quad, 12 two).
    u32 sRunFirst = 0;
    u32 sRunCount = 0;
    u32 sRunVertices = 6;
    u32 sSource = 0;

    // A move beyond this in one game frame is a jump, not motion: nothing draws the way between.
    const f32 cJumpSquared = 2000.0f * 2000.0f;

    Mtx sCamera;
    Mtx sEyeToWorld;
    // The particle being drawn; whether this is its first draw of the game frame.
    ParticleCore* sParticle = nullptr;
    bool sFresh = false;
    // Its matrix in the world, now and a frame ago, and its coordinates': none, two rows, or a projection (three rows).
    Mtx sWorld;
    Mtx sWorldBefore;
    enum { TexMtx_None, TexMtx_Rows, TexMtx_Projection };
    u32 sTexMtxMode = TexMtx_None;
    f32 sTexMtx[3][4];
    // The image in place 0 (a particle's animation changes it; repeats do not end the run).
    s32 sBatchImage = -1;

    bool jumped(const f32* pFrom, f32 x, f32 y, f32 z) {
        const f32 dx = x - pFrom[0];
        const f32 dy = y - pFrom[1];
        const f32 dz = z - pFrom[2];
        return dx * dx + dy * dy + dz * dz > cJumpSquared;
    }

    // The particle's first write of a game frame (its matrix or a corner). It continues from the game frame before only if
    // it wrote then: what it wrote becomes its place a frame ago. A particle that was reached and skipped (invisible, no
    // direction) wrote nothing, so a frame it sat out starts it over at its new place instead of at an older one.
    void startFrame(ParticleCore* pParticle) {
        sFresh = false;
        const u32 frame = NebulaFrameClock::count();
        pParticle->mDrawnContinues = pParticle->mDrawnFrame + 1 == frame;
        pParticle->mDrawnFrame = frame;
        if (pParticle->mDrawnContinues) {
            memcpy(pParticle->mDrawnBefore, pParticle->mDrawnNow, sizeof(pParticle->mDrawnBefore));
        }
    }

    // `pMtx`: the particle's matrix to eye space. Its first draw of a game frame keeps it in the world.
    void setParticleMtx(const Mtx pMtx) {
        NebulaMtxConcat(sEyeToWorld, pMtx, sWorld);
        ParticleCore* const pParticle = sParticle;
        if (pParticle == nullptr) {
            memcpy(sWorldBefore, sWorld, sizeof(Mtx));
            return;
        }
        if (sFresh) {
            startFrame(pParticle);
            memcpy(pParticle->mDrawnNow, sWorld, sizeof(Mtx));
            const f32 before[3] = {pParticle->mDrawnBefore[3], pParticle->mDrawnBefore[7], pParticle->mDrawnBefore[11]};
            if (!pParticle->mDrawnContinues || jumped(before, sWorld[0][3], sWorld[1][3], sWorld[2][3])) {
                memcpy(pParticle->mDrawnBefore, sWorld, sizeof(Mtx));
            }
        }
        memcpy(sWorldBefore, pParticle->mDrawnBefore, sizeof(Mtx));
    }

    void setTexMtx(const Mtx mtx, u32 mode) {
        memcpy(sTexMtx, mtx, sizeof(sTexMtx));
        sTexMtxMode = mode;
    }

    // A corner: `pNow` and `pBefore` in the world; x, y, z its place (a projection reads them).
    void addCorner(const f32* pNow, const f32* pBefore, f32 x, f32 y, f32 z, f32 s, f32 t) {
        const u32 v = sBatchCount++;
        VertexCorner& rCorner = sBatch.room(sBatchCount)[v];
        rCorner.x = pNow[0];
        rCorner.y = pNow[1];
        rCorner.z = pNow[2];
        rCorner.bx = pBefore[0];
        rCorner.by = pBefore[1];
        rCorner.bz = pBefore[2];
        rCorner.prm = Rgba8(ParticleMaterial::prmWord());
        rCorner.env = Rgba8(ParticleMaterial::envWord());
        f32 u = s;
        f32 w = t;
        f32 q = 1.0f;
        if (sTexMtxMode == TexMtx_Rows) {
            u = sTexMtx[0][0] * s + sTexMtx[0][1] * t + sTexMtx[0][2] + sTexMtx[0][3];
            w = sTexMtx[1][0] * s + sTexMtx[1][1] * t + sTexMtx[1][2] + sTexMtx[1][3];
        } else if (sTexMtxMode == TexMtx_Projection) {
            u = sTexMtx[0][0] * x + sTexMtx[0][1] * y + sTexMtx[0][2] * z + sTexMtx[0][3];
            w = sTexMtx[1][0] * x + sTexMtx[1][1] * y + sTexMtx[1][2] * z + sTexMtx[1][3];
            q = sTexMtx[2][0] * x + sTexMtx[2][1] * y + sTexMtx[2][2] * z + sTexMtx[2][3];
        }
        rCorner.s = u;
        rCorner.t = w;
        if (sBatchExtras) {
            f32* const pUv1 = sBatchUv1.room(sBatchCount * 2);
            f32* const pQ = sBatchQ.room(sBatchCount * 2);
            pUv1[v * 2] = s;
            pUv1[v * 2 + 1] = t;
            pQ[v * 2] = q;
            pQ[v * 2 + 1] = 0.0f;
        }
    }
}  // namespace

namespace ParticleDraw {
    void camera(const f32 (*pMtx)[4]) {
        memcpy(sCamera, pMtx, sizeof(Mtx));
        NebulaMtxInverse(pMtx, sEyeToWorld);
    }

    void primitive(u8 primitive) {
        if (sBatchPrimitive != primitive || primitive == NEBULA_PRIMITIVE_TRIANGLE_STRIP) {
            flush();
            sBatchPrimitive = primitive;
        }
    }

    // The particle to draw next; its first write of the game frame (setParticleMtx, corner) starts its frame.
    void particle(ParticleCore* pParticle) {
        sParticle = pParticle;
        sFresh = pParticle->mDrawnFrame != NebulaFrameClock::count();
    }

    void corner(u32 index, f32 x, f32 y, f32 z, f32 s, f32 t) {
        if (sFresh) {
            startFrame(sParticle);
        }
        f32* const pNow = sParticle->mDrawnNow + index * 3;
        f32* const pBefore = sParticle->mDrawnBefore + index * 3;
        pNow[0] = x;
        pNow[1] = y;
        pNow[2] = z;
        if (!sParticle->mDrawnContinues || jumped(pBefore, x, y, z)) {
            pBefore[0] = x;
            pBefore[1] = y;
            pBefore[2] = z;
        }
        addCorner(pNow, pBefore, x, y, z, s, t);
    }

    void setMatrix(const f32 (*pMtx)[4]) {
        setParticleMtx(pMtx);
    }

    void clearCoord() {
        sTexMtxMode = TexMtx_None;
    }

    void setCoordRows(const f32 (*pMtx)[4]) {
        setTexMtx(pMtx, TexMtx_Rows);
    }

    void setProjection(const f32 (*pMtx)[4]) {
        setTexMtx(pMtx, TexMtx_Projection);
    }

    void quad(bool crossed) {
        const u32 vertices = crossed ? 12 : 6;
        if (sBatchPrimitive != NEBULA_PRIMITIVE_QUADS) {
            flush();
            sBatchPrimitive = NEBULA_PRIMITIVE_QUADS;
        }
        // A run is consecutive rows: one that something else's rows followed is drawn first.
        if (sRunCount != 0 && (sRunVertices != vertices || FrameRing::instanceRowCount() != sRunFirst + sRunCount * (sizeof(NebulaParticleInstance) / 16))) {
            flush();
        }
        if (sRunCount == 0) {
            sRunFirst = FrameRing::instanceRowCount();
            sRunVertices = vertices;
        }
        NebulaParticleInstance& rOut = FrameRing::particle();
        memcpy(rOut.now, sWorld, sizeof(rOut.now));
        memcpy(rOut.before, sWorldBefore, sizeof(rOut.before));
        memcpy(rOut.coord, sTexMtx, sizeof(rOut.coord));
        rOut.prm = ParticleMaterial::prmWord();
        rOut.env = ParticleMaterial::envWord();
        rOut.coord_mode = sTexMtxMode;
        rOut.reserved = 0;
        sRunCount++;
    }

    void image(EmitterWork* pWork, s32 image) {
        if (image != sBatchImage) {
            flush();
            sBatchImage = image;
            pWork->mpResMgr->use(image, 0);
        }
    }

    void begin(bool extras) {
        flush();
        sBatchExtras = extras;
        sParticle = nullptr;
        sTexMtxMode = TexMtx_None;
        sBatchImage = -1;
    }

    void setSource(u32 source) {
        sSource = source;
    }

    void flush() {
        if (sRunCount != 0) {
            TypedDraw site(ParticleMaterial::material(), nullptr, sCamera);
            site.setSource(sSource);
            site.drawParticles(sRunFirst, sRunCount, sRunVertices);
            sRunCount = 0;
        }
        if (sBatchCount == 0) {
            return;
        }
        TypedDraw site(ParticleMaterial::material(), nullptr, sCamera);
        site.setSource(sSource);
        site.setFlags(NEBULA_ITEM_PARTICLE);
        const NebulaPrimitive primitive = static_cast< NebulaPrimitive >(sBatchPrimitive);
        if (!sBatchExtras) {
            memcpy(site.vertices< VertexCorner >(primitive, sBatchCount), sBatch.mItems, sBatchCount * sizeof(VertexCorner));
        } else {
            VertexCornerExtras* pOut = site.vertices< VertexCornerExtras >(primitive, sBatchCount);
            for (u32 v = 0; v < sBatchCount; v++) {
                const VertexCorner& rCorner = sBatch[v];
                pOut[v] = {rCorner.x,  rCorner.y,  rCorner.z,  rCorner.bx,      rCorner.by,          rCorner.bz,
                           rCorner.prm, rCorner.env, rCorner.s, rCorner.t,       sBatchUv1[v * 2],    sBatchUv1[v * 2 + 1],
                           sBatchQ[v * 2], sBatchQ[v * 2 + 1]};
            }
        }
        sBatchCount = 0;
    }
}  // namespace ParticleDraw

namespace {
    void loadPrj(EmitterWork const* pWork, const Mtx pSrt) {
        Mtx mtx;
        NebulaMtxConcat(pWork->mPrjMtx, pSrt, mtx);
        ParticleDraw::setProjection(mtx);
    }

    void loadPrjAnm(EmitterWork const* pWork, const Mtx pSrt) {
        f32 frame = pWork->mpEmtr->getAge();
        const ParticleShape* pShape = pWork->mpRes->getBsp();
        f32 pivotS = 0.5f * (1.0f + pShape->getTilingS());
        f32 pivotT = 0.5f * (1.0f + pShape->getTilingT());
        f32 translationS = (frame * pShape->getIncTransX()) + pShape->getInitTransX();
        f32 translationT = (frame * pShape->getIncTransY()) + pShape->getInitTransY();
        f32 scaleS = (frame * pShape->getIncScaleX()) + pShape->getInitScaleX();
        f32 scaleT = (frame * pShape->getIncScaleY()) + pShape->getInitScaleY();
        s32 angle = (frame * pShape->getIncRot()) + pShape->getInitRot();
        f32 sin = Angle::sinShort(angle);
        f32 cos = Angle::cosShort(angle);
        Mtx matrix;
        matrix[0][0] = scaleS * cos;
        matrix[0][1] = -scaleS * sin;
        matrix[0][2] = (pivotS + (scaleS * ((sin * (pivotT + translationT)) - (cos * (pivotS + translationS)))));
        matrix[0][3] = 0.0f;
        matrix[1][0] = scaleT * sin;
        matrix[1][1] = scaleT * cos;
        matrix[1][2] = (pivotT + (-scaleT * ((sin * (pivotS + translationS)) + (cos * (pivotT + translationT)))));
        matrix[1][3] = 0.0f;
        matrix[2][0] = 0.0f;
        matrix[2][1] = 0.0f;
        matrix[2][2] = 1.0f;
        matrix[2][3] = 0.0f;
        NebulaMtxConcat(matrix, pWork->mPrjMtx, matrix);
        NebulaMtxConcat(matrix, pSrt, matrix);
        ParticleDraw::setProjection(matrix);
    }

    // The coordinates' projection by the emitter's type: none, the projection, or the projection under the shape's
    // coordinate animation.
    void loadProjection(EmitterWork const* pWork, const Mtx pSrt) {
        switch (pWork->mPrjType) {
        case 1:
            loadPrj(pWork, pSrt);
            break;
        case 2:
            loadPrjAnm(pWork, pSrt);
            break;
        default:
            break;
        }
    }

    void dirTypeVel(EmitterWork const* /*pWork*/, ParticleCore const* pParticle, Vec3f* pDirection) {
        pParticle->getVelVec(*pDirection);
    }

    void dirTypePos(EmitterWork const* /*pWork*/, ParticleCore const* pParticle, Vec3f* pDirection) {
        pParticle->getLocalPosition(*pDirection);
    }

    void dirTypePosInv(EmitterWork const* /*pWork*/, ParticleCore const* pParticle, Vec3f* pDirection) {
        pParticle->getLocalPosition(*pDirection);
        pDirection->negate();
    }

    void dirTypeEmtrDir(EmitterWork const* pWork, ParticleCore const* /*pParticle*/, Vec3f* pDirection) {
        pDirection->set(pWork->mGlobalEmtrDir);
    }

    void dirTypePrevPtcl(EmitterWork const* pWork, ParticleCore const* pParticle, Vec3f* pDirection) {
        Vec3f position;
        pParticle->getGlobalPosition(position);
        ParticleSlot* pEnd = pWork->mpAlivePtcl->getEnd();
        ParticleSlot* pPrev = pWork->mpCurNode->getPrev();

        if (pEnd != pPrev) {
            ParticleCore* pPrevious = pWork->mpCurNode->getPrev()->getObject();
            pPrevious->getGlobalPosition(*pDirection);
        } else {
            pWork->mpEmtr->calcEmitterGlobalPosition(pDirection);
        }

        pDirection->sub(position);
    }

    void rotTypeY(f32 sin, f32 cos, Mtx& rMtx) {
        rMtx[0][0] = cos;
        rMtx[0][1] = 0.0f;
        rMtx[0][2] = -sin;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = 0.0f;
        rMtx[1][1] = 1.0f;
        rMtx[1][2] = 0.0f;
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = sin;
        rMtx[2][1] = 0.0f;
        rMtx[2][2] = cos;
        rMtx[2][3] = 0.0f;
    }

    void rotTypeX(f32 sin, f32 cos, Mtx& rMtx) {
        rMtx[0][0] = 1.0f;
        rMtx[0][1] = 0.0f;
        rMtx[0][2] = 0.0f;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = 0.0f;
        rMtx[1][1] = cos;
        rMtx[1][2] = -sin;
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = 0.0f;
        rMtx[2][1] = sin;
        rMtx[2][2] = cos;
        rMtx[2][3] = 0.0f;
    }

    void rotTypeZ(f32 sin, f32 cos, Mtx& rMtx) {
        rMtx[0][0] = cos;
        rMtx[0][1] = -sin;
        rMtx[0][2] = 0.0f;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = sin;
        rMtx[1][1] = cos;
        rMtx[1][2] = 0.0f;
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = 0.0f;
        rMtx[2][1] = 0.0f;
        rMtx[2][2] = 1.0f;
        rMtx[2][3] = 0.0f;
    }

    void rotTypeXYZ(f32 sin, f32 cos, Mtx& rMtx) {
        f32 third = 0.333333f * (1.0f - cos);
        f32 scaledSin = 0.57735f * sin;
        f32 plus = third + scaledSin;
        rMtx[0][0] = (third + cos);
        rMtx[0][1] = (third - scaledSin);
        rMtx[0][2] = plus;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = plus;
        rMtx[1][1] = (third + cos);
        rMtx[1][2] = (third - scaledSin);
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = (third - scaledSin);
        rMtx[2][1] = plus;
        rMtx[2][2] = (third + cos);
        rMtx[2][3] = 0.0f;
    }

    void basePlaneTypeXY(MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        pMtx[0][0] *= scaleX;
        pMtx[1][0] *= scaleX;
        pMtx[2][0] *= scaleX;
        pMtx[0][1] *= scaleY;
        pMtx[1][1] *= scaleY;
        pMtx[2][1] *= scaleY;
    }

    void basePlaneTypeXZ(MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        pMtx[0][0] *= scaleX;
        pMtx[1][0] *= scaleX;
        pMtx[2][0] *= scaleX;
        pMtx[0][2] *= scaleY;
        pMtx[1][2] *= scaleY;
        pMtx[2][2] *= scaleY;
    }

    void basePlaneTypeX(MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        pMtx[0][0] *= scaleX;
        pMtx[1][0] *= scaleX;
        pMtx[2][0] *= scaleX;
        pMtx[0][1] *= scaleY;
        pMtx[1][1] *= scaleY;
        pMtx[2][1] *= scaleY;
        pMtx[0][2] *= scaleX;
        pMtx[1][2] *= scaleX;
        pMtx[2][2] *= scaleX;
    }

    // The way a particle faces by the emitter's type: its velocity, its place, away from its place, the emitter's
    // direction, toward the particle before it (or the emitter).
    void particleDirection(EmitterWork const* pWork, ParticleCore const* pParticle, Vec3f* pDirection) {
        switch (pWork->mDirType) {
        case 1:
            dirTypePos(pWork, pParticle, pDirection);
            break;
        case 2:
            dirTypePosInv(pWork, pParticle, pDirection);
            break;
        case 3:
            dirTypeEmtrDir(pWork, pParticle, pDirection);
            break;
        case 4:
            dirTypePrevPtcl(pWork, pParticle, pDirection);
            break;
        default:
            dirTypeVel(pWork, pParticle, pDirection);
            break;
        }
    }

    // The turn by the emitter's rotation type: about y (0 and 4), x, z, or the diagonal.
    void rotationOf(u32 type, f32 sin, f32 cos, Mtx& rMtx) {
        switch (type) {
        case 1:
            rotTypeX(sin, cos, rMtx);
            break;
        case 2:
            rotTypeZ(sin, cos, rMtx);
            break;
        case 3:
            rotTypeXYZ(sin, cos, rMtx);
            break;
        default:
            rotTypeY(sin, cos, rMtx);
            break;
        }
    }

    // The size on the shape's plane: xy, xz, or x across both.
    void scalePlane(u32 type, MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        switch (type) {
        case 1:
            basePlaneTypeXZ(pMtx, scaleX, scaleY);
            break;
        case 2:
            basePlaneTypeX(pMtx, scaleX, scaleY);
            break;
        default:
            basePlaneTypeXY(pMtx, scaleX, scaleY);
            break;
        }
    }
}  // namespace

// A camera-facing quad at the particle, scaled, in view space.
void ParticleDrawBillboard(EmitterWork* work, ParticleCore* ptcl) {
    if (ptcl->checkStatus(ParticleStatus_Invisible) == 0) {
        Vec3f viewPos;
        NebulaMtxMultVec(work->mPosCamMtx, &ptcl->mPosition, &viewPos);
        Mtx mtx;
        mtx[0][0] = work->mGlobalPtclScl.x * ptcl->mParticleScaleX;
        mtx[0][3] = viewPos.x;
        mtx[1][1] = work->mGlobalPtclScl.y * ptcl->mParticleScaleY;
        mtx[1][3] = viewPos.y;
        mtx[2][2] = 1.0f;
        mtx[2][3] = viewPos.z;
        mtx[2][1] = 0.0f;
        mtx[2][0] = 0.0f;
        mtx[1][2] = 0.0f;
        mtx[1][0] = 0.0f;
        mtx[0][2] = 0.0f;
        mtx[0][1] = 0.0f;
        setParticleMtx(mtx);
        loadProjection(work, mtx);
        ParticleDraw::quad(false);
    }
}

// The billboard turned by the particle's angle about the view axis.
void ParticleDrawRotBillboard(EmitterWork* work, ParticleCore* ptcl) {
    if (ptcl->checkStatus(ParticleStatus_Invisible) == 0) {
        Vec3f viewPos;
        NebulaMtxMultVec(work->mPosCamMtx, &ptcl->mPosition, &viewPos);
        f32 sinRot, cosRot;
        Angle::sinCosShort(ptcl->mRotateAngle, &sinRot, &cosRot);
        f32 particleX = work->mGlobalPtclScl.x * ptcl->mParticleScaleX;
        f32 particleY = work->mGlobalPtclScl.y * ptcl->mParticleScaleY;

        Mtx mtx;
        mtx[0][0] = cosRot * particleX;
        mtx[0][1] = -sinRot * particleY;
        mtx[0][3] = viewPos.x;
        mtx[1][0] = sinRot * particleX;
        mtx[1][1] = cosRot * particleY;
        mtx[1][3] = viewPos.y;
        mtx[2][2] = 1.0f;
        mtx[2][3] = viewPos.z;
        mtx[2][1] = 0.0f;
        mtx[2][0] = 0.0f;
        mtx[1][2] = 0.0f;
        mtx[0][2] = 0.0f;
        setParticleMtx(mtx);
        loadProjection(work, mtx);
        ParticleDraw::quad(false);
    }
}

// A world-space plane: the emitter's rotation axis and plane type, at the particle.
void ParticleDrawRotation(EmitterWork* work, ParticleCore* ptcl) {
    if (ptcl->checkStatus(ParticleStatus_Invisible) == 0) {
        f32 sinRot, cosRot;
        Angle::sinCosShort(ptcl->mRotateAngle, &sinRot, &cosRot);
        f32 particleX = work->mGlobalPtclScl.x * ptcl->mParticleScaleX;
        f32 particleY = work->mGlobalPtclScl.y * ptcl->mParticleScaleY;
        Mtx mtx;
        rotationOf(work->mRotType, sinRot, cosRot, mtx);
        scalePlane(work->mPlaneType, mtx, particleX, particleY);
        mtx[0][3] = ptcl->mPosition.x;
        mtx[1][3] = ptcl->mPosition.y;
        mtx[2][3] = ptcl->mPosition.z;
        NebulaMtxConcat(work->mPosCamMtx, mtx, mtx);
        setParticleMtx(mtx);
        loadProjection(work, mtx);
        ParticleDraw::quad(work->mListType != 0);
    }
}

void ParticleDrawPoint(EmitterWork* /*work*/, ParticleCore* ptcl) {
    if (!!(ptcl->mStatus & ParticleStatus_Invisible)) {
        return;
    }

    ParticleDraw::primitive(NEBULA_PRIMITIVE_POINTS);
    ParticleDraw::corner(0, ptcl->mPosition.x, ptcl->mPosition.y, ptcl->mPosition.z, 0.0f, 0.0f);
}

void ParticleDrawDirection(EmitterWork* pWork, ParticleCore* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    Vec3f direction;
    Vec3f side;
    particleDirection(pWork, pParticle, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);
    side.cross(pParticle->mBaseAxis, direction);
    if (MR::isNearZero(side)) {
        return;
    }

    MR::normalize(&side);
    pParticle->mBaseAxis.cross(direction, side);
    MR::normalize(&pParticle->mBaseAxis);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    matrix[0][0] = pParticle->mBaseAxis.x;
    matrix[0][1] = direction.x;
    matrix[0][2] = side.x;
    matrix[0][3] = pParticle->mPosition.x;
    matrix[1][0] = pParticle->mBaseAxis.y;
    matrix[1][1] = direction.y;
    matrix[1][2] = side.y;
    matrix[1][3] = pParticle->mPosition.y;
    matrix[2][0] = pParticle->mBaseAxis.z;
    matrix[2][1] = direction.z;
    matrix[2][2] = side.z;
    matrix[2][3] = pParticle->mPosition.z;
    scalePlane(pWork->mPlaneType, matrix, scaleX, scaleY);
    NebulaMtxConcat(pWork->mPosCamMtx, matrix, matrix);
    setParticleMtx(matrix);
    loadProjection(pWork, matrix);
    ParticleDraw::quad(pWork->mListType != 0);
}

void ParticleDrawRotDirection(EmitterWork* pWork, ParticleCore* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    f32 sin = Angle::sinShort(pParticle->mRotateAngle);
    f32 cos = Angle::cosShort(pParticle->mRotateAngle);
    Vec3f direction;
    Vec3f side;
    particleDirection(pWork, pParticle, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);
    side.cross(pParticle->mBaseAxis, direction);
    if (MR::isNearZero(side)) {
        return;
    }

    MR::normalize(&side);
    pParticle->mBaseAxis.cross(direction, side);
    MR::normalize(&pParticle->mBaseAxis);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx rotation;
    Mtx matrix;
    rotationOf(pWork->mRotType, sin, cos, rotation);
    scalePlane(pWork->mPlaneType, rotation, scaleX, scaleY);
    matrix[0][0] = pParticle->mBaseAxis.x;
    matrix[0][1] = direction.x;
    matrix[0][2] = side.x;
    matrix[0][3] = pParticle->mPosition.x;
    matrix[1][0] = pParticle->mBaseAxis.y;
    matrix[1][1] = direction.y;
    matrix[1][2] = side.y;
    matrix[1][3] = pParticle->mPosition.y;
    matrix[2][0] = pParticle->mBaseAxis.z;
    matrix[2][1] = direction.z;
    matrix[2][2] = side.z;
    matrix[2][3] = pParticle->mPosition.z;
    NebulaMtxConcat(matrix, rotation, rotation);
    NebulaMtxConcat(pWork->mPosCamMtx, rotation, matrix);
    setParticleMtx(matrix);
    loadProjection(pWork, matrix);
    ParticleDraw::quad(pWork->mListType != 0);
}

void ParticleDrawDBillboard(EmitterWork* pWork, ParticleCore* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    Vec3f direction;
    particleDirection(pWork, pParticle, &direction);
    Vec3f camera(pWork->mPosCamMtx[2][0], pWork->mPosCamMtx[2][1], pWork->mPosCamMtx[2][2]);
    NebulaVecCrossProduct(&direction, &camera, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);
    NebulaMtxMultVecSR(pWork->mPosCamMtx, &direction, &direction);
    Vec3f position;
    NebulaMtxMultVec(pWork->mPosCamMtx, &pParticle->mPosition, &position);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    matrix[0][0] = direction.x * scaleX;
    matrix[0][1] = -direction.y * scaleY;
    matrix[0][3] = position.x;
    matrix[1][0] = direction.y * scaleX;
    matrix[1][1] = direction.x * scaleY;
    matrix[1][3] = position.y;
    matrix[2][2] = 1.0f;
    matrix[2][3] = position.z;
    matrix[0][2] = matrix[1][2] = matrix[2][0] = matrix[2][1] = 0.0f;
    setParticleMtx(matrix);
    loadProjection(pWork, matrix);
    ParticleDraw::quad(false);
}

void ParticleDrawYBillboard(EmitterWork* pWork, ParticleCore* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    Vec3f axis(0.0f, pWork->mPosCamMtx[1][1], pWork->mPosCamMtx[2][1]);
    if (MR::isNearZero(axis)) {
        return;
    }

    Vec3f position;
    NebulaMtxMultVec(pWork->mPosCamMtx, &pParticle->mPosition, &position);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    matrix[0][0] = scaleX;
    matrix[0][3] = position.x;
    matrix[1][1] = pWork->mYBBCamMtx[1][1] * scaleY;
    matrix[1][2] = pWork->mYBBCamMtx[1][2];
    matrix[1][3] = position.y;
    matrix[2][1] = pWork->mYBBCamMtx[2][1] * scaleY;
    matrix[2][2] = pWork->mYBBCamMtx[2][2];
    matrix[2][3] = position.z;
    matrix[0][1] = matrix[0][2] = matrix[1][0] = matrix[2][0] = 0.0f;
    setParticleMtx(matrix);
    loadProjection(pWork, matrix);
    ParticleDraw::quad(false);
}

void ParticleDrawRotYBillboard(EmitterWork* pWork, ParticleCore* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    Vec3f axis(0.0f, pWork->mPosCamMtx[1][1], pWork->mPosCamMtx[2][1]);
    if (MR::isNearZero(axis)) {
        return;
    }

    Vec3f position;
    NebulaMtxMultVec(pWork->mPosCamMtx, &pParticle->mPosition, &position);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    f32 sin = Angle::sinShort(pParticle->mRotateAngle);
    f32 cos = Angle::cosShort(pParticle->mRotateAngle);
    f32 sinX = sin * scaleX;
    f32 cosY = cos * scaleY;
    f32 y = pWork->mYBBCamMtx[1][1];
    f32 z = pWork->mYBBCamMtx[2][1];
    matrix[0][0] = (cos * scaleX);
    matrix[0][1] = (-sin * scaleY);
    matrix[0][2] = 0.0f;
    matrix[0][3] = position.x;
    matrix[1][0] = sinX * y;
    matrix[1][1] = cosY * y;
    matrix[1][2] = -z;
    matrix[1][3] = position.y;
    matrix[2][0] = sinX * z;
    matrix[2][1] = cosY * z;
    matrix[2][2] = y;
    matrix[2][3] = position.z;
    setParticleMtx(matrix);
    loadProjection(pWork, matrix);
    ParticleDraw::quad(false);
}

void ParticleDrawLine(EmitterWork* pWork, ParticleCore* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    Vec3f position(pParticle->mPosition);
    Vec3f end;
    end.set(pParticle->mVelocity);
    if (MR::isNearZero(end)) {
        return;
    }

    end.setLength(pWork->mGlobalPtclScl.y * (25.0f * pParticle->mParticleScaleY));
    end.sub(position, end);
    ParticleDraw::primitive(NEBULA_PRIMITIVE_LINES);
    ParticleDraw::corner(0, position.x, position.y, position.z, 0.0f, 0.0f);
    ParticleDraw::corner(1, end.x, end.y, end.z, 0.0f, 1.0f);
}

void ParticleDrawStripe(EmitterWork* pWork) {
    const u32 count = pWork->mpAlivePtcl->getNum();
    ParticleShape* pShape = pWork->mpRes->getBsp();
    if (count < 2) {
        return;
    }

    f32 texCoord = 0.0f;
    f32 texStep = 1.0f / (count - 1.0f);
    f32 leftWidth = (1.0f + pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    f32 rightWidth = (1.0f - pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    ParticleSlot* pFirst;
    bool backward = false;
    if (pShape->isDrawFwdAhead()) {
        texCoord = 1.0f;
        texStep = -texStep;
        pFirst = pWork->mpAlivePtcl->getLast();
        backward = true;
    } else {
        pFirst = pWork->mpAlivePtcl->getFirst();
    }

    setParticleMtx(pWork->mPosCamMtx);
    loadProjection(pWork, pWork->mPosCamMtx);
    Vec3f position;
    Vec3f direction;
    Vec3f side;
    Vec3f vertices[2];
    Mtx matrix;
    ParticleDraw::primitive(NEBULA_PRIMITIVE_TRIANGLE_STRIP);
    for (ParticleSlot* pNode = pFirst; pNode != pWork->mpAlivePtcl->getEnd();) {
        ParticleCore* pParticle = pNode->getObject();
        pWork->mpCurNode = pNode;
        ParticleDraw::particle(pParticle);
        position.set(pParticle->mPosition);
        f32 sin = Angle::sinShort(pParticle->mRotateAngle);
        f32 cos = Angle::cosShort(pParticle->mRotateAngle);
        vertices[0].set(-pParticle->mParticleScaleX * leftWidth, 0.0f, 0.0f);
        vertices[0].set< f32 >(vertices[0].x * cos, 0.0f, vertices[0].x * sin);
        vertices[1].set< f32 >(pParticle->mParticleScaleX * rightWidth, 0.0f, 0.0f);
        vertices[1].set< f32 >(vertices[1].x * cos, 0.0f, vertices[1].x * sin);
        particleDirection(pWork, pParticle, &direction);
        if (MR::isNearZero(direction)) {
            direction.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            MR::normalize(&direction);
        }

        NebulaVecCrossProduct(&pParticle->mBaseAxis, &direction, &side);
        if (MR::isNearZero(side)) {
            side.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&side);
        }

        NebulaVecCrossProduct(&direction, &side, &pParticle->mBaseAxis);
        MR::normalize(&pParticle->mBaseAxis);
        matrix[0][0] = side.x;
        matrix[0][1] = direction.x;
        matrix[0][2] = pParticle->mBaseAxis.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = side.y;
        matrix[1][1] = direction.y;
        matrix[1][2] = pParticle->mBaseAxis.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = side.z;
        matrix[2][1] = direction.z;
        matrix[2][2] = pParticle->mBaseAxis.z;
        matrix[2][3] = 0.0f;
        NebulaMtxMultVecArraySR(matrix, vertices, vertices, 2);
        ParticleDraw::corner(0, vertices[0].x + position.x, vertices[0].y + position.y, vertices[0].z + position.z, 0.0f, texCoord);
        ParticleDraw::corner(1, vertices[1].x + position.x, vertices[1].y + position.y, vertices[1].z + position.z, 1.0f, texCoord);
        pNode = backward ? pNode->getPrev() : pNode->getNext();
        texCoord += texStep;
    }
}

void ParticleDrawStripeX(EmitterWork* pWork) {
    const u32 count = pWork->mpAlivePtcl->getNum();
    ParticleShape* pShape = pWork->mpRes->getBsp();
    if (count < 2) {
        return;
    }

    f32 startTexCoord = 0.0f;
    f32 texCoord = 0.0f;
    f32 texStep = 1.0f / (count - 1.0f);
    f32 leftWidth = (1.0f + pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    f32 rightWidth = (1.0f - pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    f32 topWidth = (1.0f + pWork->mPivot.y) * (25.0f * pWork->mGlobalPtclScl.y);
    f32 bottomWidth = (1.0f - pWork->mPivot.y) * (25.0f * pWork->mGlobalPtclScl.y);
    ParticleSlot* pFirst;
    bool backward = false;
    if (pShape->isDrawFwdAhead()) {
        texCoord = 1.0f;
        startTexCoord = 1.0f;
        texStep = -texStep;
        pFirst = pWork->mpAlivePtcl->getLast();
        backward = true;
    } else {
        pFirst = pWork->mpAlivePtcl->getFirst();
    }

    setParticleMtx(pWork->mPosCamMtx);
    loadProjection(pWork, pWork->mPosCamMtx);
    Vec3f position;
    Vec3f direction;
    Vec3f side;
    Vec3f vertices[2];
    Mtx matrix;
    ParticleDraw::primitive(NEBULA_PRIMITIVE_TRIANGLE_STRIP);
    for (ParticleSlot* pNode = pFirst; pNode != pWork->mpAlivePtcl->getEnd();) {
        ParticleCore* pParticle = pNode->getObject();
        pWork->mpCurNode = pNode;
        ParticleDraw::particle(pParticle);
        position.set(pParticle->mPosition);
        f32 sin = Angle::sinShort(pParticle->mRotateAngle);
        f32 cos = Angle::cosShort(pParticle->mRotateAngle);
        vertices[0].set(-pParticle->mParticleScaleX * leftWidth, 0.0f, 0.0f);
        vertices[0].set< f32 >(vertices[0].x * cos, 0.0f, vertices[0].x * sin);
        vertices[1].set< f32 >(pParticle->mParticleScaleX * rightWidth, 0.0f, 0.0f);
        vertices[1].set< f32 >(vertices[1].x * cos, 0.0f, vertices[1].x * sin);
        particleDirection(pWork, pParticle, &direction);
        if (MR::isNearZero(direction)) {
            direction.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            MR::normalize(&direction);
        }

        NebulaVecCrossProduct(&pParticle->mBaseAxis, &direction, &side);
        if (MR::isNearZero(side)) {
            side.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&side);
        }

        if (MR::isSameDirection(direction, side, 0.01f)) {
            side.set< f32 >(0.0f, 1.0f, 0.0f);
        }

        NebulaVecCrossProduct(&direction, &side, &pParticle->mBaseAxis);
        MR::normalize(&pParticle->mBaseAxis);
        matrix[0][0] = side.x;
        matrix[0][1] = direction.x;
        matrix[0][2] = pParticle->mBaseAxis.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = side.y;
        matrix[1][1] = direction.y;
        matrix[1][2] = pParticle->mBaseAxis.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = side.z;
        matrix[2][1] = direction.z;
        matrix[2][2] = pParticle->mBaseAxis.z;
        matrix[2][3] = 0.0f;
        NebulaMtxMultVecArraySR(matrix, vertices, vertices, 2);
        ParticleDraw::corner(0, vertices[0].x + position.x, vertices[0].y + position.y, vertices[0].z + position.z, 0.0f, texCoord);
        ParticleDraw::corner(1, vertices[1].x + position.x, vertices[1].y + position.y, vertices[1].z + position.z, 1.0f, texCoord);
        pNode = backward ? pNode->getPrev() : pNode->getNext();
        texCoord += texStep;
    }

    texCoord = startTexCoord;
    ParticleDraw::primitive(NEBULA_PRIMITIVE_TRIANGLE_STRIP);
    for (ParticleSlot* pNode = pFirst; pNode != pWork->mpAlivePtcl->getEnd();) {
        ParticleCore* pParticle = pNode->getObject();
        pWork->mpCurNode = pNode;
        ParticleDraw::particle(pParticle);
        position.set(pParticle->mPosition);
        f32 sin = -Angle::sinShort(pParticle->mRotateAngle);
        f32 cos = Angle::cosShort(pParticle->mRotateAngle);
        vertices[0].set(-pParticle->mParticleScaleY * topWidth, 0.0f, 0.0f);
        vertices[0].set< f32 >(vertices[0].x * sin, 0.0f, vertices[0].x * cos);
        vertices[1].set< f32 >(pParticle->mParticleScaleY * bottomWidth, 0.0f, 0.0f);
        vertices[1].set< f32 >(vertices[1].x * sin, 0.0f, vertices[1].x * cos);
        particleDirection(pWork, pParticle, &direction);
        if (MR::isNearZero(direction)) {
            direction.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            MR::normalize(&direction);
        }

        NebulaVecCrossProduct(&pParticle->mBaseAxis, &direction, &side);
        if (MR::isNearZero(side)) {
            side.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&side);
        }

        if (MR::isSameDirection(direction, side, 0.01f)) {
            side.set< f32 >(0.0f, 1.0f, 0.0f);
        }

        NebulaVecCrossProduct(&direction, &side, &pParticle->mBaseAxis);
        MR::normalize(&pParticle->mBaseAxis);
        matrix[0][0] = side.x;
        matrix[0][1] = direction.x;
        matrix[0][2] = pParticle->mBaseAxis.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = side.y;
        matrix[1][1] = direction.y;
        matrix[1][2] = pParticle->mBaseAxis.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = side.z;
        matrix[2][1] = direction.z;
        matrix[2][2] = pParticle->mBaseAxis.z;
        matrix[2][3] = 0.0f;
        NebulaMtxMultVecArraySR(matrix, vertices, vertices, 2);
        ParticleDraw::corner(2, vertices[0].x + position.x, vertices[0].y + position.y, vertices[0].z + position.z, 0.0f, texCoord);
        ParticleDraw::corner(3, vertices[1].x + position.x, vertices[1].y + position.y, vertices[1].z + position.z, 1.0f, texCoord);
        pNode = backward ? pNode->getPrev() : pNode->getNext();
        texCoord += texStep;
    }
}
