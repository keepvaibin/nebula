#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class Joint;
class Model;
class LiveActor;

class JointController;

struct JointControllerInfo {
    JointController* mController;
    Joint* mJoint;
};

class JointController {
public:
    JointController();

    virtual bool calcJointMatrix(Pos3f*, const JointControllerInfo&);
    virtual bool calcJointMatrixAfterChild(Pos3f*, const JointControllerInfo&);

    void registerCallBack();
    void calcJointMatrixAndSetSystem(Joint*);
    void calcJointMatrixAfterChildAndSetSystem(Joint*);
    static int staticCallBack(Joint*, int);

    Model* mModel;
    Joint* mJoint;
};

template < typename T >
class JointControlDelegator : public JointController {
public:
    typedef bool (T::*func)(Pos3f*, const JointControllerInfo&);

    inline JointControlDelegator(T* pHost, func calcFunc, func calcAfterChild) : JointController() {
        mHost = pHost;
        mMtxCalcFunc = calcFunc;
        mMtxCalcAfterChildFunc = calcAfterChild;
    }

    inline JointControlDelegator(func calcFunc, T* pHost, func calcAfterChild) : JointController() {
        mHost = pHost;
        mMtxCalcFunc = calcAfterChild;
        mMtxCalcAfterChildFunc = calcFunc;
    }

    virtual ~JointControlDelegator() {
    }

    virtual bool calcJointMatrix(Pos3f* pMtx, const JointControllerInfo& rInfo) {
        if (mMtxCalcFunc != nullptr) {
            return (mHost->*mMtxCalcFunc)(pMtx, rInfo);
        } else {
            return false;
        }
    }

    virtual bool calcJointMatrixAfterChild(Pos3f* pMtx, const JointControllerInfo& rInfo) {
        if (mMtxCalcAfterChildFunc != nullptr) {
            return (mHost->*mMtxCalcAfterChildFunc)(pMtx, rInfo);
        } else {
            return false;
        }
    }

    T* mHost;
    func mMtxCalcFunc;
    func mMtxCalcAfterChildFunc;
};

namespace MR {
    void setJointControllerParam(JointController*, const LiveActor*, const char*);
    void setJointControllerParam(JointController*, const LiveActor*, u32);

    template < class T >
    JointController* createJointController(T* pHost, const LiveActor* pActor, const char* pJointName,
                                           bool (T::*calcFunc)(Pos3f*, const JointControllerInfo&),
                                           bool (T::*calcChild)(Pos3f*, const JointControllerInfo&)) {
        JointControlDelegator< T >* pController = new JointControlDelegator< T >(pHost, calcFunc, calcChild);
        setJointControllerParam(pController, pActor, pJointName);
        return pController;
    }

    template < class T >
    JointController* createJointController(T* pHost, const LiveActor* pActor, u32 jointIndex,
                                           bool (T::*calcFunc)(Pos3f*, const JointControllerInfo&),
                                           bool (T::*calcChild)(Pos3f*, const JointControllerInfo&)) {
        JointControlDelegator< T >* controller = new JointControlDelegator< T >(pHost, calcFunc, calcChild);
        setJointControllerParam(controller, pActor, jointIndex);
        return controller;
    }

    template < class T >
    JointControlDelegator< T >* createJointDelegator(T* pHost, const LiveActor* pActor, bool (T::*calcFunc)(Pos3f*, const JointControllerInfo&),
                                                     bool (T::*calcChild)(Pos3f*, const JointControllerInfo&), const char* pName) {
        JointControlDelegator< T >* delegator = new JointControlDelegator< T >(pHost, calcFunc, calcChild);
        setJointControllerParam(delegator, pActor, pName);
        return delegator;
    }

    template < class T >
    JointControlDelegator< T >* createJointDelegatorWithNullChildFunc(T* pHost, bool (T::*calcFunc)(Pos3f*, const JointControllerInfo&),
                                                                      const char* pName) {
        JointControlDelegator< T >* delegator = new JointControlDelegator< T >(pHost, calcFunc, 0);
        setJointControllerParam(delegator, pHost, pName);
        return delegator;
    }

    template < class T >
    JointControlDelegator< T >* createJointDelegatorWithNullMtxFunc(T* pHost, bool (T::*calcFunc)(Pos3f*, const JointControllerInfo&),
                                                                    const char* pName) {
        JointControlDelegator< T >* delegator = new JointControlDelegator< T >(calcFunc, pHost, 0);
        setJointControllerParam(delegator, pHost, pName);
        return delegator;
    }

    template < class T >
    JointController* createJointController(T* pHost, const char* pName, bool (T::*calcFunc)(Pos3f*, const JointControllerInfo&)) {
        JointControlDelegator< T >* delegator = new JointControlDelegator< T >(pHost, calcFunc, 0);
        setJointControllerParam(delegator, pHost, pName);
        return delegator;
    }
};  // namespace MR
