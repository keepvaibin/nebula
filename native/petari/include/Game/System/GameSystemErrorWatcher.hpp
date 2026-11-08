#pragma once

#include "Game/Screen/ErrorMessageWindow.hpp"
#include "Game/System/NerveExecutor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

class BatteryLayout;

class GameSystemErrorWatcher : public NerveExecutor {
public:
    struct Message {
        const char* mId;
        ErrorMessageWindow::MessageType mType;
    };

    GameSystemErrorWatcher();

    void initAfterResourceLoaded();
    void movement();
    void draw() const;
    bool isWarning() const;
    bool setPermissionUpdateInputStatus(bool);
    void exeNoError();
    void exeErrorWindowIn();
    void exeErrorWindowDisplay();
    void exeErrorWindowOut();
    void updateInputStatus();
    const Message* getProperMessageId() const;
    const Ui::TexMap* getProperErrorTexMap() const;

private:
    ErrorMessageWindow* mWindow;
    const Message* mMessage;
    const Ui::TexMap* mUnplaggedTexMap;
    BatteryLayout* mBatteryLayout;
    s32 mInputStatus;
    s32 mInputBattery;
    bool mPermissionUpdateInputStatus;
};
