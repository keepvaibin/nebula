#pragma once

#include <nebula_geometry.hpp>

class Joint;
class Model;
class ModelData;
class LiveActor;

namespace MR {
    Joint* getJoint(Model*, const char*);
    Joint* getJoint(const LiveActor*, const char*);
    Joint* getJoint(const LiveActor*, u32);
    MtxPtr getJointMtx(Model*, const char*);
    MtxPtr getJointMtx(const LiveActor*, const char*);
    MtxPtr getJointMtx(const LiveActor*, int);
    u32 getJointIndex(const LiveActor*, const char*);
    const char* getJointName(const LiveActor*, int);
    s32 getJointNum(const LiveActor*);
    bool isExistJoint(const LiveActor*, const char*);
    void copyJointPos(Model*, const char*, Vec3f*);
    void copyJointPos(const LiveActor*, const char*, Vec3f*);
    void copyJointPos(Model*, int, Vec3f*);
    void copyJointPos(const LiveActor*, int, Vec3f*);
    void copyJointScale(const LiveActor*, const char*, Vec3f*);
    void hideJoint(Joint*);
    void hideJoint(Model*, const char*);
    void hideJoint(const LiveActor*, const char*);
    void hideJointAndChildren(Joint*);
    void hideJointAndChildren(Model*, const char*);
    void hideJointAndChildren(const LiveActor*, const char*);
    void showJoint(Joint*);
    void showJoint(Model*, const char*);
    void showJoint(const LiveActor*, const char*);
    void showJointAndChildren(Joint*);
    void showJointAndChildren(Model*, const char*);
    void showJointAndChildren(const LiveActor*, const char*);
    void showJointAndChildrenOn(Model*, Joint*, bool);
    bool isJointUnder(const Joint*, const Joint*);
    f32 getJointTransX(const Joint*);
    f32 getJointTransY(const Joint*);
    f32 getJointTransZ(const Joint*);
    Joint* searchChildJoint(Joint* pRoot, Joint* pJoint);
    Joint* getParentJoint(ModelData*, Joint*);
    Joint* getParentJoint(const LiveActor*, Joint*);

    inline Vec3f getJointPos(const LiveActor* pActor, const char* pJointName) {
        Vec3f jointPos;
        copyJointPos(pActor, pJointName, &jointPos);
        return jointPos;
    }

};  // namespace MR
