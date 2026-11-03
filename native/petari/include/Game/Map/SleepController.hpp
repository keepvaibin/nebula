#pragma once


class JMapInfoIter;
class SwitchEventListener;
class SwitchIdInfo;

class SleepController {
public:
    SleepController(const JMapInfoIter&, SwitchEventListener*);

    void initSync();
    void update();

    SwitchIdInfo* mSwitchIdInfo;
    SwitchEventListener* mSwitchEventListener;
    bool mIsOnSwitchByIdInfo;
};
