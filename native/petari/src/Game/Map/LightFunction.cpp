#include "Game/Render/Material.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/GameLightSignal.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/AreaObj/LightAreaHolder.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_loadLightInfoCoin_f1, "loadLightInfoCoin.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadLightInfoCoin_f2, "loadLightInfoCoin.f2", 2.0f);


void LightFunction::initLightRegisterAll() {
    Color8 c(255, 255, 255, 255);

    for (s32 i = 0; i < 8; i++) {
        NebulaLight obj = {};
        LightValues::color(&obj, c);
        LightValues::pos(&obj, 0.0f, 0.0f, 0.0f);
        LightValues::dir(&obj, 0.0f, -1.0f, 0.0f);
        LightValues::attn(&obj, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        LightValues::specularDir(&obj, 0.0f, -1.0f, 0.0f);
        LightValues::specularDirHA(&obj, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f);
        LightValues::attn(&obj, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
        Frame::setLight(i, obj);
    }
}

void LightFunction::initLightData() {
    MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->initData();
}

ResourceHolder* LightFunction::loadLightArchive() {
    return MR::createAndAddResourceHolder("LightData");
}

s32 LightFunction::createLightDataParser(Table** pOut) {
    ResourceHolder* holder = MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mResourceHolder;
    Table* info = MR::tryCreateCsvParser(holder, "LightData.bcsv");
    *pOut = info;

    // A light archive with no LightData table holds no lights.
    if (info == nullptr) {
        return 0;
    }

    return info->getNumEntries();
}

s32 LightFunction::createZoneDataParser(const char* pZone, Table** pOut) {
    ResourceHolder* holder = MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mResourceHolder;
    Table* info = MR::tryCreateCsvParser(holder, "Light%s.bcsv", pZone);
    *pOut = info;

    if (info != nullptr) {
        return info->getNumEntries();
    }

    return 0;
}

namespace {
    void getDataLightInfo(Table* pInfo, int id, LightInfo* pLightInfo, const char* pName) {
        MR::FormattedName colorBuf("%sColor", pName);
        MR::getCsvDataColor(&pLightInfo->mColor, pInfo, colorBuf.c_str(), id);
        MR::FormattedName posBuf("%sPos", pName);
        MR::getCsvDataVec(&pLightInfo->mPos, pInfo, posBuf.c_str(), id);
        MR::FormattedName camBuf("%sFollowCamera", pName);
        MR::getCsvDataBool(&pLightInfo->mIsFollowCamera, pInfo, camBuf.c_str(), id);
    }

    void getDataActorLightInfo(Table* pInfo, int lightID, ActorLightInfo* pActorInfo, const char* pName) {
        MR::FormattedName lightBuf("%sLight%d", pName, 0);
        getDataLightInfo(pInfo, lightID, &pActorInfo->mInfo0, lightBuf.c_str());
        lightBuf.format("%sLight%d", pName, 1);
        getDataLightInfo(pInfo, lightID, &pActorInfo->mInfo1, lightBuf.c_str());
        MR::FormattedName alphaBuf("%sAlpha2", pName);
        MR::getCsvDataU8(&pActorInfo->mAlpha2, pInfo, alphaBuf.c_str(), lightID);
        MR::FormattedName ambBuf("%sAmbient", pName);
        MR::getCsvDataColor(&pActorInfo->mColor, pInfo, ambBuf.c_str(), lightID);
    }
};  // namespace

void LightFunction::getAreaLightLightData(Table* pInfo, int idx, AreaLightInfo* pLightInfo) {
    MR::getCsvDataStr(&pLightInfo->mAreaLightName, pInfo, "AreaLightName", idx);
    MR::getCsvDataS32(&pLightInfo->mInterpolate, pInfo, "Interpolate", idx);
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mPlayerLight, "Player");
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mStrongLight, "Strong");
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mWeakLight, "Weak");
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mPlanetLight, "Planet");
}

const char* LightFunction::getDefaultAreaLightName() {
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mDataHolder->getDefaultAreaLightName();
}

s32 LightFunction::getDefaultStepInterpolate() {
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mDataHolder->getDefaultStepInterpolate();
}

void LightFunction::registerPlayerLightCtrl(const ActorLightCtrl* pCtrl) {
    MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mPlayerLightCtrl = pCtrl;
}

void LightFunction::registerLightAreaHolder(LightAreaHolder* pHolder) {
    MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mAreaHolder = pHolder;
}

bool LightFunction::tryFindNewAreaLightID(const Vec3f& rPos, ZoneLightID* pId) {
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mAreaHolder->tryFindLightID(rPos, pId);
}

AreaLightInfo* LightFunction::getAreaLightInfo(const ZoneLightID& rId) {
    const char* name = MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mZoneDataHolder->getAreaLightNameInZoneData(rId);
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mDataHolder->findAreaLight(name);
}

namespace {
    void blendActorLightPos(const LightInfo& rFrom, const LightInfo& rTo, LightInfo* pResult, f32 rate) {
        if (pResult->mIsFollowCamera) {
            if (rFrom.mIsFollowCamera) {
                MR::blendVec(&pResult->mPos, rFrom.mPos, rTo.mPos, rate);
            } else {
                Vec3f pos(rFrom.mPos);
                MR::getCameraViewMtx().mult(pos, pos);
                MR::blendVec(&pResult->mPos, pos, rTo.mPos, rate);
            }
        } else if (rFrom.mIsFollowCamera) {
            Vec3f pos(rFrom.mPos);
            MR::getCameraInvViewMtx().mult(pos, pos);
            MR::blendVec(&pResult->mPos, pos, rTo.mPos, rate);
        } else {
            MR::blendVec(&pResult->mPos, rFrom.mPos, rTo.mPos, rate);
        }
    }
};  // namespace

void LightFunction::blendActorLightInfo(ActorLightInfo* pResult, const ActorLightInfo& rFrom, const ActorLightInfo& rTo, f32 rate) {
    MR::blendColor(&pResult->mInfo0.mColor, rFrom.mInfo0.mColor, rTo.mInfo0.mColor, rate);
    MR::blendColor(&pResult->mInfo1.mColor, rFrom.mInfo1.mColor, rTo.mInfo1.mColor, rate);
    MR::blendColor(&pResult->mColor, rFrom.mColor, rTo.mColor, rate);
    ::blendActorLightPos(rFrom.mInfo0, rTo.mInfo0, &pResult->mInfo0, rate);
    ::blendActorLightPos(rFrom.mInfo1, rTo.mInfo1, &pResult->mInfo1, rate);
    pResult->mAlpha2 = (s32)(MR::getInterpolateValue(rate, rFrom.mAlpha2, rTo.mAlpha2));
}

namespace {
    void loadLightDiffuse(NebulaColor8 color, const Vec3f& rPos, u8 light) {
        NebulaLight lightObj = {};

        LightValues::pos(&lightObj, rPos.x, rPos.y, rPos.z);
        LightValues::attn(&lightObj, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

        NebulaColor8 new_color(color);
        LightValues::color(&lightObj, new_color);
        Frame::setLight(light, lightObj);
    }

    inline void loadCameraLight(const LightInfo& rInfo, u8 light) {
        Vec3f position(rInfo.mPos);
        ::loadLightDiffuse(NebulaColor8(rInfo.mColor), position, light);
    }

    void loadLightInfoDiffuse(const LightInfo& rInfo, u8 light) {
        if (rInfo.mIsFollowCamera) {
            ::loadCameraLight(rInfo, light);
        } else {
            Vec3f viewPos(rInfo.mPos);
            MR::getCameraViewMtx().mult(viewPos, viewPos);
            ::loadLightDiffuse(NebulaColor8(rInfo.mColor), viewPos, light);
        }
    }
};  // namespace

// Each loader marks only the lights it writes (Frame::setLightFollows): a light another loader wrote keeps its own mark,
// so the host carries every light across a camera move as what it is, fixed to the camera or to the world.
void LightFunction::loadActorLightInfo(const ActorLightInfo* pInfo) {
    Frame::setLightFollows(0, pInfo->mInfo0.mIsFollowCamera);
    Frame::setLightFollows(1, pInfo->mInfo1.mIsFollowCamera);
    // Light 2 (the alpha light) sits at the eye.
    Frame::setLightFollows(2, true);
    GameLightSignal::actorLight(pInfo);
    ::loadLightInfoDiffuse(pInfo->mInfo0, 0);
    ::loadLightInfoDiffuse(pInfo->mInfo1, 1);

    u8 alpha = pInfo->mAlpha2;
    NebulaColor8 c = {0, 0, 0, alpha};
    ::loadLightDiffuse(c, Vec3f(0.0f, 0.0f, 0.0f), 2);
    MaterialAmbient::set(0, NebulaColor8(pInfo->mColor));
}

void LightFunction::loadLightInfoCoin(const LightInfoCoin* pInfo) {
    Frame::setLightFollows(0, pInfo->base.mIsFollowCamera);
    // The shine: a direction fixed in view space.
    Frame::setLightFollows(3, true);
    GameLightSignal::coinLight(pInfo);
    ::loadLightInfoDiffuse(pInfo->base, 0);
    NebulaLight obj = {};
    f32 shininess = pInfo->mShininess;
    LightValues::color(&obj, NebulaColor8(pInfo->mSpecularColor));
    LightValues::specularDir(&obj, 0.0f, 0.0f, -1.0f);
    LightValues::attn(&obj, 0.0f, 0.0f, 1.0f, (shininess / cNum_loadLightInfoCoin_f1), 0.0f, (1.0f - (shininess / cNum_loadLightInfoCoin_f2)));
    Frame::setLight(3, obj);
}

void LightFunction::loadPointLightInfo(const PointLightInfo* pInfo) {
    // A world point (moved into view space below).
    Frame::setLightFollows(4, false);
    GameLightSignal::pointLight(pInfo);
    Vec3f viewPos(pInfo->mPos);
    MR::getCameraViewMtx().mult(viewPos, viewPos);
    NebulaLight obj = {};
    LightValues::pos(&obj, viewPos.x, viewPos.y, viewPos.z);
    LightValues::distAttn(&obj, pInfo->mRefDistance, pInfo->mRefBrightness, pInfo->mDistAttnFn);
    LightValues::noSpot(&obj);
    LightValues::color(&obj, pInfo->mColor);
    Frame::setLight(4, obj);
}

void LightFunction::loadAllLightWhite() {
    // All eight at the eye.
    Frame::setLightFollowsCamera(0xffu);
    GameLightSignal::noLight();

    for (u32 i = 0; i < 8; i++) {
        NebulaLight lightObj = {};
        LightValues::pos(&lightObj, 0.0f, 0.0f, 0.0f);

        NebulaColor8 color = {255, 255, 255, 255};
        LightValues::color(&lightObj, color);
        Frame::setLight(i, lightObj);
    }
}

void LightFunction::calcLightWorldPos(Vec3f* pPos, const LightInfo& rInfo) {
    pPos->x = rInfo.mPos.x;
    pPos->y = rInfo.mPos.y;
    pPos->z = rInfo.mPos.z;

    if (rInfo.mIsFollowCamera) {
        Pos3f mtx;
        mtx.setInline(MR::getCameraViewMtx());
        mtx.invert(mtx);
        mtx.mult(*pPos, *pPos);
    }
}
