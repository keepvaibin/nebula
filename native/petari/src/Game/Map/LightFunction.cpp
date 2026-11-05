#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include "Game/Map/LightFunction.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Util/GameLightSignal.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/AreaObj/LightAreaHolder.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <cstdio>


namespace {};  // namespace

void LightFunction::initLightRegisterAll() {
    Color8 c(255, 255, 255, 255);

    for (s32 i = 0; i < 8; i++) {
        J3DNativeLight obj;
        J3DNativeState::initLightColor(&obj, c);
        J3DNativeState::initLightPos(&obj, 0.0f, 0.0f, 0.0f);
        J3DNativeState::initLightDir(&obj, 0.0f, -1.0f, 0.0f);
        J3DNativeState::initLightAttn(&obj, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        J3DNativeState::initSpecularDir(&obj, 0.0f, -1.0f, 0.0f);
        J3DNativeState::initSpecularDirHA(&obj, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f);
        J3DNativeState::initLightAttn(&obj, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
        J3DNativeState::sendLight(&obj, i);
    }
}

void LightFunction::initLightData() {
    MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->initData();
}

ResourceHolder* LightFunction::loadLightArchive() {
    char buf[0x100];
    nebula_snprintf(buf, sizeof(buf), "LightData.arc");
    return MR::createAndAddResourceHolder(buf);
}

s32 LightFunction::createLightDataParser(JMapInfo** pOut) {
    ResourceHolder* holder = MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mResourceHolder;
    JMapInfo* info = MR::tryCreateCsvParser(holder, "LightData.bcsv");
    *pOut = info;
    if (info->mData != nullptr) {
        return info->mData->mNumEntries;
    }

    return 0;
}

s32 LightFunction::createZoneDataParser(const char* pZone, JMapInfo** pOut) {
    ResourceHolder* holder = MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mResourceHolder;
    JMapInfo* info = MR::tryCreateCsvParser(holder, "Light%s.bcsv", pZone);
    *pOut = info;

    if (info != nullptr) {
        if (info->mData != nullptr) {
            return info->mData->mNumEntries;
        }

        return 0;
    }

    return 0;
}

namespace {
    void getDataLightInfo(JMapInfo* pInfo, int id, LightInfo* pLightInfo, const char* pName) {
        char colorBuf[256];
        nebula_snprintf(colorBuf, sizeof(colorBuf), "%sColor", pName);
        MR::getCsvDataColor(&pLightInfo->mColor, pInfo, colorBuf, id);
        char posBuf[256];
        nebula_snprintf(posBuf, sizeof(posBuf), "%sPos", pName);
        MR::getCsvDataVec(&pLightInfo->mPos, pInfo, posBuf, id);
        char camBuf[256];
        nebula_snprintf(camBuf, sizeof(camBuf), "%sFollowCamera", pName);
        MR::getCsvDataBool(&pLightInfo->mIsFollowCamera, pInfo, camBuf, id);
    }

    void getDataActorLightInfo(JMapInfo* pInfo, int lightID, ActorLightInfo* pActorInfo, const char* pName) {
        char lightBuf[256];
        nebula_snprintf(lightBuf, sizeof(lightBuf), "%sLight%d", pName, 0);
        getDataLightInfo(pInfo, lightID, &pActorInfo->mInfo0, lightBuf);
        nebula_snprintf(lightBuf, sizeof(lightBuf), "%sLight%d", pName, 1);
        getDataLightInfo(pInfo, lightID, &pActorInfo->mInfo1, lightBuf);
        char alphaBuf[256];
        nebula_snprintf(alphaBuf, sizeof(alphaBuf), "%sAlpha2", pName);
        MR::getCsvDataU8(&pActorInfo->mAlpha2, pInfo, alphaBuf, lightID);
        char ambBuf[256];
        nebula_snprintf(ambBuf, sizeof(ambBuf), "%sAmbient", pName);
        MR::getCsvDataColor(&pActorInfo->mColor, pInfo, ambBuf, lightID);
    }
};  // namespace

void LightFunction::getAreaLightLightData(JMapInfo* pInfo, int idx, AreaLightInfo* pLightInfo) {
    MR::getCsvDataStr(&pLightInfo->mAreaLightName, pInfo, "AreaLightName", idx);
    MR::getCsvDataS32(&pLightInfo->mInterpolate, pInfo, "Interpolate", idx);
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mPlayerLight, "Player");
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mStrongLight, "Strong");
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mWeakLight, "Weak");
    ::getDataActorLightInfo(pInfo, idx, &pLightInfo->mPlanetLight, "Planet");
    MR::getCsvDataBool(&pLightInfo->mFix, pInfo, "Fix", idx);
}

const char* LightFunction::getDefaultAreaLightName() {
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mDataHolder->getDefaultAreaLightName();
}

s32 LightFunction::getDefaultStepInterpolate() {
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->mDataHolder->getDefaultStepInterpolate();
}

void LightFunction::registerPlayerLightCtrl(const ActorLightCtrl* pCtrl) {
    MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->_1C = pCtrl;
}

void LightFunction::registerLightAreaHolder(LightAreaHolder* pHolder) {
    MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->_C = pHolder;
}

bool LightFunction::tryFindNewAreaLightID(const TVec3f& rPos, ZoneLightID* pId) {
    return MR::getSceneObj< LightDirector >(SceneObj_LightDirector)->_C->tryFindLightID(rPos, pId);
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
                TVec3f pos(rFrom.mPos);
                MR::getCameraViewMtx().mult(pos, pos);
                MR::blendVec(&pResult->mPos, pos, rTo.mPos, rate);
            }
        } else if (rFrom.mIsFollowCamera) {
            TVec3f pos(rFrom.mPos);
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
    void loadLightDiffuse(NebulaColor8 color, const TVec3f& rPos, u8 light) {
        J3DNativeLight lightObj;

        J3DNativeState::initLightPos(&lightObj, rPos.x, rPos.y, rPos.z);
        J3DNativeState::initLightAttn(&lightObj, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

        NebulaColor8 new_color(color);
        J3DNativeState::initLightColor(&lightObj, new_color);
        J3DNativeState::sendLight(&lightObj, light);
    }

    inline void loadCameraLight(const LightInfo& rInfo, u8 light) {
        TVec3f position(rInfo.mPos);
        ::loadLightDiffuse(NebulaColor8(rInfo.mColor), position, light);
    }

    void loadLightInfoDiffuse(const LightInfo& rInfo, u8 light) {
        if (rInfo.mIsFollowCamera) {
            ::loadCameraLight(rInfo, light);
        } else {
            TVec3f v14(rInfo.mPos);
            MR::getCameraViewMtx().mult(v14, v14);
            ::loadLightDiffuse(NebulaColor8(rInfo.mColor), v14, light);
        }
    }
};  // namespace

void LightFunction::loadActorLightInfo(const ActorLightInfo* pInfo) {
    MotionSignal::actorLight(pInfo);
    GameLightSignal::actorLight(pInfo);
    ::loadLightInfoDiffuse(pInfo->mInfo0, 0);
    ::loadLightInfoDiffuse(pInfo->mInfo1, 1);

    u8 alpha = pInfo->mAlpha2;
    NebulaColor8 c = {0, 0, 0, alpha};
    ::loadLightDiffuse(c, TVec3f(0.0f, 0.0f, 0.0f), 2);
    J3DNativeMaterial::setLightAmbient(0, NebulaColor8(pInfo->mColor));
}

void LightFunction::loadLightInfoCoin(const LightInfoCoin* pInfo) {
    MotionSignal::coinLight(pInfo);
    ::loadLightInfoDiffuse(pInfo->base, 0);
    TVec3f v7(0.0f, 0.0f, -1.0f);
    J3DNativeLight obj;
    f32 v = pInfo->_18;
    J3DNativeState::initLightColor(&obj, NebulaColor8(pInfo->_14));
    J3DNativeState::initSpecularDir(&obj, v7.x, v7.y, v7.z);
    J3DNativeState::initLightAttn(&obj, 0.0f, 0.0f, 1.0f, (v / 2.0f), 0.0f, (1.0f - (v / 2.0f)));
    J3DNativeState::sendLight(&obj, 3);
}

void LightFunction::loadPointLightInfo(const PointLightInfo* pInfo) {
    MotionSignal::pointLight(pInfo);
    GameLightSignal::pointLight(pInfo);
    TVec3f v7(pInfo->mPos);
    MR::getCameraViewMtx().mult(v7, v7);
    J3DNativeLight obj;
    J3DNativeState::initLightPos(&obj, v7.x, v7.y, v7.z);
    J3DNativeState::initLightDistAttn(&obj, pInfo->mRefDistance, pInfo->mRefBrightness, pInfo->mDistAttnFn);
    J3DNativeState::initLightNoSpot(&obj);
    J3DNativeState::initLightColor(&obj, pInfo->mColor);
    J3DNativeState::sendLight(&obj, 4);
}

void LightFunction::loadAllLightWhite() {
    MotionSignal::allLightWhite();

    for (u32 i = 0; i < 8; i++) {
        J3DNativeLight lightObj;
        J3DNativeState::initLightPos(&lightObj, 0.0f, 0.0f, 0.0f);

        NebulaColor8 color = {255, 255, 255, 255};
        J3DNativeState::initLightColor(&lightObj, color);
        J3DNativeState::sendLight(&lightObj, i);
    }
}

void LightFunction::calcLightWorldPos(TVec3f* pPos, const LightInfo& rInfo) {
    pPos->x = rInfo.mPos.x;
    pPos->y = rInfo.mPos.y;
    pPos->z = rInfo.mPos.z;

    if (rInfo.mIsFollowCamera) {
        TPos3f mtx;
        mtx.setInline(MR::getCameraViewMtx());
        mtx.invert(mtx);
        mtx.mult(*pPos, *pPos);
    }
}
