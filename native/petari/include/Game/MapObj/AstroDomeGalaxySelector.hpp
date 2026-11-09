#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AstroDomeCameraController;
class GalaxyConfirmLayout;
class GalaxySelectBackButton;
class GalaxySelectInfo;
class MiniatureGalaxy;

class AstroDomeGalaxySelector : public LiveActor {
public:
    AstroDomeGalaxySelector(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void showGalaxyInfo(const MiniatureGalaxy*);
    bool tryStartLectureDemo(const Nerve*);

    void exeGalaxySelectStart();
    void exeGalaxySelect();
    void exeGalaxySelectCancel();
    void exeGalaxyConfirmStart();
    void exeGalaxyConfirm();
    void exeGalaxyConfirmCancel();
    void exeDemoJumpOut();
    void exeDemoDomeLecture();
    void exeGalaxyMoveLecture();
    void exeWaitStartDemo();

    AstroDomeCameraController* mCameraController;
    GalaxySelectBackButton* mBackButton;
    GalaxySelectInfo* mInfo;
    GalaxyConfirmLayout* mConfirmLayout;
    bool mIsBackButtonPointed;
};
