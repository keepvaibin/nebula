#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Camera/DotCamParams.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f1, "arrangeChunk.f1", 0.17453294f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f2, "arrangeChunk.f2", 0.34906587f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f3, "arrangeChunk.f3", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f4, "arrangeChunk.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f5, "arrangeChunk.f5", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f6, "arrangeChunk.f6", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f7, "arrangeChunk.f7", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f8, "arrangeChunk.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f9, "arrangeChunk.f9", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f10, "arrangeChunk.f10", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f11, "arrangeChunk.f11", 0.992f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeChunk_f12, "arrangeChunk.f12", 0.5f);

CameraParamChunkHolder::CameraParamChunkHolder(CameraHolder* pCameraHolder, const char* pName)
    : NameObj(pName), mCameraHolder(pCameraHolder), mChunkCapacity(cInitialCapacity), mNrChunks(),
      mChunks(new CameraParamChunk*[cInitialCapacity]), mIsSorted() {
    lumacore::setClass(this, lumacore::kClassId< CameraParamChunkHolder >);
}

CameraParamChunk* CameraParamChunkHolder::createChunk(const CameraParamChunkID& rID, NebulaArena* pArena) {
    if (isNewAttribute(rID)) {
        char firstChar = rID.mName[0];
        CameraParamChunk* chunk;

        if (firstChar == 'e') {
            chunk = new (pArena, 0) CameraParamChunkEvent(mCameraHolder, rID);
        } else if (firstChar == 'g' || firstChar == 'c' || firstChar == 's') {
            chunk = new (pArena, 0) CameraParamChunkGame(mCameraHolder, rID);
        } else {
            chunk = new (pArena, 0) CameraParamChunk(mCameraHolder, rID);
        }

        if (mNrChunks == mChunkCapacity) {
            mChunks = MR::growArray(mChunks, static_cast< s32 >(mNrChunks));
            mChunkCapacity *= 2;
        }

        mChunks[mNrChunks++] = chunk;

        return chunk;
    }

    return nullptr;
}

CameraParamChunk* CameraParamChunkHolder::getChunk(const CameraParamChunkID& rID) {
    if (rID.mName == nullptr) {
        return nullptr;
    }

    return findChunk(rID);
}

void CameraParamChunkHolder::sort() {
    // Selection sort
    for (s32 i = 0; i < mNrChunks - 1; i++) {
        s32 minIndex = i;

        for (s32 j = i + 1; j < mNrChunks; j++) {
            if (*mChunks[j]->mParamChunkID > *mChunks[minIndex]->mParamChunkID) {
                minIndex = j;
            }
        }

        if (minIndex != i) {
            CameraParamChunk* temp = mChunks[i];
            mChunks[i] = mChunks[minIndex];
            mChunks[minIndex] = temp;
        }
    }

    mIsSorted = true;
}

void CameraParamChunkHolder::loadCameraParameters() {
    for (s32 i = 0; i < MR::getZoneNum(); i++) {
        loadFile(i);
    }
}

bool CameraParamChunkHolder::isNewAttribute(const CameraParamChunkID& rID) {
    for (s32 i = 0; i < mNrChunks; i++) {
        if (*mChunks[i]->mParamChunkID == rID) {
            return false;
        }
    }

    return true;
}

CameraParamChunk* CameraParamChunkHolder::findChunk(const CameraParamChunkID& rID) {
    if (mIsSorted) {
        s32 low = 0;
        s32 high = mNrChunks - 1;

        while (low < high) {
            s32 middle = (low + high) / 2;

            if (*mChunks[middle]->mParamChunkID > rID) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }

        if (*mChunks[low]->mParamChunkID == rID) {
            return mChunks[low];
        }
    } else {
        for (s32 i = 0; i < mNrChunks; i++) {
            if (*mChunks[i]->mParamChunkID == rID) {
                return mChunks[i];
            }
        }
    }

    return nullptr;
}

CameraParamChunk* CameraParamChunkHolder::findChunk(s32 zoneID, const char* pName) {
    for (s32 i = 0; i < mNrChunks; i++) {
        if (mChunks[i]->mParamChunkID->equals(zoneID, pName)) {
            return mChunks[i];
        }
    }

    return nullptr;
}

void CameraParamChunkHolder::loadFile(s32 zoneID) {
    void* data;
    s32 dataSize;
    MR::getStageCameraData(&data, &dataSize, zoneID);

    if (dataSize != 0) {
        DotCamReaderInBin reader = DotCamReaderInBin(data);
        mCameraVersion = reader.mVersion;

        while (reader.hasMoreChunk()) {
            const char* id = nullptr;
            reader.getValueString("id", &id);

            CameraParamChunk* chunk = findChunk(zoneID, id);

            if (chunk != nullptr && chunk->mIsCodeChunk) {
                reader.nextToChunk();
            } else {
                if (chunk != nullptr) {
                    chunk->load(&reader, mCameraHolder);
                    arrangeChunk(chunk);
                }

                reader.nextToChunk();
            }
        }
    }
}

void CameraParamChunkHolder::arrangeChunk(CameraParamChunk* pChunk) {
    s32 index = pChunk->mCameraTypeIndex;

    if (mCameraHolder->getIndexOf("CAM_TYPE_FOLLOW") == index) {
        if (mCameraVersion < 0x30001) {
            CameraGeneralParam* param = pChunk->mGeneralParam;

            param->mAngleA = cNum_arrangeChunk_f1;  // 10 degrees
            param->mAngleB = cNum_arrangeChunk_f2;  // 20 degrees
        }

        if (mCameraVersion < 0x30002) {
            pChunk->mGeneralParam->mDist = cNum_arrangeChunk_f3;
        }
    }

    if (mCameraVersion < 0x30003) {
        const char* className = pChunk->getClassName();

        if (MR::isEqualString(className, "Game")) {
            CameraParamChunkGame* pGameChunk = static_cast< CameraParamChunkGame* >(pChunk);
            pGameChunk->mThru = 0;
        }
    }

    index = pChunk->mCameraTypeIndex;

    if (mCameraHolder->getIndexOf("CAM_TYPE_RAIL_FOLLOW") == index) {
        if (mCameraVersion < 0x30007) {
            CameraGeneralParam* param = pChunk->mGeneralParam;

            param->mWPoint.x = cNum_arrangeChunk_f4;
            param->mWPoint.y = cNum_arrangeChunk_f5;
            param->mWPoint.z = 0.0f;
        }
    }

    if (mCameraVersion < 0x30008) {
        pChunk->mExParam.mVPanAxis.set(Vec3f(0.0f, 1.0f, 0.0f));
    }

    if (mCameraVersion < 0x3000A) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_CUBE_PLANET") == index) {
            pChunk->mGeneralParam->mAngleB = cNum_arrangeChunk_f6;
        }
    }

    if (mCameraVersion < 0x3000B) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_WATER_FOLLOW") == index) {
            pChunk->mGeneralParam->mDist = cNum_arrangeChunk_f7;
        }
    }

    if (mCameraVersion < 0x3000C) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_CHARMED_VECREG") == index) {
            CameraGeneralParam* param = pChunk->mGeneralParam;
            param->mString.setCharPtr(CameraLocalUtil::getDummyVecRegName());
        }
    }

    if (mCameraVersion < 0x3000D) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_FOLLOW") == index) {
            pChunk->mGeneralParam->mNum1 = 1;
        }
    }

    if (mCameraVersion < 0x3000E) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_CHARMED_VECREG") == index) {
            pChunk->mGeneralParam->mAngleA = cNum_arrangeChunk_f8;
        }
    }

    if (mCameraVersion < 0x3000F) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_MEDIAN_TOWER") == index) {
            pChunk->mGeneralParam->mUp.zero();
        }
    }

    if (mCameraVersion < 0x30010) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_MEDIAN_TOWER") == index) {
            pChunk->mGeneralParam->mUp.y = cNum_arrangeChunk_f9;
        }
    }

    if (mCameraVersion < 0x30011) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_CHARMED_VECREG") == index) {
            pChunk->mGeneralParam->mAngleB = cNum_arrangeChunk_f10;
        }
    }

    if (mCameraVersion < 0x30012) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_MEDIAN_TOWER") == index) {
            pChunk->mGeneralParam->mUp.z = cNum_arrangeChunk_f11;
        }
    }

    if (mCameraVersion < 0x30013) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_EYEPOS_FIX_THERE") == index) {
            pChunk->mExParam.mRoll = 0.0f;
        }
    }

    if (mCameraVersion < 0x30014) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_MEDIAN_PLANET") == index) {
            pChunk->mGeneralParam->mWPoint.x = cNum_arrangeChunk_f12;
        }
    }

    if (mCameraVersion < 0x30015) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_DPD") == index) {
            pChunk->mGeneralParam->mUp.zero();
            pChunk->mGeneralParam->mNum2 = 0;
        }
    }

    if (mCameraVersion < 0x30016) {
        index = pChunk->mCameraTypeIndex;

        if (mCameraHolder->getIndexOf("CAM_TYPE_XZ_PARA") == index) {
            pChunk->mGeneralParam->mNum1 = 0;
        }
    }
}
