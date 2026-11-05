#pragma once

#include <nebula_geometry.hpp>

class Camera;
class CameraDirector;
class CameraMan;
class CameraTargetObj;

namespace CameraLocalUtil {
    const Vec3f& getWatchPos(const CameraMan*);
    const Vec3f& getPos(const CameraMan*);
    const Vec3f& getUpVec(const CameraMan*);
    const Vec3f& getWatchUpVec(const CameraMan*);
    f32 getFovy(const CameraMan*);
    const Vec3f& getGlobalOffset(const CameraMan*);
    const Vec3f& getLocalOffset(const CameraMan*);
    f32 getRoll(const CameraMan*);
    void setWatchPos(CameraMan*, const Vec3f&);
    void setPos(CameraMan*, const Vec3f&);
    void setUpVec(CameraMan*, const Vec3f&);
    void setWatchUpVec(CameraMan*, const Vec3f&);
    void setFovy(CameraMan*, f32);
    void setGlobalOffset(CameraMan*, const Vec3f&);
    void setLocalOffset(CameraMan*, const Vec3f&);
    void setFrontOffset(CameraMan*, f32);
    void setUpperOffset(CameraMan*, f32);
    void setRoll(CameraMan*, f32);
    CameraTargetObj* getTarget(const CameraMan*);
    const MtxPtr getMtxReg(const char*);
    const Vec3f& getVecReg(const char*);
    const char* getDummyVecRegName();
    bool isForceCameraChange();
    CameraDirector* getCameraDirector();
    void setUsedTarget(const CameraMan*, CameraTargetObj*);
    CameraTargetObj* getTarget(const Camera*);
    const Vec3f& getWatchPos(const Camera*);
    const Vec3f& getPos(const Camera*);
    const Vec3f& getUpVec(const Camera*);
    const Vec3f& getWatchUpVec(const Camera*);
    f32 getFovy(const Camera*);
    const Vec3f& getGlobalOffset(const Camera*);
    const Vec3f& getLocalOffset(const Camera*);
    f32 getFrontOffset(const Camera*);
    f32 getUpperOffset(const Camera*);
    f32 getRoll(const Camera*);
    void setWatchPos(Camera*, const Vec3f&);
    void setPos(Camera*, const Vec3f&);
    void setUpVec(Camera*, const Vec3f&);
    void setWatchUpVec(Camera*, const Vec3f&);
    void setUpVecAndWatchUpVec(Camera*, const Vec3f&);
    void setFovy(Camera*, f32);
    void setGlobalOffset(Camera*, const Vec3f&);
    void setLocalOffset(Camera*, const Vec3f&);
    void setFrontOffset(Camera*, f32);
    void setUpperOffset(Camera*, f32);
    void setRoll(Camera*, f32);
    void recalcUpVec(Vec3f*, const Vec3f&);
    void makeWatchOffset(Vec3f*, Camera*, CameraTargetObj*, f32);
    void makeWatchPoint(Vec3f*, Camera*, CameraTargetObj*, f32);
    void makeWatchOffsetImm(Vec3f*, Camera*, CameraTargetObj*);
    void makeWatchPointImm(Vec3f*, Camera*, CameraTargetObj*);
    bool tryCameraReset();
    bool tryCameraResetTrigger();
    bool testCameraPadButtonReset();
    bool testCameraPadTriggerReset();
    bool testCameraPadTriggerRoundLeft();
    bool testCameraPadTriggerRoundRight();
    void slerpCamera(Quat4f*, const Quat4f&, const Quat4f&, f32, bool);
    bool makeTowerCameraMtx(Pos3f*, const Pos3f&, const Vec3f&, const Vec3f&, const Vec3f&);
    void arrangeDistanceByFovy(Camera*, Vec3f, f32);
    void arrangeDistanceByPushAndPull(Camera*, f32, f32);
    void calcSafePose(CameraMan*, Camera*);
};  // namespace CameraLocalUtil
