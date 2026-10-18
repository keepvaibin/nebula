#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraBehind.hpp"
#include "Game/Camera/CameraBlackHole.hpp"
#include "Game/Camera/CameraCharmedFix.hpp"
#include "Game/Camera/CameraCharmedTripodBoss.hpp"
#include "Game/Camera/CameraCharmedVecReg.hpp"
#include "Game/Camera/CameraCharmedVecRegTower.hpp"
#include "Game/Camera/CameraCubePlanet.hpp"
#include "Game/Camera/CameraDPD.hpp"
#include "Game/Camera/CameraDead.hpp"
#include "Game/Camera/CameraFix.hpp"
#include "Game/Camera/CameraFixedPoint.hpp"
#include "Game/Camera/CameraFixedThere.hpp"
#include "Game/Camera/CameraFollow.hpp"
#include "Game/Camera/CameraFooFighter.hpp"
#include "Game/Camera/CameraFooFighterPlanet.hpp"
#include "Game/Camera/CameraFrontAndBack.hpp"
#include "Game/Camera/CameraGround.hpp"
#include "Game/Camera/CameraInnerCylinder.hpp"
#include "Game/Camera/CameraInwardSphere.hpp"
#include "Game/Camera/CameraInwardTower.hpp"
#include "Game/Camera/CameraMedianPlanet.hpp"
#include "Game/Camera/CameraMedianTower.hpp"
#include "Game/Camera/CameraMtxRegParallel.hpp"
#include "Game/Camera/CameraObjParallel.hpp"
#include "Game/Camera/CameraParallel.hpp"
#include "Game/Camera/CameraRaceFollow.hpp"
#include "Game/Camera/CameraRailDemo.hpp"
#include "Game/Camera/CameraRailFollow.hpp"
#include "Game/Camera/CameraRailWatch.hpp"
#include "Game/Camera/CameraSlide.hpp"
#include "Game/Camera/CameraSpiral.hpp"
#include "Game/Camera/CameraSubjective.hpp"
#include "Game/Camera/CameraTalk.hpp"
#include "Game/Camera/CameraTower.hpp"
#include "Game/Camera/CameraTowerPos.hpp"
#include "Game/Camera/CameraTripodBoss.hpp"
#include "Game/Camera/CameraTripodBossJoint.hpp"
#include "Game/Camera/CameraTripodPlanet.hpp"
#include "Game/Camera/CameraTrundle.hpp"
#include "Game/Camera/CameraTwistedPassage.hpp"
#include "Game/Camera/CameraWaterFollow.hpp"
#include "Game/Camera/CameraWaterPlanet.hpp"
#include "Game/Camera/CameraWaterPlanetBoss.hpp"
#include "Game/Camera/CameraWonderPlanet.hpp"
#include <cstring>

struct CameraTableEntry {
    /* 0x00 */ const char* mName;
    /* 0x04 */ const char* mExplain;
    /* 0x08 */ Camera* (*mCreateFunc)(void);
    /* 0x0C */ bool mIsPublic;
};

namespace {
    template < typename T >
    Camera* createCamera() {
        return new T();
    }

    static const CameraTableEntry sCameraTable[] = {
        {"CAM_TYPE_XZ_PARA", "\225\300\215\163" /* 並行 */, createCamera< CameraParallel >, true},
        {"CAM_TYPE_TOWER", "\223\203" /* 塔 */, createCamera< CameraTower >, true},
        {"CAM_TYPE_FOLLOW", "\203\164\203\110\203\215\201\133" /* フォロー */, createCamera< CameraFollow >, true},
        {"CAM_TYPE_WONDER_PLANET", "\203\166\203\211\203\154\203\142\203\147" /* プラネット */, createCamera< CameraWonderPlanet >, true},
        {"CAM_TYPE_POINT_FIX", "\212\256\221\123\214\305\222\350" /* 完全固定 */, createCamera< CameraFix >, true},
        {"CAM_TYPE_EYEPOS_FIX", "\222\350\223\137" /* 定点 */, createCamera< CameraFixedPoint >, true},
        {"CAM_TYPE_SLIDER", "\203\130\203\211\203\103\203\137\201\133" /* スライダー */, createCamera< CameraBehind >, true},
        {"CAM_TYPE_INWARD_TOWER", "\223\203\223\340\225\224" /* 塔内部 */, createCamera< CameraInwardTower >, true},
        {"CAM_TYPE_EYEPOS_FIX_THERE", "\202\273\202\314\217\352\222\350\223\137" /* その場定点 */, createCamera< CameraFixedThere >, true},
        {"CAM_TYPE_TRIPOD_BOSS", "\216\117\213\162\203\173\203\130" /* 三脚ボス */, createCamera< CameraTripodBoss >, true},
        {"CAM_TYPE_TOWER_POS", "\223\203\201\151\203\124\203\165\203\136\201\133\203\121\203\142\203\147\225\164\202\253\201\152" /* 塔（サブターゲット付き） */, createCamera< CameraTowerPos >, true},
        {"CAM_TYPE_TRIPOD_PLANET", "\216\117\213\162\230\146\220\257" /* 三脚惑星 */, createCamera< CameraTripodPlanet >, true},
        {"CAM_TYPE_DEAD", "\222\312\217\355\216\200\226\123" /* 通常死亡 */, createCamera< CameraDead >, true},
        {"CAM_TYPE_INWARD_SPHERE", "\213\205\223\340\225\224" /* 球内部 */, createCamera< CameraInwardSphere >, true},
        {"CAM_TYPE_RAIL_DEMO", "\203\214\201\133\203\213\203\146\203\202" /* レールデモ */, createCamera< CameraRailDemo >, true},
        {"CAM_TYPE_RAIL_FOLLOW", "\203\214\201\133\203\213\203\164\203\110\203\215\201\133" /* レールフォロー */, createCamera< CameraRailFollow >, true},
        {"CAM_TYPE_TRIPOD_BOSS_JOINT", "\216\117\213\162\203\173\203\130\203\127\203\207\203\103\203\223\203\147" /* 三脚ボスジョイント */, createCamera< CameraTripodBossJoint >, true},
        {"CAM_TYPE_CHARMED_TRIPOD_BOSS", "\216\117\213\162\203\173\203\130\203\127\203\207\203\103\203\223\203\147\222\215\216\213" /* 三脚ボスジョイント注視 */, createCamera< CameraCharmedTripodBoss >, true},
        {"CAM_TYPE_OBJ_PARALLEL", "\203\111\203\165\203\127\203\106\225\300\215\163" /* オブジェ並行 */, createCamera< CameraObjParallel >, true},
        {"CAM_TYPE_CHARMED_FIX", "\203\124\203\223\203\173" /* サンボ */, createCamera< CameraCharmedFix >, true},
        {"CAM_TYPE_GROUND", "\222\156\226\312" /* 地面 */, createCamera< CameraGround >, true},
        {"CAM_TYPE_TRUNDLE", "\203\147\203\211\203\223\203\150\203\213" /* トランドル */, createCamera< CameraTrundle >, true},
        {"CAM_TYPE_CUBE_PLANET", "\203\114\203\205\201\133\203\165\230\146\220\257" /* キューブ惑星 */, createCamera< CameraCubePlanet >, true},
        {"CAM_TYPE_INNER_CYLINDER", "\211\176\223\233\223\340\225\224" /* 円筒内部 */, createCamera< CameraInnerCylinder >, true},
        {"CAM_TYPE_SPIRAL_DEMO", "\227\206\220\371\203\146\203\202" /* 螺旋デモ */, createCamera< CameraSpiral >, true},
        {"CAM_TYPE_TALK", "\211\357\230\142" /* 会話 */, createCamera< CameraTalk >, true},
        {"CAM_TYPE_MTXREG_PARALLEL", "\203\175\203\147\203\212\203\116\203\130\203\214\203\127\203\130\203\136\225\300\215\163" /* マトリクスレジスタ並行 */, createCamera< CameraMtxRegParallel >, true},
        {"CAM_TYPE_CHARMED_VECREG", "\203\170\203\116\203\147\203\213\203\214\203\127\203\130\203\136\222\215\226\332" /* ベクトルレジスタ注目 */, createCamera< CameraCharmedVecReg >, true},
        {"CAM_TYPE_MEDIAN_PLANET", "\222\206\223\137\222\215\226\332\203\166\203\211\203\154\203\142\203\147" /* 中点注目プラネット */, createCamera< CameraMedianPlanet >, true},
        {"CAM_TYPE_TWISTED_PASSAGE", "\202\313\202\266\202\352\211\361\230\114" /* ねじれ回廊 */, createCamera< CameraTwistedPassage >, true},
        {"CAM_TYPE_MEDIAN_TOWER", "\222\206\223\137\223\203\203\112\203\201\203\211" /* 中点塔カメラ */, createCamera< CameraMedianTower >, true},
        {"CAM_TYPE_CHARMED_VECREG_TOWER", "VecReg\212\160\223\170\225\342\220\263\223\203\203\112\203\201\203\211" /* VecReg角度補正塔カメラ */, createCamera< CameraCharmedVecRegTower >, true},
        {"CAM_TYPE_FRONT_AND_BACK", "\225\134\227\240\203\112\203\201\203\211" /* 表裏カメラ */, createCamera< CameraFrontAndBack >, true},
        {"CAM_TYPE_RACE_FOLLOW", "\203\214\201\133\203\130\227\160\203\164\203\110\203\215\201\133" /* レース用フォロー */, createCamera< CameraRaceFollow >, true},
        {"CAM_TYPE_2D_SLIDE", "\202\121\202\143\203\130\203\211\203\103\203\150" /* ２Ｄスライド */, createCamera< CameraSlide >, true},
        {"CAM_TYPE_FOO_FIGHTER", "\203\164\201\133\203\164\203\100\203\103\203\136\201\133" /* フーファイター */, createCamera< CameraFooFighter >, true},
        {"CAM_TYPE_FOO_FIGHTER_PLANET", "\203\164\201\133\203\164\203\100\203\103\203\136\201\133\203\166\203\211\203\154\203\142\203\147" /* フーファイタープラネット */, createCamera< CameraFooFighterPlanet >, true},
        {"CAM_TYPE_BLACK_HOLE", "\203\165\203\211\203\142\203\116\203\172\201\133\203\213" /* ブラックホール */, createCamera< CameraBlackHole >, false},
        {"CAM_TYPE_ANIM", "\203\101\203\152\203\201" /* アニメ */, createCamera< CameraAnim >, false},
        {"CAM_TYPE_DPD", "\202\143\202\157\202\143" /* ＤＰＤ */, createCamera< CameraDPD >, true},
        {"CAM_TYPE_WATER_FOLLOW", "\220\205\222\206\203\164\203\110\203\215\201\133" /* 水中フォロー */, createCamera< CameraWaterFollow >, true},
        {"CAM_TYPE_WATER_PLANET", "\220\205\222\206\203\166\203\211\203\154\203\142\203\147" /* 水中プラネット */, createCamera< CameraWaterPlanet >, true},
        {"CAM_TYPE_WATER_PLANET_BOSS", "\220\205\222\206\203\166\203\211\203\154\203\142\203\147\203\173\203\130" /* 水中プラネットボス */, createCamera< CameraWaterPlanetBoss >, true},
        {"CAM_TYPE_RAIL_WATCH", "\203\214\201\133\203\213\222\215\226\332" /* レール注目 */, createCamera< CameraRailWatch >, true},
        {"CAM_TYPE_SUBJECTIVE", "\216\345\212\317" /* 主観 */, createCamera< CameraSubjective >, true},
    };
    static const char* sDefaultCamera = "CAM_TYPE_XZ_PARA";
};  // namespace

CameraHolder::CameraHolder(const char* pName) : NameObj(pName) {
    createCameras();

    mDefaultCameraIndex = getIndexOf(getNameStrOfDefault());
    mDefaultCamera = getDefaultCamera();
}

s32 CameraHolder::getNum() const {
    return ARRAY_SIZE(::sCameraTable);
}

CamTranslatorBase* CameraHolder::getTranslator(s32 index) {
    return mTranslators[index];
}

s32 CameraHolder::getIndexOf(const char* pName) const {
    for (s32 i = 0; i < getNum(); i++) {
        if (strcmp(pName, getNameStrOf(i)) == 0) {
            return i;
        }
    }

    return -1;
}

const char* CameraHolder::getNameStrOf(s32 index) const {
    return ::sCameraTable[index].mName;
}

const char* CameraHolder::getExplainStrOf(s32 index) const {
    return ::sCameraTable[index].mExplain;
}

bool CameraHolder::isPublic(s32 index) const {
    return ::sCameraTable[index].mIsPublic;
}

Camera* CameraHolder::getDefaultCamera() {
    return mCameras[mDefaultCameraIndex];
}

s32 CameraHolder::getIndexOfDefault() const {
    return mDefaultCameraIndex;
}

const char* CameraHolder::getNameStrOfDefault() const {
    return ::sDefaultCamera;
}

s32 CameraHolder::getIndexOf(Camera* pCamera) const {
    for (s32 i = 0; i < getNum(); i++) {
        if (mCameras[i] == pCamera) {
            return i;
        }
    }

    return -1;
}

void CameraHolder::createCameras() {
    mCameras = new Camera*[getNum()];
    mTranslators = new CamTranslatorBase*[getNum()];

    for (s32 i = 0; i < getNum(); i++) {
        mCameras[i] = ::sCameraTable[i].mCreateFunc();
        mTranslators[i] = mCameras[i]->createTranslator();
    }
}

Camera* CameraHolder::getCameraInner(s32 index) const {
    return mCameras[index];
}
