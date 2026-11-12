#include "Game/MapObj/BeamGoRoundPlanet.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/VolumeModelDrawer.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // One per joint beam1..beam16.
    const s32 sBeamNum = 16;
};  // namespace

BeamGoRoundBeam::BeamGoRoundBeam(MtxPtr pMtx) : LiveActor("ビームゴーラウンドビーム") {
    lumacore::setClass(this, lumacore::kClassId< BeamGoRoundBeam >);
    mModelDrawer = nullptr;
    mBloomModel = nullptr;
    mBeamJointMtx = pMtx;
}

void BeamGoRoundBeam::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("BeamGoRoundBeam", nullptr, false);
    mPosition.set< f32 >(mBeamJointMtx[0][3], mBeamJointMtx[1][3], mBeamJointMtx[2][3]);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_IndirectMapObj, MR::DrawType_VolumeModel);
    initHitSensor(1);
    MR::addHitSensorCallbackEnemyAttack(this, "beam", 4, 100.0f);
    initEffectKeeper(0, nullptr);
    mModelDrawer = new VolumeModelDrawer("ビームボリューム", "BeamGoRoundBeamVolume", getBaseMtx());
    mBloomModel = MR::createModelObjBloomModel("ビームブルーム", "BeamGoRoundBeamBloom", getBaseMtx());
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    MR::startAllAnim(this, "BeamGoRoundBeam");
    makeActorAppeared();
}

void BeamGoRoundBeam::calcAndSetBaseMtx() {
    mPosition.set< f32 >(mBeamJointMtx[0][3], mBeamJointMtx[1][3], mBeamJointMtx[2][3]);
    MR::setBaseTRMtx(this, mBeamJointMtx);
}

void BeamGoRoundBeam::draw() const {
    mModelDrawer->draw();
}

void BeamGoRoundBeam::updateHitSensor(HitSensor* pSensor) {
    f32 radius = pSensor->mRadius;
    Vec3f up;
    MR::calcUpVec(&up, this);
    Vec3f lineStart;
    lineStart.scaleAdd(radius, up, mPosition);
    Vec3f lineEnd;
    lineEnd.scaleAdd(2700.0f - radius, up, lineStart);
    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), lineStart, lineEnd);
}

void BeamGoRoundBeam::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isSensorEnemyAttack(pSender)) {
            MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
        }
    }
}

namespace NrvBeamGoRoundPlanet {
    NEW_NERVE(BeamGoRoundPlanetNrvWait, BeamGoRoundPlanet, Wait);
};  // namespace NrvBeamGoRoundPlanet

BeamGoRoundPlanet::BeamGoRoundPlanet(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< BeamGoRoundPlanet >);
    mBeams = nullptr;
    mMove1Collision = nullptr;
    mMove2Collision = nullptr;
}

void BeamGoRoundPlanet::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupFarClipping(-1.0f);
    info.setupNerve(GET_NERVE(BeamGoRoundPlanet, BeamGoRoundPlanetNrvWait));
    initialize(rIter, info);
    mMove1Collision = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("body"), MR::getJointMtx(this, "move1"), MR::CollisionScaleType_Default);
    mMove2Collision = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("body"), MR::getJointMtx(this, "move2"), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mMove2Collision);
    MR::validateCollisionParts(mMove1Collision);
    initBeam();
}

void BeamGoRoundPlanet::exeWait() {
    mMove2Collision->setMtx();
    mMove1Collision->setMtx();
    MR::startLevelSound(this, "SE_OJ_LV_BEAM_GO_ROUND");
}

void BeamGoRoundPlanet::connectToScene(const MapObjActorInitInfo&) {
    MR::connectToScenePlanet(this);
}

void BeamGoRoundPlanet::initBeam() {
    mBeams = new BeamGoRoundBeam*[::sBeamNum];

    for (s32 i = 0; i < ::sBeamNum; i++) {
        MtxPtr jointMtx = MR::getJointMtx(this, MR::FormattedName("beam%d", i + 1).c_str());
        mBeams[i] = new BeamGoRoundBeam(jointMtx);
        mBeams[i]->initWithoutIter();
    }
}
