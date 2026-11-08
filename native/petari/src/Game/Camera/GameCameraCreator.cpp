#include "Game/Camera/GameCameraCreator.hpp"
#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <cstring>

GameCameraCreator::GameCameraCreator(CameraParamChunkHolder* pChunkHolder) {
    mChunkHolder = pChunkHolder;
    mName = nullptr;
    mZoneId = -1;

    memset(&mCodes[0], 0, sizeof(mCodes));
}

void GameCameraCreator::initCameraCodeCollection(const char* pName, s32 zoneId) {
    mName = pName;
    mZoneId = zoneId;
}

void GameCameraCreator::registerCameraCode(u32 code) {
    if (code >= 0xFF) {
        return;
    }

    mCodes[code] = 1;
}

void GameCameraCreator::termCameraCodeCollection() {
    for (u32 i = 0; i < 0xFF; i++) {
        if (mCodes[i] != 0) {
            mCodes[i] = 0;

            CameraParamChunkID_Tmp chunk = CameraParamChunkID_Tmp();
            chunk.createGroupID(mZoneId, mName, i, 0);

            mChunkHolder->createChunk(chunk, nullptr);
        }
    }

    mName = nullptr;
    mZoneId = -1;
}

void GameCameraCreator::scanStartPos() {
    for (s32 idx = 0; idx < MR::getStartPosNum(); idx++) {
        PlacementId info(-1, -1);

        MR::getStartCameraIdInfoFromStartDataIndex(&info, idx);

        CameraParamChunkID_Tmp chunk = CameraParamChunkID_Tmp();
        chunk.createStartID(info.mZoneID, info.mId);

        mChunkHolder->createChunk(chunk, nullptr);
    }
}

void GameCameraCreator::scanArea() {
    CubeCameraMgr* mgr;
    CubeCameraArea* area;

    mgr = static_cast< CubeCameraMgr* >(MR::getAreaObjManager("CubeCamera"));
    mgr->initAfterLoad();

    for (s32 idx = 0; idx < mgr->getNumAreaObj(); idx++) {
        area = static_cast< CubeCameraArea* >(mgr->getAreaObj(idx));
        if (area->isValidCameraID()) {
            CameraParamChunkID_Tmp chunk = CameraParamChunkID_Tmp();
            s32 zone = area->getZoneID();
            chunk.createCubeID(zone, area->getCameraID());

            mChunkHolder->createChunk(chunk, nullptr);
        }
    }
}
