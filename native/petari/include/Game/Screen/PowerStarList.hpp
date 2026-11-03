#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;
class GalaxyStatusAccessor;
class SysInfoWindow;

class PowerStarList : public LayoutActor {
public:
    class Separator {
    public:
        /// @brief Creates a new `Separator`.
        Separator();

        f32 _0;
        TVec2f _4;
    };

    /// @brief Creates a new `PowerStarList`.
    PowerStarList();

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void appear();
    virtual void control();

    bool isAppearing() const;
    ButtonPaneController* createButtonController(const char*, const char*);
    void resetButtonAll();
    void updateButtonAppearance();
    void updateList(s32, bool);
    char16_t* makeGalaxyNameText(char16_t*, const GalaxyStatusAccessor*, bool) const;
    char16_t* makeStarNumText(char16_t*, const GalaxyStatusAccessor*, bool) const;
    char16_t* makeCoinText(char16_t*, const GalaxyStatusAccessor*, bool) const;
    char16_t* makeCrownText(char16_t*, const GalaxyStatusAccessor*, bool) const;
    char16_t* makeRaceNameText(char16_t*, int, bool) const;
    char16_t* makeRaceTimeText(char16_t*, int, bool) const;
    void startScrollAnimNext(bool);
    void startScrollAnimPrev();
    bool tryShowSeparator(s32, f32);
    void setSeparatorPaneSize();
    

    void exeAppear();
    void exeWait();
    void exeDisappear();
    void exePageNext();
    void exePagePrev();

private:
    ButtonPaneController* mArrowUpButtonCtrl;
    ButtonPaneController* mArrowDownButtonCtrl;
    SysInfoWindow* mSysInfoWindow;
    s32 mPageNo;
    s32 mPageNum;
    Separator* mSeparatorArray;
};
