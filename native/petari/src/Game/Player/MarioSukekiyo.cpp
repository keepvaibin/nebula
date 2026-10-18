#include "Game/Player/MarioSukekiyo.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/MtxUtil.hpp"

MarioSukekiyo::MarioSukekiyo(MarioActor* pActor) : MarioState(pActor, MarioStatus_Sukekiyo) {
    _44 = new Triangle();
    _14.zero();
    _20.zero();
    _2C.zero();
    _38.zero();
    _48 = 0;
    _4A = 0;
}

MarioBury::MarioBury(MarioActor* pActor) : MarioSukekiyo(pActor) {
    mStatusId = MarioStatus_Bury;
}

bool MarioSukekiyo::notice() {
    return false;
}

bool MarioSukekiyo::postureCtrl(MtxPtr pMtx) {
    MR::makeMtxUpSide(reinterpret_cast< TPos3f* >(pMtx), _14, _20);
    return true;
}

bool MarioSukekiyo::start() {
    playSound("\203\130\203\120\203\114\203\210\212\112\216\156" /* スケキヨ開始 */);
    Mario* player = getPlayer();
    playEffectRT("\221\256\220\253\220\113\203\150\203\215\203\142\203\166" /* 属性尻ドロップ */, player->_368, getTrans());
    startPadVib("\215\305\213\255" /* 最強 */);
    startCamVib(3);
    mActor->_F44 = 0;
    _14 = getPlayer()->_368;
    _20 = getPlayer()->mSideVec;
    getPlayer()->forceSetHeadVecKeepSide(_14);
    _48 = 0;
    _4A = 0;
    getPlayer()->stopJump();
    getPlayer()->stopWalk();

    if (mStatusId == MarioStatus_Sukekiyo) {
        changeAnimation("\203\130\203\120\203\114\203\210" /* スケキヨ */);
    } else {
        playSound("\220\272\221\253\226\204\202\334\202\350\212\112\216\156" /* 声足埋まり開始 */);
        changeAnimation("\226\204\202\334\202\350" /* 埋まり */);
    }

    return true;
}

bool MarioSukekiyo::update() {
    if (!getPlayer()->isCurrentFloorSand()) {
        return false;
    }

    if (_4A) {
        return isAnimationRun(nullptr) != false;
    }

    if (mActor->isRequestRush()) {
        _4A = 1;
    }

    if (checkTrgA()) {
        _4A = 1;
    }

    if (_4A) {
        if (mStatusId == MarioStatus_Sukekiyo) {
            changeAnimation("\203\130\203\120\203\114\203\210\222\105\217\157" /* スケキヨ脱出 */, "\212\356\226\173" /* 基本 */);
            playSound("\220\272\203\130\203\120\203\114\203\210\217\111\227\271" /* 声スケキヨ終了 */);
        } else {
            changeAnimation("\226\204\202\334\202\350\222\105\217\157" /* 埋まり脱出 */, "\212\356\226\173" /* 基本 */);
            playSound("\220\272\221\253\226\204\202\334\202\350\217\111\227\271" /* 声足埋まり終了 */);
        }

        playSound("\203\130\203\120\203\114\203\210\217\111\227\271" /* スケキヨ終了 */);
    }

    return true;
}

bool MarioSukekiyo::close() {
    getPlayer()->stopWalk();
    mActor->_F44 = 1;
    stopAnimation(nullptr, "\212\356\226\173" /* 基本 */);
    return true;
}
