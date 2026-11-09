#pragma once

#include "Game/Screen/LayoutActor.hpp"

class IconAButton;

class InformationMessage : public LayoutActor {
public:
    InformationMessage();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;

    void appearWithButtonLayout();
    void disappear();
    void setMessage(const char* pMessageId);
    void setMessage(const char16_t* pMessage);
    void setReplaceString(const char16_t* pString, s32 param2);
    void exeAppear();
    void exeWait();
    void exeDisappear();

    void setCenter(bool isCenter) {
        mIsCenter = isCenter;
    }

private:
    IconAButton* mAButtonIcon;
    bool mIsCenter;
};
