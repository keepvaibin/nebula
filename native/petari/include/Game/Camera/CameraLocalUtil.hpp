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
    MtxPtr getMtxReg(const char*);
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
    // rVec normalized, or rDefault when rVec is near zero.
    Vec3f getNormalizedOr(const Vec3f& rVec, const Vec3f& rDefault);
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
    // *pDst: the target's watch point moved onto its ground. After the first frame (*pIsFirstFrame, then cleared) its height along
    // the target's up moves 5% a frame from *pLastWatchPoint's; *pLastWatchPoint keeps the result.
    void makeGroundWatchPoint(Vec3f* pDst, Camera* pCamera, Vec3f* pLastWatchPoint, bool* pIsFirstFrame);
    // Moves *pAngle by step toward target; false once it is there.
    bool approachRoundAngle(f32* pAngle, f32 target, f32 step);
    // The angle one round left or right of *pAngle on a grid of roundDiv steps a turn; past half the grid *pAngle is wrapped a turn.
    f32 calcRoundTarget(f32* pAngle, bool isRight, f32 roundDiv);
    // pAxis is normalized (Y when zero). *pPos is rWatchPos plus dist along the watch point's offset from the axis through rWPoint,
    // pitched by angleX about the side of world up. False, and *pPos untouched, when the watch point is on the axis.
    bool calcTowerIdealPos(Vec3f* pPos, Vec3f* pAxis, const Vec3f& rWPoint, const Vec3f& rWatchPos, f32 angleX, f32 dist);
    // The water and flying cameras' frame: front toward rWatchPos, the camera's up carried through the turn, side level with rGravUp,
    // never upside down. Returns the camera distance, clamped to [distMin, distMax] unless the camera is on the watch point.
    f32 calcFollowFrame(const Camera* pCamera, const Vec3f& rWatchPos, const Vec3f& rGravUp, f32 distMin, f32 distMax, bool isSideReorthogonalized,
                        Vec3f* pFront, Vec3f* pUp, Vec3f* pSide);
    // *pCount (0-60) counts frames the line rPos to rPos + rLine hits the map while the target moves more than 5 a frame;
    // *pLastMoveDir keeps the last such move. Returns the turn rate toward *pLastMoveDir.
    f32 updateMoveCollision(s32* pCount, Vec3f* pLastMoveDir, const CameraTargetObj* pTarget, const Vec3f& rPos, const Vec3f& rLine);
    void arrangeDistanceByFovy(Camera*, Vec3f, f32);
    void arrangeDistanceByPushAndPull(Camera*, f32, f32);
    void calcSafePose(CameraMan*, Camera*);
};  // namespace CameraLocalUtil
