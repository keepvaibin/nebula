#include "Game/Util/Array.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObjListExecutor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/System/LumaCore.hpp"

// NameObjExecuteInfo keeps each list type in an s8.
static_assert(MR::MovementType_ShadowControllerHolder <= 127 && MR::CalcAnimType_AnimParticleIgnorePause <= 127 && MR::DrawType_CameraCover <= 127 &&
                  MR::DrawBufferType_MirrorMapObj <= 127,
              "list types fit an s8");

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
    // Made after the scene's connecting (CoinHolder::takeDeadCoin, a mod's attached model): the scene's one
    // initConnectting is past, so the object takes its step now; its first connect request is then an ordinary one.
    if (mConnectingDone) {
        mExecuteArray[mExecuteArraySize].initConnectting();
    }
    mExecuteArraySize++;
}

void NameObjExecuteHolder::initConnectting() {
    for (s32 i = 0; i < mExecuteArraySize; i++) {
        mExecuteArray[i].initConnectting();
    }
    mConnectingDone = true;
}

// The lists grow in the holder's arena: a request made in a frame (the permanent arena current) stays the scene's.
void NameObjExecuteHolder::push(PendingList* pList, s32 index) {
    const s32 word = index >> 6;
    if (word >= pList->mWords) {
        s32 words = pList->mWords > 0 ? pList->mWords : 16;
        while (words <= word) {
            words *= 2;
        }
        NebulaArena* pArena = NebulaArena::findFromRoot(this);
        u64* pBits = new (pArena, 0) u64[words];
        u64* pRunBits = new (pArena, 0) u64[words];
        for (s32 i = 0; i < words; i++) {
            pBits[i] = i < pList->mWords ? pList->mBits[i] : 0;
            pRunBits[i] = i < pList->mWords ? pList->mRunBits[i] : 0;
        }
        delete[] pList->mBits;
        delete[] pList->mRunBits;
        pList->mBits = pBits;
        pList->mRunBits = pRunBits;
        pList->mWords = words;
    }

    const u64 bit = 1ull << (index & 63);
    if ((pList->mBits[word] & bit) == 0) {
        pList->mBits[word] |= bit;
        pList->mCount++;
    }
}

// An index asked for twice runs once: a run after the first finds nothing pending.
void NameObjExecuteHolder::run(PendingList* pList, void (NameObjExecuteInfo::*pExecute)()) {
    if (pList->mCount == 0) {
        return;
    }

    u64* pTaken = pList->mBits;
    pList->mBits = pList->mRunBits;
    pList->mRunBits = pTaken;
    pList->mCount = 0;

    // A push while running may grow both sets: read them through pList.
    const s32 words = pList->mWords;
    for (s32 word = 0; word < words; word++) {
        u64 bits = pList->mRunBits[word];
        if (bits == 0) {
            continue;
        }
        pList->mRunBits[word] = 0;
        while (bits != 0) {
            const s32 index = (word << 6) + __builtin_ctzll(bits);
            bits &= bits - 1;
            (mExecuteArray[index].*pExecute)();
        }
    }
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

// Each acts only on a pending connect, so the pending run that comes later finds nothing for it.
void NameObjExecuteHolder::connectNow(NameObj* pObj) {
    NameObjExecuteInfo* pInfo = getConnectToSceneInfo(pObj);
    if (pInfo != nullptr) {
        pInfo->executeRequirementConnectMovement();
        pInfo->executeRequirementConnectDraw();
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

    void connectNow(NameObj* pObj) {
        ::getNameObjExecuteHolder()->connectNow(pObj);
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
      mConnectMovement(), mDisconnectMovement(), mConnectDraw(), mDisconnectDraw(), mConnectingDone(false) {
    lumacore::setClass(this, lumacore::kClassId< NameObjExecuteHolder >);
}
