#pragma once


class TableIter;

class ActorCameraInfo {
public:
    ActorCameraInfo(const TableIter&);
    ActorCameraInfo(s32 cameraSetID = -1, s32 zoneID = 0);

    s32 mCameraSetID;
    s32 mZoneID;
};
