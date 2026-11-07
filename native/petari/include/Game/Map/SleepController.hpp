#pragma once


class TableIter;
class SwitchEventListener;
class SwitchIdInfo;

class SleepController {
public:
    SleepController(const TableIter&, SwitchEventListener*);

    void initSync();
    void update();

    SwitchIdInfo* mSwitchIdInfo;
    SwitchEventListener* mSwitchEventListener;
    bool mIsOnSwitchByIdInfo;
};
