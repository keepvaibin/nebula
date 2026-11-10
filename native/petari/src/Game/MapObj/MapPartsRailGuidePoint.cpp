#include "Game/MapObj/MapPartsRailGuidePoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 5000.0f);

namespace {
    NEBULA_TUNABLE(f32, sShadowRadius, 20.0f);
};  //  namespace

MapPartsRailGuidePoint::MapPartsRailGuidePoint(const LiveActor* pActor, const char* pName, f32 coord, bool isPointModel) : LiveActor("レイル点") {
    mCoord = coord;
    mIsPointModel = isPointModel;
    initModelManagerWithAnm(pName, 0, false);
    MR::calcRailPosAtCoord(&mPosition, pActor, mCoord);
}

MapPartsRailGuidePoint::MapPartsRailGuidePoint(const LiveActor* pActor, const char* pName, int pointIndex, bool isPointModel) : LiveActor("レイル点") {
    mIsPointModel = isPointModel;
    mCoord = 0.0f;
    initModelManagerWithAnm(pName, 0, false);
    MR::calcRailPointPos(&mPosition, pActor, pointIndex);
    mCoord = MR::getRailPointCoord(pActor, pointIndex);
}

void MapPartsRailGuidePoint::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjNoMovement(this);

    if (mIsPointModel) {
        MR::initShadowVolumeSphere(this, ::sShadowRadius);
        MR::setShadowDropLength(this, 0, cNum_init_f1);
        MR::onCalcShadowOneTime(this, 0);
    }

    kill();
}
