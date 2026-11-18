// SPDX-License-Identifier: GPL-3.0-only
// Binder (Game/LiveActor/Binder.cpp, P-166): a bind keeps every plane the map reports, past the binder's starting room
// (Mario's is 8; the console kept the first 8 and dropped the rest, so its slide pass pushed with none of them). The
// owner keeps the port's behaviour; this test holds it. The map's strike list and the triangle's accessors are the
// test's own; the binder and the arenas are the game's.
#include "Game/LiveActor/Binder.hpp"
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"

#include <stdint.h>
#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    // What the map's last strike check found.
    HitInfo sHits[64];
    u32 sHitCount = 0;

    // `count` planes, each its own depth (1, 2, ...) and the floor's normal.
    void strike(u32 count) {
        sHitCount = count;
        for (u32 i = 0; i < count; i++) {
            sHits[i] = HitInfo();
            sHits[i].mParentTriangle.mIdx = i;
            sHits[i].mParentTriangle.mNormals[0].set(0.0f, 1.0f, 0.0f);
            sHits[i].mDepth = static_cast< f32 >(i + 1);
        }
    }
}  // namespace

// The map, as the strike check leaves it.
namespace Collision {
    s32 checkStrikeBallToMap(const Vec3f&, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*) {
        return static_cast< s32 >(sHitCount);
    }
    s32 checkStrikeBallToMapWithMovingReaction(const Vec3f&, f32, const CollisionPartsFilterBase*, const TriangleFilterBase*) {
        return static_cast< s32 >(sHitCount);
    }
    const HitInfo* getStrikeInfoMap(u32 index) {
        return &sHits[index];
    }
    u32 getStrikeInfoNumMap() {
        return sHitCount;
    }
}  // namespace Collision

// A plane's triangle, as Game/Map/HitInfo.cpp keeps it (without the collision parts behind it).
Triangle::Triangle() : mParts(nullptr), mIdx(0xFFFFFFFF), mSensor(nullptr) {
    for (Vec3f& normal : mNormals) {
        normal.zero();
    }
    for (Vec3f& pos : mPos) {
        pos.zero();
    }
}
const Vec3f* Triangle::getNormal(int index) const {
    return &mNormals[index];
}
bool Triangle::isHostMoved() const {
    return false;
}
void Triangle::calcForceMovePower(Vec3f*, const Vec3f&) const {}
HitInfo::HitInfo() : mParentTriangle(), mDepth(0.0f), mHitPos(0, 0, 0), mNormalMove(0, 0, 0) {
    mHitPart = 0;
}

// What the bind's other passes call; this test runs none of them.
void CollisionCategorizedKeeper::addToGlobal(CollisionParts*) {}
void CollisionCategorizedKeeper::removeFromGlobal(CollisionParts*) {}
namespace MR {
    CollisionDirector* getCollisionDirector() {
        return nullptr;
    }
    bool normalizeOrZero(Vec3f* pVec) {
        const f32 length = pVec->length();
        if (length == 0.0f) {
            return true;
        }
        pVec->scale(1.0f / length);
        return false;
    }
    bool isNearZero(const Vec3f& rVec, f32 tolerance) {
        return rVec.x <= tolerance && rVec.x >= -tolerance && rVec.y <= tolerance && rVec.y >= -tolerance &&
               rVec.z <= tolerance && rVec.z >= -tolerance;
    }
    bool isFloorPolygon(const Vec3f&, const Vec3f&) {
        return false;
    }
    bool isWallPolygon(const Vec3f&, const Vec3f&) {
        return false;
    }
}  // namespace MR

// What NebulaArena::destroy tells; no arena is destroyed here.
namespace lumacore {
    void released(const void*, const void*) {}
}  // namespace lumacore
namespace luma {
    namespace content {
        namespace fx {
            void released(const void*, const void*) {}
        }  // namespace fx
        namespace model {
            void released(const void*, const void*) {}
        }  // namespace model
    }  // namespace content
    namespace draw {
        void released(const void*, const void*) {}
        namespace inst {
            void released(const void*, const void*) {}
        }  // namespace inst
        namespace view {
            void released(const void*, const void*) {}
        }  // namespace view
    }  // namespace draw
    namespace ui {
        void released(const void*, const void*) {}
    }  // namespace ui
    namespace anim {
        void released(const void*, const void*) {}
    }  // namespace anim
    namespace player {
        void released(const void*, const void*) {}
    }  // namespace player
}  // namespace luma
namespace RenderTexture {
    void forget(uintptr_t, uintptr_t) {}
}  // namespace RenderTexture
namespace GameAssets {
    void unloadArena(const NebulaArena*) {}
}  // namespace GameAssets

int main() {
    NebulaArena::create(nullptr);
    const Vec3f position(0.0f, 0.0f, 0.0f);
    const Vec3f gravity(0.0f, -1.0f, 0.0f);

    // Mario's binder (MarioActor.cpp initBinder(60.0f, 0.0f, 8)) meets 20 planes in one strike check.
    Binder mario(nullptr, &position, &gravity, 60.0f, 0.0f, 8);
    strike(20);
    expect(mario.storeCurrentHitInfo(true) == 20, "the bind takes all 20 planes of one check");
    expect(mario.mPlaneNum == 20, "Mario's bind keeps 20 planes, not the first 8");
    expect(mario.mPlaneCapacity >= 20, "the binder's list grew to hold them");
    bool inOrder = true;
    for (u32 i = 0; i < 20; i++) {
        inOrder = inOrder && mario.getPlane(static_cast< int >(i))->mParentTriangle.mIdx == i &&
                  mario.getPlane(static_cast< int >(i))->mDepth == static_cast< f32 >(i + 1);
    }
    expect(inOrder, "the planes are kept in the order the map reports them, with their depths");

    // The slide pass's planes come after the first pass's; the console had no room left for them.
    strike(12);
    expect(mario.storeCurrentHitInfo(true) == 12, "the slide pass's check adds its 12 planes");
    expect(mario.mPlaneNum == 32, "the bind holds both passes' planes");
    expect(mario.getPlane(19)->mDepth == 20.0f && mario.getPlane(20)->mDepth == 1.0f, "the first pass's planes stay");
    Vec3f reaction;
    mario.obtainMomentFixReaction(&reaction, 20);
    expect(reaction.x == 0.0f && reaction.y == 12.0f && reaction.z == 0.0f,
           "the slide pass pushes with its own planes (the deepest, 12, up the floor's normal)");

    // A pass's planes get the overlap margin unless the bind stops at the first hit.
    mario.clear();
    strike(9);
    expect(mario.storeCurrentHitInfo(false) == 9 && mario.mPlaneNum == 9, "after clear, a bind of 9 keeps 9");
    expect(mario.getPlane(8)->mDepth > 9.0f, "the ninth plane carries the overlap margin");

    // A binder with no list of its own (planeCapacity 0) keeps every plane in the shared list.
    Binder shared(nullptr, &position, &gravity, 30.0f, 0.0f, 0);
    strike(40);
    expect(shared.storeCurrentHitInfo(true) == 40 && shared.mPlaneNum == 40, "a binder without a list keeps 40 planes");
    expect(shared.planes()[39].mDepth == 40.0f, "the shared list holds the 40th plane");

    if (sFailures != 0) {
        return 1;
    }
    printf("binder_tests: all passed\n");
    return 0;
}
