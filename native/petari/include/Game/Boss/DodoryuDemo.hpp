#pragma once

#include "Game/Boss/DodoryuStateBase.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"

class DodoryuDemoOpening : public DodoryuStateBase {
public:
    DodoryuDemoOpening(Dodoryu* pHost, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    virtual void start() override;
    virtual void end() override;

    bool tryFinish();
    void turnToRabbit();

    void exeDemoWait();
    void exeUnderground();
    void exeUndergroundHide();
    void exeAppear();
    void exeWalk();
    void exeHideReady();
    void exeHide();

    ActorCameraInfo mActorCameraInfo;
};

class DodoryuDemoAppear : public DodoryuStateBase {
public:
    DodoryuDemoAppear(Dodoryu* pHost, const char* pName);

    virtual void init(const TableIter& rIter) override;

    virtual void start() override;
    virtual void end() override;

    void exeDemoWait();
    void exePlay();

    ActorCameraInfo mActorCameraInfo;
};

class DodoryuDemoAngry : public DodoryuStateBase {
public:
    DodoryuDemoAngry(Dodoryu* pHost, const char* pName);

    virtual void init(const TableIter& rIter) override;

    virtual void start() override;
    virtual void end() override;

    void exeDemoWait();
    void exePlay();

    ActorCameraInfo mActorCameraInfo;
};

class DodoryuDemoDown : public DodoryuStateBase {
public:
    DodoryuDemoDown(Dodoryu* pHost, const char* pName);

    virtual void init(const TableIter& rIter) override;

    virtual void start() override;
    virtual void end() override;

    void exeDemoWait();
    void exePlay();

    ActorCameraInfo mActorCameraInfo;
};
