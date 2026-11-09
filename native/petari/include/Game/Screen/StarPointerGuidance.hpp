#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class Pane;
}  // namespace Ui

class StarPointerGuidance : public LayoutActor {
public:
    /// @brief Creates a new `StarPointerGuidance`
    /// @param pName A pointer to the null-terminated name of the object.
    StarPointerGuidance(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    void checkRequest2P();
    void checkRequest1P();
    void exeAppear1P();
    void exeWait1P();
    void exeEnd1P();
    void exeAppearBlueStarGuide();
    void exeWaitBlueStarGuide();
    void exeEndBlueStarGuide();
    void exeAppear2P();
    void exeWait2P();
    void exeEnd2P();
    void exeEndWait();
    s32 exeAppearCore(Spine*, const char*, u32, const Nerve*);
    s32 exeWaitCore(Spine*, const char*, u32);
    s32 exeEndCore(Spine*, const char*, u32);
    bool request1PGuidance(const char*, bool);
    void tryResetTimeout();
    bool isExistGuidanceOrFrame();
    bool isExistGuidance();
    bool isExistFrame1P();
    bool isExistFrame2P();
    void deactive();

    void active() {
        mIsActive = true;
    }

    Spine* mSpineFrame1P;
    Spine* mSpineGuidance;
    Spine* mSpineFrame2P;
    s32 mOutScreenTime;
    s32 mRequestTime1P;
    s32 mRequestTime2P;
    const char16_t* mGuidanceMessage;
    const char16_t* mPrevGuidanceMessage;
    bool mIsActiveRequest1P;
    bool mIsActiveRequest2P;
    bool mIsActive;
    bool mIsGuidanceDisabled;
    u32 mTextLineNumGuidance;
    u32 mTextLineNum1P;
    u32 mTextLineNum2P;
    bool mIsTimeOutEnabled;
    // The three frames, bound in init: deactive hides them every frame the pointer is off.
    Ui::Pane* mGuidePane = nullptr;
    Ui::Pane* mFrame1PPane = nullptr;
    Ui::Pane* mFrame2PPane = nullptr;
};
