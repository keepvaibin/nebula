#pragma once

#include "Game/Screen/ErrorMessageWindow.hpp"
#include "Game/System/NerveExecutor.hpp"

namespace nw4r {
    namespace lyt {
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

class BatteryLayout;
class TriggerChecker;

class GameSystemErrorWatcher : public NerveExecutor {
public:
    struct Message {
        /* 0x00 */ const char* mId;
        /* 0x04 */ ErrorMessageWindow::MessageType mType;
    };

    /// @brief Creates a new `GameSystemErrorWatcher`.
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
    const nw4r::lyt::TexMap* getProperErrorTexMap() const;

private:
    /* 0x08 */ ErrorMessageWindow* mWindow;
    /* 0x0C */ const Message* mMessage;
    /* 0x10 */ const nw4r::lyt::TexMap* mUnplaggedTexMap;
    /* 0x14 */ BatteryLayout* mBatteryLayout;
    /* 0x1C */ TriggerChecker* _1C;
    /* 0x20 */ s32 mInputStatus;
    /* 0x24 */ s32 mCounterIgnoreCheckMoveStick;
    /* 0x28 */ s32 mCounterDecideDisconnect;
    /* 0x2C */ s32 mInputBattery;
    /* 0x30 */ bool mPermissionUpdateInputStatus;
};
