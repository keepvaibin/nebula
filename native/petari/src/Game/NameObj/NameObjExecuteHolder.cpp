#include "Game/Util/Array.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObjListExecutor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"

namespace {
    NameObjExecuteHolder* getNameObjExecuteHolder() {
        return MR::getSceneObj< NameObjExecuteHolder >(SceneObj_NameObjExecuteHolder);
    }
};  // namespace

NameObjExecuteInfo::NameObjExecuteInfo()
    : mExecutedObj(), mMovementState(NameObjConnect_None), mDrawState(NameObjConnect_None), mMovementType(-1), mCalcAnimType(-1), mDrawType(-1),
      mDrawBufferType(-1), mDrawBufferIndex(-1) {
}

void NameObjExecuteInfo::setConnectInfo(NameObj* pObj, int movementType, int calcAnimType, int drawBufferType, int drawType) {
    mMovementState = NameObjConnect_UnwantedBeforeInit;
    mExecutedObj = pObj;
    mDrawState = NameObjConnect_UnwantedBeforeInit;
    mMovementType = movementType;
    mCalcAnimType = calcAnimType;
    mDrawBufferType = drawBufferType;
    mDrawType = drawType;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (drawBufferType != -1) {
        mDrawBufferIndex = pListExecutor->registerDrawBuffer(static_cast< LiveActor* >(pObj), drawBufferType);
    }

    if (movementType != -1) {
        pListExecutor->incrementCheckMovement(pObj, movementType);
    }

    if (calcAnimType != -1) {
        pListExecutor->incrementCheckCalcAnim(pObj, calcAnimType);
    }

    if (drawType != -1) {
        pListExecutor->incrementCheckDraw(pObj, drawType);
    }
}

void NameObjExecuteInfo::initConnectting() {
    if (mMovementState == NameObjConnect_WantedBeforeInit) {
        connectToScene();
    } else {
        mMovementState = NameObjConnect_Disconnected;
    }

    if (mDrawState == NameObjConnect_WantedBeforeInit) {
        connectToDraw();
    } else {
        mDrawState = NameObjConnect_Disconnected;
    }
}

bool NameObjExecuteInfo::requestConnect(s8* pState) {
    switch (*pState) {
    case NameObjConnect_UnwantedBeforeInit:
        *pState = NameObjConnect_WantedBeforeInit;
        return false;
    case NameObjConnect_Disconnected:
        *pState = NameObjConnect_ConnectPending;
        return true;
    case NameObjConnect_DisconnectPending:
        *pState = NameObjConnect_Connected;
        return false;
    default:
        return false;
    }
}

bool NameObjExecuteInfo::requestDisconnect(s8* pState) {
    switch (*pState) {
    case NameObjConnect_WantedBeforeInit:
        *pState = NameObjConnect_UnwantedBeforeInit;
        return false;
    case NameObjConnect_Connected:
        *pState = NameObjConnect_DisconnectPending;
        return true;
    case NameObjConnect_ConnectPending:
        *pState = NameObjConnect_Disconnected;
        return false;
    default:
        return false;
    }
}

void NameObjExecuteInfo::executeRequirementConnectMovement() {
    if (mMovementState == NameObjConnect_ConnectPending) {
        connectToScene();
    }
}

void NameObjExecuteInfo::executeRequirementDisconnectMovement() {
    if (mMovementState == NameObjConnect_DisconnectPending) {
        disconnectToScene();
    }
}

void NameObjExecuteInfo::executeRequirementConnectDraw() {
    if (mDrawState == NameObjConnect_ConnectPending) {
        connectToDraw();
    }
}

void NameObjExecuteInfo::executeRequirementDisconnectDraw() {
    if (mDrawState == NameObjConnect_DisconnectPending) {
        disconnectToDraw();
    }
}

void NameObjExecuteInfo::requestMovementOn(int movementType) {
    if (mMovementType != static_cast< s8 >(movementType)) {
        return;
    }

    MR::requestMovementOn(mExecutedObj);
}

void NameObjExecuteInfo::requestMovementOff(int movementType) {
    if (mMovementType != static_cast< s8 >(movementType)) {
        return;
    }

    MR::requestMovementOff(mExecutedObj);
}

void NameObjExecuteInfo::findLightInfo() const {
    SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor()->findLightInfo(static_cast< LiveActor* >(mExecutedObj),
                                                                                                    mDrawBufferType, mDrawBufferIndex);
}

void NameObjExecuteInfo::connectToScene() {
    mMovementState = NameObjConnect_Connected;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mMovementType != -1) {
        pListExecutor->addToMovement(mExecutedObj, mMovementType);
    }

    if (mCalcAnimType != -1) {
        pListExecutor->addToCalcAnim(mExecutedObj, mCalcAnimType);
    }
}

void NameObjExecuteInfo::disconnectToScene() {
    mMovementState = NameObjConnect_Disconnected;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mMovementType != -1) {
        pListExecutor->removeToMovement(mExecutedObj, mMovementType);
    }

    if (mCalcAnimType != -1) {
        pListExecutor->removeToCalcAnim(mExecutedObj, mCalcAnimType);
    }
}

void NameObjExecuteInfo::connectToDraw() {
    mDrawState = NameObjConnect_Connected;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mDrawType != -1) {
        pListExecutor->addToDraw(mExecutedObj, mDrawType);
    }

    if (mDrawBufferType != -1) {
        pListExecutor->addToDrawBuffer(static_cast< LiveActor* >(mExecutedObj), mDrawBufferType, mDrawBufferIndex);
    }
}

void NameObjExecuteInfo::disconnectToDraw() {
    mDrawState = NameObjConnect_Disconnected;
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    if (mDrawType != -1) {
        pListExecutor->removeToDraw(mExecutedObj, mDrawType);
    }

    if (mDrawBufferType != -1) {
        pListExecutor->removeToDrawBuffer(static_cast< LiveActor* >(mExecutedObj), mDrawBufferType, mDrawBufferIndex);
    }
}

void NameObjExecuteHolder::registerActor(NameObj* pObj, int movementType, int calcAnimType, int drawBufferType, int drawType) {
    if (mExecuteArraySize >= mExecuteArrayMaxSize) {
        mExecuteArray = MR::growArray(mExecuteArray, mExecuteArrayMaxSize);
        mExecuteArrayMaxSize *= 2;
    }

    mExecuteArray[mExecuteArraySize].setConnectInfo(pObj, movementType, calcAnimType, drawBufferType, drawType);
    pObj->mExecutorIdx = mExecuteArraySize;
    mExecuteArraySize++;
}

void NameObjExecuteHolder::initConnectting() {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].initConnectting();
    }
}

void NameObjExecuteHolder::push(PendingList* pList, s32 index) {
    if (pList->mCount >= pList->mCapacity) {
        const s32 grown = pList->mCapacity > 0 ? pList->mCapacity * 2 : 64;
        s32* pIndices = new s32[grown];
        for (s32 i = 0; i < pList->mCount; i++) {
            pIndices[i] = pList->mIndices[i];
        }
        delete[] pList->mIndices;
        pList->mIndices = pIndices;
        pList->mCapacity = grown;
    }
    pList->mIndices[pList->mCount++] = index;
}

// Requests made while the list runs wait for the next run.
void NameObjExecuteHolder::run(PendingList* pList, void (NameObjExecuteInfo::*pExecute)()) {
    const s32 count = pList->mCount;
    if (count == 0) {
        return;
    }

    s32* pIndices = pList->mIndices;
    for (s32 i = 1; i < count; i++) {
        const s32 index = pIndices[i];
        s32 j = i;
        for (; j > 0 && pIndices[j - 1] > index; j--) {
            pIndices[j] = pIndices[j - 1];
        }
        pIndices[j] = index;
    }

    for (s32 i = 0; i < count; i++) {
        (mExecuteArray[pList->mIndices[i]].*pExecute)();
    }

    const s32 later = pList->mCount - count;
    for (s32 i = 0; i < later; i++) {
        pList->mIndices[i] = pList->mIndices[count + i];
    }
    pList->mCount = later;
}

void NameObjExecuteHolder::connectToScene(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo != nullptr && NameObjExecuteInfo::requestConnect(&pInfo->mMovementState)) {
        push(&mConnectMovement, pObj->mExecutorIdx);
    }
}

void NameObjExecuteHolder::connectToDraw(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo != nullptr && NameObjExecuteInfo::requestConnect(&pInfo->mDrawState)) {
        push(&mConnectDraw, pObj->mExecutorIdx);
    }
}

void NameObjExecuteHolder::disconnectToScene(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo != nullptr && NameObjExecuteInfo::requestDisconnect(&pInfo->mMovementState)) {
        push(&mDisconnectMovement, pObj->mExecutorIdx);
    }
}

void NameObjExecuteHolder::disconnectToDraw(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);

    if (pInfo != nullptr && NameObjExecuteInfo::requestDisconnect(&pInfo->mDrawState)) {
        push(&mDisconnectDraw, pObj->mExecutorIdx);
    }
}

bool NameObjExecuteHolder::isConnectToDraw(const NameObj* pObj) const {
    const NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);
    return pInfo != nullptr && pInfo->mDrawState == NameObjConnect_Connected;
}

void NameObjExecuteHolder::executeRequirementConnectMovement() {
    run(&mConnectMovement, &NameObjExecuteInfo::executeRequirementConnectMovement);
}

void NameObjExecuteHolder::executeRequirementDisconnectMovement() {
    run(&mDisconnectMovement, &NameObjExecuteInfo::executeRequirementDisconnectMovement);
}

void NameObjExecuteHolder::executeRequirementConnectDraw() {
    run(&mConnectDraw, &NameObjExecuteInfo::executeRequirementConnectDraw);
}

void NameObjExecuteHolder::executeRequirementDisconnectDraw() {
    run(&mDisconnectDraw, &NameObjExecuteInfo::executeRequirementDisconnectDraw);
}

void NameObjExecuteHolder::requestMovementOn(int movementType) {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].requestMovementOn(movementType);
    }
}

void NameObjExecuteHolder::requestMovementOff(int movementType) {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].requestMovementOff(movementType);
    }
}

NameObjExecuteInfo* NameObjExecuteHolder::getConnectToSceneInfo(const NameObj* pObj) const {
    if (pObj->mExecutorIdx >= 0) {
        return &mExecuteArray[pObj->mExecutorIdx];
    }

    return nullptr;
}

namespace MR {
    void registerNameObjToExecuteHolder(NameObj* pObj, int movementType, int calcAnimType, int drawBufferType, int drawType) {
        ::getNameObjExecuteHolder()->registerActor(pObj, movementType, calcAnimType, drawBufferType, drawType);
    }

    void initConnectting() {
        ::getNameObjExecuteHolder()->initConnectting();
    }

    void connectToSceneTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->connectToScene(pObj);
    }

    void disconnectToSceneTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->disconnectToScene(pObj);
    }

    void connectToDrawTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->connectToDraw(pObj);
    }

    void disconnectToDrawTemporarily(NameObj* pObj) {
        ::getNameObjExecuteHolder()->disconnectToDraw(pObj);
    }

    bool isConnectToDrawTemporarily(const NameObj* pObj) {
        return ::getNameObjExecuteHolder()->isConnectToDraw(pObj);
    }

    // No draw code of its own: scenery at any time, the rest (hasRun) once it has run.
    bool isDrawnFarWhenClipped(const NameObj* pObj, bool hasRun) {
        const NameObjExecuteInfo* pInfo = ::getNameObjExecuteHolder()->getConnectToSceneInfo(pObj);
        if (pInfo == nullptr || !pInfo->isDrawBufferOnly()) {
            return false;
        }
        switch (pInfo->getMovementType()) {
        case MR::MovementType_ClippedMapParts:
        case MR::MovementType_Planet:
        case MR::MovementType_CollisionMapObj:
        case MR::MovementType_Environment:
        case MR::MovementType_MapObj:
        case MR::MovementType_MapObjDecoration:
        case MR::MovementType_Sky:
            return true;
        default:
            return hasRun;
        }
    }

    bool isMovementPlanet(const NameObj* pObj) {
        const NameObjExecuteInfo* pInfo = ::getNameObjExecuteHolder()->getConnectToSceneInfo(pObj);
        return pInfo != nullptr && pInfo->getMovementType() == MR::MovementType_Planet;
    }

    void executeRequirementConnectMovement() {
        ::getNameObjExecuteHolder()->executeRequirementConnectMovement();
    }

    void executeRequirementDisconnectMovement() {
        ::getNameObjExecuteHolder()->executeRequirementDisconnectMovement();
    }

    void executeRequirementConnectDraw() {
        ::getNameObjExecuteHolder()->executeRequirementConnectDraw();
    }

    void executeRequirementDisconnectDraw() {
        ::getNameObjExecuteHolder()->executeRequirementDisconnectDraw();
    }

    void requestMovementOnWithCategory(int category) {
        ::getNameObjExecuteHolder()->requestMovementOn(category);
    }

    void requestMovementOffWithCategory(int category) {
        ::getNameObjExecuteHolder()->requestMovementOff(category);
    }

    void findActorLightInfo(const LiveActor* pActor) {
        ::getNameObjExecuteHolder()->getConnectToSceneInfo(pActor)->findLightInfo();
    }
};  // namespace MR

NameObjExecuteHolder::NameObjExecuteHolder(int size)
    : NameObj("connectToScene情報保持"), mExecuteArray(new NameObjExecuteInfo[size]), mExecuteArrayMaxSize(size), mExecuteArraySize(),
      mConnectMovement(), mDisconnectMovement(), mConnectDraw(), mDisconnectDraw() {
}
