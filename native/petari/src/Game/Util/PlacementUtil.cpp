#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaClasses.hpp"

bool MR::getObjectName(const char** pName, const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    if (rIter.getValue< const char* >("type", pName)) {
        return true;
    }

    return rIter.getValue< const char* >("name", pName);
}

bool MR::isExistPlacementArg(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg;

    return rIter.getValue< s32 >("Obj_arg0", &arg);
}

bool MR::getPlacementShapeIdWithInit(const TableIter& rIter, s32* pShapeID) {
    return rIter.getValue< s32 >("ShapeModelNo", pShapeID);
}

bool MR::getPlacementTransLocal(const TableIter& rIter, Vec3f* pOut) {
    if (!MR::getValue< f32 >(rIter, "pos_x", &pOut->x)) {
        return false;
    }

    if (!MR::getValue< f32 >(rIter, "pos_y", &pOut->y)) {
        return false;
    }

    return MR::getValue< f32 >(rIter, "pos_z", &pOut->z);
}

bool MR::getPlacementRotateLocal(const TableIter& rIter, Vec3f* pOut) {
    if (!MR::getValue< f32 >(rIter, "dir_x", &pOut->x)) {
        return false;
    }

    if (!MR::getValue< f32 >(rIter, "dir_y", &pOut->y)) {
        return false;
    }

    return MR::getValue< f32 >(rIter, "dir_z", &pOut->z);
}

bool MR::getPlacementScale(const TableIter& rIter, Vec3f* pOut) {
    if (!MR::getValue< f32 >(rIter, "scale_x", &pOut->x)) {
        return false;
    }

    if (!MR::getValue< f32 >(rIter, "scale_y", &pOut->y)) {
        return false;
    }

    return MR::getValue< f32 >(rIter, "scale_z", &pOut->z);
}

bool MR::isValidInfo(const TableIter& rIter) {
    return rIter.isValid();
}

bool MR::isObjectName(const TableIter& rIter, const char* pName) {
    const char* objName = nullptr;

    if (getObjectName(&objName, rIter)) {
        return isEqualString(pName, objName);
    }

    return false;
}

namespace {
    bool getPlacementArgNoInit(const TableIter& rIter, const char* pName, s32* pOut) {
        // An object an alias placed (LumaClasses, classes.md, "alias") reads the alias's arguments while it
        // initialises; -1 there leaves the placement's own.
        if (luma::classes::placementArg(rIter.mInfo, rIter.mIndex, pName, pOut)) {
            return true;
        }

        s32 arg;

        if (!rIter.getValue(pName, &arg)) {
            return false;
        }

        if (arg != -1) {
            *pOut = arg;

            return true;
        }

        return false;
    }

    bool getPlacementArgNoInit(const TableIter& rIter, const char* pName, f32* pOut) {
        s32 arg;

        if (!getPlacementArgNoInit(rIter, pName, &arg)) {
            return false;
        }

        *pOut = arg;

        return true;
    }

    bool getPlacementArgNoInit(const TableIter& rIter, const char* pName, bool* pOut) {
        s32 arg;

        if (!getPlacementArgNoInit(rIter, pName, &arg)) {
            return false;
        }

        if (arg != -1) {
            *pOut = true;
        } else {
            *pOut = false;
        }

        return true;
    }

    bool getPlacementArgWithInit(const TableIter& rIter, const char* pName, s32* pOut) {
        *pOut = -1;

        return getPlacementArgNoInit(rIter, pName, pOut);
    }

    bool getPlacementArgWithInit(const TableIter& rIter, const char* pName, f32* pOut) {
        *pOut = -1.0f;

        return getPlacementArgNoInit(rIter, pName, pOut);
    }

    bool getPlacementArgWithInit(const TableIter& rIter, const char* pName, bool* pOut) {
        *pOut = false;

        return getPlacementArgNoInit(rIter, pName, pOut);
    }
};  // namespace

bool MR::getPlacementTrans(const TableIter& rIter, Vec3f* pOut) {
    if (!getPlacementTransLocal(rIter, pOut)) {
        return false;
    }

    if (isPlacementLocalStage()) {
        getZonePlacementMtx(rIter)->mult(*pOut, *pOut);
    }

    return true;
}

bool MR::getPlacementRotate(const TableIter& rIter, Vec3f* pOut) {
    if (!getPlacementRotateLocal(rIter, pOut)) {
        return false;
    }

    if (isPlacementLocalStage()) {
        Mtx34f rotateMtx;
        makeMtxRotate(rotateMtx.toMtxPtr(), *pOut);
        rotateMtx.concat(*getZonePlacementMtx(rIter), rotateMtx);

        // Euler angles from the matrix, with the two gimbal-locked cases first.
        if (-0.001f <= rotateMtx.mMtx[2][0] - 1.0f) {
            pOut->x = std::atan2(-rotateMtx.mMtx[0][1], rotateMtx.mMtx[1][1]);
            pOut->y = -1.5707964f;
            pOut->z = 0.0f;
        } else if (rotateMtx.mMtx[2][0] + 1.0f <= 0.001f) {
            pOut->x = std::atan2(rotateMtx.mMtx[0][1], rotateMtx.mMtx[1][1]);
            pOut->y = 1.5707964f;
            pOut->z = 0.0f;
        } else {
            pOut->x = std::atan2(rotateMtx.mMtx[2][1], rotateMtx.mMtx[2][2]);
            pOut->z = std::atan2(rotateMtx.mMtx[1][0], rotateMtx.mMtx[0][0]);
            pOut->y = ::asin(-rotateMtx.mMtx[2][0]);
        }

        *pOut = *pOut * _180_PI;
    }

    return true;
}

bool MR::getPlacementMatrixFromRT(const TableIter& rIter, Pos3f* pOut) {
    Vec3f trans;
    if (!getPlacementTrans(rIter, &trans)) {
        return false;
    }

    Vec3f rotate;
    if (!getPlacementRotate(rIter, &rotate)) {
        return false;
    }

    makeMtxTR(pOut->toMtxPtr(), trans, rotate);

    return true;
}

bool MR::getPlacementArg0WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg0", pOut);
}

bool MR::getPlacementArg0WithInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg0", pOut);
}

bool MR::getPlacementArg0WithInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg0", pOut);
}

bool MR::getPlacementArg1WithInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg1", pOut);
}

bool MR::getPlacementArg1WithInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg1", pOut);
}

bool MR::getPlacementArg2WithInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg2", pOut);
}

bool MR::getPlacementArg2WithInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg2", pOut);
}

bool MR::getPlacementArg3WithInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg3", pOut);
}

bool MR::getPlacementArg3WithInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg3", pOut);
}

bool MR::getPlacementArg4WithInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg4", pOut);
}

bool MR::getPlacementArg7WithInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg7", pOut);
}

bool MR::getPlacementArg0NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg0", pOut);
}

bool MR::getPlacementArg0NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg0", pOut);
}

bool MR::getPlacementArg0NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg0", pOut);
}

bool MR::getPlacementArg1NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg1", pOut);
}

bool MR::getPlacementArg1NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg1", pOut);
}

bool MR::getPlacementArg1NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg1", pOut);
}

bool MR::getPlacementArg2NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg2", pOut);
}

bool MR::getPlacementArg2NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg2", pOut);
}

bool MR::getPlacementArg2NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg2", pOut);
}

bool MR::getPlacementArg3NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg3", pOut);
}

bool MR::getPlacementArg3NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg3", pOut);
}

bool MR::getPlacementArg3NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg3", pOut);
}

bool MR::getPlacementArg4NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg4", pOut);
}

bool MR::getPlacementArg4NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg4", pOut);
}

bool MR::getPlacementArg4NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg4", pOut);
}

bool MR::getPlacementArg5NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg5", pOut);
}

bool MR::getPlacementArg5NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg5", pOut);
}

bool MR::getPlacementArg5NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg5", pOut);
}

bool MR::getPlacementArg6NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg6", pOut);
}

bool MR::getPlacementArg6NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg6", pOut);
}

bool MR::getPlacementArg6NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg6", pOut);
}

bool MR::getPlacementArg7NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg7", pOut);
}

bool MR::getPlacementArg7NoInit(const TableIter& rIter, f32* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg7", pOut);
}

bool MR::getPlacementArg7NoInit(const TableIter& rIter, bool* pOut) {
    return ::getPlacementArgNoInit(rIter, "Obj_arg7", pOut);
}

bool MR::isEqualObjectName(const TableIter& rIter, const char* pOtherName) {
    const char* objName = nullptr;

    // A row with no object name is no object of that name.
    if (!getObjectName(&objName, rIter) || objName == nullptr) {
        return false;
    }

    return isEqualStringCase(objName, pOtherName);
}

s32 MR::getDemoGroupLinkID(const TableIter& rIter) {
    s32 linkID = -1;
    rIter.getValue< s32 >("l_id", &linkID);

    return linkID;
}

namespace {
    bool getPlacementRailArg(const TableIter& rIter, const char* pName, s32* pOut) {
        s32 arg;

        if (!rIter.getValue< s32 >(pName, &arg)) {
            return false;
        }

        if (arg != -1) {
            *pOut = arg;
            return true;
        }

        return false;
    }
};  // namespace

bool MR::getPlacementRailArg0NoInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementRailArg(rIter, "path_arg0", pOut);
}

bool MR::getRailId(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "CommonPath_ID", pOut);
}

bool MR::getPlacementV3f(const TableIter& rIter, const char* pName, Vec3f* pOut) {
    MR::FormattedName str("%sX", pName);

    if (!MR::getValue< f32 >(rIter, str.c_str(), &pOut->x)) {
        return false;
    }

    str.format("%sY", pName);

    if (!MR::getValue< f32 >(rIter, str.c_str(), &pOut->y)) {
        return false;
    }

    str.format("%sZ", pName);

    return MR::getValue< f32 >(rIter, str.c_str(), &pOut->z);
}

bool MR::getPlacementArg1WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg1", pOut);
}

bool MR::getPlacementArg2WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg2", pOut);
}

bool MR::getPlacementArg3WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg3", pOut);
}

bool MR::getPlacementArg4WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg4", pOut);
}

bool MR::getPlacementArg5WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg5", pOut);
}

bool MR::getPlacementArg6WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg6", pOut);
}

bool MR::getPlacementArg7WithInit(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "Obj_arg7", pOut);
}

bool MR::getPlacementFollowID(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "FollowId", pOut);
}

bool MR::getPlacementGroupID(const TableIter& rIter, s32* pOut) {
    if (::getPlacementArgWithInit(rIter, "GroupId", pOut)) {
        return true;
    }

    return MR::getPlacementClippingGroupID(rIter, pOut);
}

bool MR::getPlacementClippingGroupID(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "ClippingGroupId", pOut);
}

bool MR::getPlacementDemoGroupID(const TableIter& rIter, s32* pOut) {
    return ::getPlacementArgWithInit(rIter, "DemoGroupId", pOut);
}

bool MR::getPlacementLinkID(const TableIter& rIter, s32* pOut) {
    return rIter.getValue< s32 >("l_id", pOut);
}

bool MR::isConnectedWithRail(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg;

    if (!::getPlacementArgWithInit(rIter, "CommonPath_ID", &arg)) {
        return false;
    }

    return arg != -1;
}

bool MR::isExistStageSwitchA(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg = -1;
    rIter.getValue< s32 >("SW_A", &arg);

    return arg != -1;
}

bool MR::isExistStageSwitchB(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg = -1;
    rIter.getValue< s32 >("SW_B", &arg);

    return arg != -1;
}

bool MR::isExistStageSwitchAppear(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg = -1;
    rIter.getValue< s32 >("SW_APPEAR", &arg);

    return arg != -1;
}

bool MR::isExistStageSwitchDead(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg = -1;
    rIter.getValue< s32 >("SW_DEAD", &arg);

    return arg != -1;
}

bool MR::isExistStageSwitchSleep(const TableIter& rIter) {
    if (!rIter.isValid()) {
        return false;
    }

    s32 arg = -1;
    rIter.getValue< s32 >("SW_SLEEP", &arg);

    return arg != -1;
}

bool MR::getPlacementCameraSetID(const TableIter& rIter, s32* pOut) {
    *pOut = -1;

    if (!rIter.isValid()) {
        return false;
    }

    return ::getPlacementArgNoInit(rIter, "CameraSetId", pOut);
}

bool MR::getPlacementViewGroupID(const TableIter& rIter, s32* pOut) {
    *pOut = -1;

    if (!rIter.isValid()) {
        return false;
    }

    return ::getPlacementArgNoInit(rIter, "ViewGroupId", pOut);
}

bool MR::getPlacementMessageID(const TableIter& rIter, s32* pOut) {
    *pOut = -1;

    if (!rIter.isValid()) {
        return false;
    }

    return ::getPlacementArgNoInit(rIter, "MessageId", pOut);
}

s32 MR::getDemoCastID(const TableIter& rIter) {
    s32 castId = -1;
    rIter.getValue< s32 >("CastId", &castId);

    return castId;
}

const char* MR::getDemoName(const TableIter& rIter) {
    const char* demoName = nullptr;
    rIter.getValue< const char* >("DemoName", &demoName);

    return demoName;
}

const char* MR::getDemoSheetName(const TableIter& rIter) {
    const char* timeSheetName = nullptr;
    rIter.getValue< const char* >("TimeSheetName", &timeSheetName);
    return timeSheetName;
}

bool MR::getNextLinkRailID(const TableIter& rIter, s32* pOut) {
    return rIter.getValue< s32 >("Path_ID", pOut);
}

bool MR::isEqualRailUsage(const TableIter& rIter, const char* pUsage) {
    const char* usage = nullptr;
    rIter.getValue< const char* >("usage", &usage);

    return usage != nullptr && isEqualStringCase(usage, pUsage);
}

namespace {
    // A rail point's position (pnt0 the point, pnt1/pnt2 its control points), placed in its zone.
    void getRailPointPosByField(const TableIter& rIter, const char* pX, const char* pY, const char* pZ, Vec3f* pOut) {
        rIter.getValue< f32 >(pX, &pOut->x);
        rIter.getValue< f32 >(pY, &pOut->y);
        rIter.getValue< f32 >(pZ, &pOut->z);

        if (MR::isPlacementLocalStage()) {
            MR::getZonePlacementMtx(rIter)->mult(*pOut, *pOut);
        }
    }
}  // namespace

void MR::getRailPointPos0(const TableIter& rIter, Vec3f* pOut) {
    ::getRailPointPosByField(rIter, "pnt0_x", "pnt0_y", "pnt0_z", pOut);
}

void MR::getRailPointPos1(const TableIter& rIter, Vec3f* pOut) {
    ::getRailPointPosByField(rIter, "pnt1_x", "pnt1_y", "pnt1_z", pOut);
}

void MR::getRailPointPos2(const TableIter& rIter, Vec3f* pOut) {
    ::getRailPointPosByField(rIter, "pnt2_x", "pnt2_y", "pnt2_z", pOut);
}

bool MR::isLoopRailPathIter(const TableIter& rIter) {
    const char* closed = "";
    rIter.getValue< const char* >("closed", &closed);

    return isEqualString(closed, "CLOSE");
}
