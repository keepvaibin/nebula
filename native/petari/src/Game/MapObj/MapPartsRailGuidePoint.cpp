#include "Game/MapObj/MapPartsRailGuidePoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"

namespace {
    static const f32 sShadowRadius = 20.0f;
};  //  namespace

MapPartsRailGuidePoint::MapPartsRailGuidePoint(const LiveActor* pActor, const char* pName, f32 a3, bool a4) : LiveActor("レイル点") {
    mCoord = a3;
    mIsPointModel = a4;
    initModelManagerWithAnm(pName, 0, false);
    MR::calcRailPosAtCoord(&mPosition, pActor, mCoord);
}

MapPartsRailGuidePoint::MapPartsRailGuidePoint(const LiveActor* pActor, const char* pName, int a3, bool a4) : LiveActor("レイル点") {
    mIsPointModel = a4;
    mCoord = 0.0f;
    initModelManagerWithAnm(pName, 0, false);
    MR::calcRailPointPos(&mPosition, pActor, a3);
    mCoord = MR::getRailPointCoord(pActor, a3);
}

void MapPartsRailGuidePoint::init(const TableIter& rIter) {
    MR::connectToSceneMapObjNoMovement(this);

    if (mIsPointModel) {
        MR::initShadowVolumeSphere(this, ::sShadowRadius);
        MR::setShadowDropLength(this, 0, 5000.0f);
        MR::onCalcShadowOneTime(this, 0);
    }

    kill();
}

MapPartsRailGuidePoint::~MapPartsRailGuidePoint() {
}