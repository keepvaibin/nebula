#pragma once


class JMapInfoIter;

class ActorCameraInfo {
public:
    ActorCameraInfo(const JMapInfoIter&);
    ActorCameraInfo(s32 cameraSetID = -1, s32 zoneID = 0);

    s32 mCameraSetID;
    s32 mZoneID;
};
