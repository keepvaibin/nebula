#pragma once

#include "Game/Screen/ErrorMessageWindow.hpp"
#include "Game/System/NerveExecutor.hpp"

namespace nw4r {
    namespace lyt {
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

class BatteryLayout;

class GameSystemErrorWatcher : public NerveExecutor {
public:
    struct Message {
        const char* mId;
        ErrorMessageWindow::MessageType mType;
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
    ErrorMessageWindow* mWindow;
    const Message* mMessage;
    const nw4r::lyt::TexMap* mUnplaggedTexMap;
    BatteryLayout* mBatteryLayout;
    s32 mInputStatus;
    s32 mInputBattery;
    bool mPermissionUpdateInputStatus;
};
