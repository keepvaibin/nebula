#pragma once

#include "Game/NameObj/NameObj.hpp"

class LiveActor;

// Where an object is in the scene's movement or draw lists. Requests before initConnectting only mark the wish.
enum NameObjConnectState {
    NameObjConnect_None,
    NameObjConnect_WantedBeforeInit,
    NameObjConnect_UnwantedBeforeInit,
    NameObjConnect_Connected,
    NameObjConnect_ConnectPending,
    NameObjConnect_Disconnected,
    NameObjConnect_DisconnectPending,
};

class NameObjExecuteInfo {
    friend class NameObjExecuteHolder;

public:
    NameObjExecuteInfo();
    bool isDrawBufferOnly() const { return mDrawType == -1 && mDrawBufferType != -1; }
    s32 getMovementType() const { return mMovementType; }

    void setConnectInfo(NameObj*, int, int, int, int);
    void initConnectting();
    // True: the request leaves a change pending.
    static bool requestConnect(s8* pState);
    static bool requestDisconnect(s8* pState);
    void executeRequirementConnectMovement();
    void executeRequirementDisconnectMovement();
    void executeRequirementConnectDraw();
    void executeRequirementDisconnectDraw();
    void requestMovementOn(int movementType);
    void requestMovementOff(int movementType);
    void findLightInfo() const;
    void connectToScene();
    void disconnectToScene();
    void connectToDraw();
    void disconnectToDraw();

private:
    NameObj* mExecutedObj;
    s8 mMovementState;  // NameObjConnectState
    s8 mDrawState;      // NameObjConnectState
    s8 mMovementType;
    s8 mCalcAnimType;
    s8 mDrawType;
    s8 mDrawBufferType;
    s32 mDrawBufferIndex;
};

class NameObjExecuteHolder : public NameObj {
public:
    NameObjExecuteHolder(int);

    void registerActor(NameObj*, int, int, int, int);
    void initConnectting();
    void connectToScene(NameObj*);
    void connectToDraw(NameObj*);
    // Its pending connects (movement and draw) now, not at the next movement run's start (which a stopped scene skips).
    void connectNow(NameObj*);
    void disconnectToScene(NameObj*);
    void disconnectToDraw(NameObj*);
    bool isConnectToDraw(const NameObj*) const;
    void executeRequirementConnectMovement();
    void executeRequirementDisconnectMovement();
    void executeRequirementConnectDraw();
    void executeRequirementDisconnectDraw();
    void requestMovementOn(int movementType);
    void requestMovementOff(int movementType);
    NameObjExecuteInfo* getConnectToSceneInfo(const NameObj*) const;

private:
    // Objects with a pending change, one bit per index; run in index order, as the game's full scans did. A run takes
    // mBits and leaves a clear set for requests made while it runs, which wait for the next run.
    struct PendingList {
        u64* mBits;
        u64* mRunBits;
        s32 mWords;
        s32 mCount;  // bits set in mBits
    };

    void push(PendingList*, s32 index);
    void run(PendingList*, void (NameObjExecuteInfo::*pExecute)());

    NameObjExecuteInfo* mExecuteArray;
    s32 mExecuteArrayMaxSize;
    s32 mExecuteArraySize;
    PendingList mConnectMovement;
    PendingList mDisconnectMovement;
    PendingList mConnectDraw;
    PendingList mDisconnectDraw;
    bool mConnectingDone;  // initConnectting has run: an object registered later starts disconnected, not "before init"
};

namespace MR {
    void registerNameObjToExecuteHolder(NameObj*, int, int, int, int);
    void initConnectting();
    void connectToSceneTemporarily(NameObj*);
    void disconnectToSceneTemporarily(NameObj*);
    void connectToDrawTemporarily(NameObj*);
    void connectNow(NameObj*);
    void disconnectToDrawTemporarily(NameObj*);
    bool isConnectToDrawTemporarily(const NameObj*);
    bool isDrawnFarWhenClipped(const NameObj*, bool hasRun);
    bool isMovementPlanet(const NameObj*);
    void executeRequirementConnectMovement();
    void executeRequirementDisconnectMovement();
    void executeRequirementConnectDraw();
    void executeRequirementDisconnectDraw();
    void requestMovementOnWithCategory(int);
    void requestMovementOffWithCategory(int);
    void findActorLightInfo(const LiveActor*);
};  // namespace MR
