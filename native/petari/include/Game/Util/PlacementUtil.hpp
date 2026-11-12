#pragma once

#include "Game/LumaMarks.hpp"

#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace MR {
    bool isValidInfo(const TableIter& rIter);
    bool isObjectName(const TableIter& rIter, const char* pName);
    bool getPlacementArgNoInit(const TableIter& rIter, const char* pFieldName, s32*);
    bool getPlacementArgNoInit(const TableIter& rIter, const char* pFieldName, f32*);
    bool getPlacementArgNoInit(const TableIter& rIter, const char* pFieldName, bool*);
    bool getPlacementTrans(const TableIter& rIter, Vec3f*);
    bool getPlacementRotate(const TableIter& rIter, Vec3f*);
    bool getPlacementMatrixFromRT(const TableIter& rIter, Pos3f*);
    bool getPlacementArg0WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg0WithInit(const TableIter& rIter, f32*);
    bool getPlacementArg0WithInit(const TableIter& rIter, bool*);
    bool getPlacementArg1WithInit(const TableIter& rIter, f32*);
    bool getPlacementArg1WithInit(const TableIter& rIter, bool*);
    bool getPlacementArg2WithInit(const TableIter& rIter, f32*);
    bool getPlacementArg2WithInit(const TableIter& rIter, bool*);
    bool getPlacementArg3WithInit(const TableIter& rIter, f32*);
    bool getPlacementArg3WithInit(const TableIter& rIter, bool*);
    bool getPlacementArg4WithInit(const TableIter& rIter, bool*);
    bool getPlacementArg7WithInit(const TableIter& rIter, bool*);
    bool getPlacementArg0NoInit(const TableIter& rIter, LUMA_OUT s32*);
    bool getPlacementArg0NoInit(const TableIter& rIter, LUMA_OUT f32*);
    bool getPlacementArg0NoInit(const TableIter& rIter, LUMA_OUT bool*);
    bool getPlacementArg1NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg1NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg1NoInit(const TableIter& rIter, bool*);
    bool getPlacementArg2NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg2NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg2NoInit(const TableIter& rIter, bool*);
    bool getPlacementArg3NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg3NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg3NoInit(const TableIter& rIter, bool*);
    bool getPlacementArg4NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg4NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg4NoInit(const TableIter& rIter, bool*);
    bool getPlacementArg5NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg5NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg5NoInit(const TableIter& rIter, bool*);
    bool getPlacementArg6NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg6NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg6NoInit(const TableIter& rIter, bool*);
    bool getPlacementArg7NoInit(const TableIter& rIter, s32*);
    bool getPlacementArg7NoInit(const TableIter& rIter, f32*);
    bool getPlacementArg7NoInit(const TableIter& rIter, bool*);
    bool isEqualObjectName(const TableIter& rIter, const char*);
    s32 getDemoGroupLinkID(const TableIter& rIter);
    bool getPlacementRailArg(const TableIter& rIter, const char*, s32*);
    bool getPlacementRailArg0NoInit(const TableIter& rIter, s32*);
    bool getRailId(const TableIter& rIter, s32*);
    bool getObjectName(const char** pDest, const TableIter& rIter);
    bool isExistPlacementArg(const TableIter& rIter);
    bool getPlacementShapeIdWithInit(const TableIter& rIter, s32*);
    bool getPlacementTransLocal(const TableIter& rIter, Vec3f*);
    bool getPlacementRotateLocal(const TableIter& rIter, Vec3f*);
    bool getPlacementScale(const TableIter& rIter, Vec3f*);
    bool getPlacementV3f(const TableIter& rIter, const char*, Vec3f*);
    bool getPlacementArg1WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg2WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg3WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg4WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg5WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg6WithInit(const TableIter& rIter, s32*);
    bool getPlacementArg7WithInit(const TableIter& rIter, s32*);
    bool getPlacementFollowID(const TableIter& rIter, s32*);
    bool getPlacementGroupID(const TableIter& rIter, s32*);
    bool getPlacementClippingGroupID(const TableIter& rIter, s32*);
    bool getPlacementDemoGroupID(const TableIter& rIter, s32*);
    bool getPlacementLinkID(const TableIter& rIter, s32*);
    bool isConnectedWithRail(const TableIter& rIter);
    bool isExistStageSwitchA(const TableIter& rIter);
    bool isExistStageSwitchB(const TableIter& rIter);
    bool isExistStageSwitchAppear(const TableIter& rIter);
    bool isExistStageSwitchDead(const TableIter& rIter);
    bool isExistStageSwitchSleep(const TableIter& rIter);
    bool getPlacementCameraSetID(const TableIter& rIter, s32*);
    bool getPlacementViewGroupID(const TableIter& rIter, s32*);
    bool getPlacementMessageID(const TableIter& rIter, s32*);
    s32 getDemoCastID(const TableIter& rIter);
    const char* getDemoName(const TableIter& rIter);
    const char* getDemoSheetName(const TableIter& rIter);
    bool getNextLinkRailID(const TableIter& rIter, s32*);
    bool isEqualRailUsage(const TableIter& rIter, const char*);
    void getRailPointPos0(const TableIter& rIter, Vec3f*);
    void getRailPointPos1(const TableIter& rIter, Vec3f*);
    void getRailPointPos2(const TableIter& rIter, Vec3f*);
    bool isLoopRailPathIter(const TableIter& rIter);

    template < typename T >
    inline bool getValue(const TableIter& rIter, const char* pName, T* pOut) {
        return rIter.getValue< T >(pName, pOut);
    }

    inline s32 getMessageID(const TableIter& rIter) {
        s32 msgId;
        getPlacementMessageID(rIter, &msgId);
        return msgId;
    }

    inline bool checkTableEntries(const TableIter& rIter) {
        return rIter.isValid();
    }
};  // namespace MR
