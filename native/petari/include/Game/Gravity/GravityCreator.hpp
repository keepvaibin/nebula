#pragma once

#include "Game/Gravity/ConeGravity.hpp"
#include "Game/Gravity/CubeGravity.hpp"
#include "Game/Gravity/DiskGravity.hpp"
#include "Game/Gravity/DiskTorusGravity.hpp"
#include "Game/Gravity/ParallelGravity.hpp"
#include "Game/Gravity/PointGravity.hpp"
#include "Game/Gravity/SegmentGravity.hpp"
#include "Game/Gravity/WireGravity.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util/Table.hpp"

class GravityCreator {
public:
    GravityCreator();

    virtual PlanetGravity* getGravity() = 0;
    virtual PlanetGravity* createInstance() = 0;
    virtual void settingFromSRT(const Vec3f& /*rTrans*/, const Vec3f& /*rRotate*/, const Vec3f& /*rScale*/) {
    }
    // The placement's Obj_arg0 to Obj_arg2.
    virtual void settingFromPlacementArgs(s32 /*objArg0*/, s32 /*objArg1*/, s32 /*objArg2*/) {
    }
    virtual void settingFromPlacementOtherParam(const TableIter& /*rIter*/) {
    }

    PlanetGravity* createFromPlacement(const TableIter& rIter);
};

class CubeGravityCreator : public GravityCreator {
public:
    inline CubeGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 xFaces, s32 yFaces, s32 zFaces) override;

    CubeGravity* mGravityInstance;
};

class DiskGravityCreator : public GravityCreator {
public:
    inline DiskGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 bothSides, s32 edgeGravity, s32 validDegree) override;

    DiskGravity* mGravityInstance;
};

class DiskTorusGravityCreator : public GravityCreator {
public:
    inline DiskTorusGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 bothSides, s32 edgeType, s32 diskRadius) override;

    DiskTorusGravity* mGravityInstance;
};

class ConeGravityCreator : public GravityCreator {
public:
    inline ConeGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 enableBottom, s32 topCutPermille, s32) override;

    ConeGravity* mGravityInstance;
};

class PlaneGravityCreator : public GravityCreator {
public:
    inline PlaneGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;

    ParallelGravity* mGravityInstance;
};

class PlaneInBoxGravityCreator : public GravityCreator {
public:
    inline PlaneInBoxGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 baseDistance, s32 distanceAxis, s32) override;

    ParallelGravity* mGravityInstance;
};

class PlaneInCylinderGravityCreator : public GravityCreator {
public:
    inline PlaneInCylinderGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 baseDistance, s32, s32) override;

    ParallelGravity* mGravityInstance;
};

class PointGravityCreator : public GravityCreator {
public:
    inline PointGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;

    PointGravity* mGravityInstance;
};

class SegmentGravityCreator : public GravityCreator {
public:
    inline SegmentGravityCreator() : GravityCreator(), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) override;
    virtual void settingFromPlacementArgs(s32 validEdges, s32 validSideDegree, s32) override;

    SegmentGravity* mGravityInstance;
};

class WireGravityCreator : public GravityCreator {
public:
    inline WireGravityCreator() : GravityCreator(), mRailRider(nullptr), mGravityInstance(nullptr) {
    }

    virtual PlanetGravity* getGravity() override;
    virtual PlanetGravity* createInstance() override;
    virtual void settingFromPlacementOtherParam(const TableIter& rIter) override;

    RailRider* mRailRider;
    WireGravity* mGravityInstance;
};
