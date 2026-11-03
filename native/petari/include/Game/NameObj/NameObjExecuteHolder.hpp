#pragma once

#include "Game/NameObj/NameObj.hpp"

class LiveActor;

class NameObjExecuteInfo {
    friend class NameObjExecuteHolder;

public:
    NameObjExecuteInfo();
    bool isDrawBufferOnly() const { return mDrawType == -1 && mDrawBufferType != -1; }
    s32 getMovementType() const { return mMovementType; }

    void setConnectInfo(NameObj*, int, int, int, int);
    void initConnectting();
    void requestConnect(u8*);
    void requestDisconnect(u8*, bool);
    void executeRequirementConnectMovement();
    void executeRequirementDisconnectMovement();
    void executeRequirementConnectDraw();
    void executeRequirementDisconnectDraw();
    void executeRequirementDisconnectDrawDelay();
    void requestMovementOn(int);
    void requestMovementOff(int);
    void findLightInfo() const;
    void connectToScene();
    void disconnectToScene();
    void connectToDraw();
    void disconnectToDraw();

private:
    NameObj* mExecutedObj;
    s8 _4;
    s8 _5;
    s8 mMovementType;
    s8 mCalcAnimType;
    s8 mDrawType;
    s8 mDrawBufferType;
    s16 _A;
};

class NameObjExecuteHolder : public NameObj {
public:
    NameObjExecuteHolder(int);

    void registerActor(NameObj*, int, int, int, int);
    void initConnectting();
    void connectToScene(NameObj*);
    void connectToDraw(NameObj*);
    void disconnectToScene(NameObj*);
    void disconnectToDraw(NameObj*);
    bool isConnectToDraw(const NameObj*) const;
    void executeRequirementConnectMovement();
    void executeRequirementDisconnectMovement();
    void executeRequirementConnectDraw();
    void executeRequirementDisconnectDraw();
    void executeRequirementDisconnectDrawDelay();
    void requestMovementOn(int);
    void requestMovementOff(int);
    NameObjExecuteInfo* getConnectToSceneInfo(const NameObj*) const;

private:
    NameObjExecuteInfo* mExecuteArray;
    s32 mExecuteArrayMaxSize;
    s32 mExecuteArraySize;
    bool _18;
    bool _19;
    bool _1A;
    bool _1B;
    bool _1C;
};

namespace MR {
    void registerNameObjToExecuteHolder(NameObj*, int, int, int, int);
    void initConnectting();
    void connectToSceneTemporarily(NameObj*);
    void disconnectToSceneTemporarily(NameObj*);
    void connectToDrawTemporarily(NameObj*);
    void disconnectToDrawTemporarily(NameObj*);
    bool isConnectToDrawTemporarily(const NameObj*);
    bool isDrawnFarWhenClipped(const NameObj*, bool hasRun);
    bool isMovementPlanet(const NameObj*);
    void executeRequirementConnectMovement();
    void executeRequirementDisconnectMovement();
    void executeRequirementConnectDraw();
    void executeRequirementDisconnectDraw();
    void executeRequirementDisconnectDrawDelay();
    void requestMovementOnWithCategory(int);
    void requestMovementOffWithCategory(int);
    void findActorLightInfo(const LiveActor*);
};  // namespace MR
