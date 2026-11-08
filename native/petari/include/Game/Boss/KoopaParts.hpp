#pragma once

#include <nebula_geometry.hpp>

class Koopa;
class TableIter;
class KoopaFireStairs;
class KoopaBattleMapStair;
class LiveActor;
class LiveActorGroup;
class KoopaBattleMapPlanet;
class KoopaPlanetShadow;
class KoopaSwitchKeeper;
class KoopaViewSwitchKeeper;
class KoopaPowerUpSwitch;
class KoopaRockBreak;
class PartsModel;
class ModelObjNpc;
class ModelObj;
class ActorCameraInfo;

class KoopaParts {
public:
    KoopaParts(Koopa* pKoopa, const TableIter& rIter);

    KoopaFireStairs* emitFireStairsToPos(const KoopaBattleMapStair* pMapStair, const Vec3f& rPosition, bool useFront);
    void killFireStairsAll();
    void emitFireShort(bool isFast, bool isCurve);
    void emitFireLongTime();
    void emitShockWave();
    Vec3f& getPlanetPos() const;
    f32 getPlanetRadius() const;
    void appearHoleSunPlanetInside();
    void appearHoleSunPlanetOutside();
    void killHoleSunPlanetOutside();
    void createPlanetShadow();
    void initVs1();
    void initVs2();
    void initVs3();
    void createRock();
    void createRollBall();
    void createCommonParts();
    void createFireStairs(bool isKoopaJr);

    Koopa* mKoopa;
    f32 mPlanetRadius;
    LiveActor* mThornBig;
    LiveActor* mThornSmall;
    LiveActor* mArmorBreak;
    LiveActor* mThornBreak;
    KoopaBattleMapPlanet* mPlanetLv1;
    KoopaPlanetShadow* mPlanetShadow;
    LiveActorGroup* mShockWave;
    LiveActorGroup* mFireShort;
    LiveActorGroup* mFireStairs;
    KoopaSwitchKeeper* mKoopaSwitchKeeper;
    KoopaViewSwitchKeeper* mKoopaViewSwitchKeeper;
    KoopaPowerUpSwitch* mKoopaPowerUpSwitch;
    PartsModel* mRock;
    KoopaRockBreak* mRockBreak;
    PartsModel* mRollBall;
    KoopaBattleMapPlanet* mPlanetLv2;
    KoopaBattleMapPlanet* mPlanetLv3;
    LiveActor* mHoleSunPlanetOutside;
    PartsModel* mHoleSunPlanetOutsideBloom;
    LiveActor* mHoleSunPlanetInside;
    PartsModel* mHoleSunPlanetInsideBloom;
    ModelObjNpc* mPeach;
    ModelObjNpc* mKoopaJr;
    ModelObjNpc* mKoopaJrShip;
    ModelObj* mMeteor1;
    ModelObj* mMeteor2;
    ModelObj* mMeteor3;
    ActorCameraInfo* mActorCameraInfo;
};
