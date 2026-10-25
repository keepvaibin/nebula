#pragma once

#include "Game/Screen/LayoutActor.hpp"

typedef void(MessageChangeFunc)(LayoutActor*, const char*, const char*);

class IconAButton;
class YesNoController;
#ifdef TARGET_PC
namespace nw4r {
    namespace lyt {
        class Pane;
    };  // namespace lyt
};  // namespace nw4r
#endif

struct MessageChangeFuncTableEntry {
    MessageChangeFunc* mFuncPtr;
};

class SysInfoWindow : public LayoutActor {
public:
    enum SysInfoWindowType {
        WindowType_Normal,
        WindowType_Mini,
    };

    enum SysInfoExecuteType {
        ExecuteType_Normal,
        ExecuteType_Children,
    };

    enum SysInfoType {
        Type_Key,
        Type_Blocking,
        Type_YesNo,
    };

    enum SysInfoTextPos {
        TextPos_Center,
        TextPos_Bottom,
    };

    enum SysInfoMessageType {
        MessageType_Game,
        MessageType_System,
    };

    SysInfoWindow(SysInfoWindowType, SysInfoExecuteType);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void appear(const char*, SysInfoType, SysInfoTextPos, SysInfoMessageType);
#ifdef TARGET_PC
    void appearYesNoWithText(const wchar_t* pText);
    void decideNo();
    // B answers No (a question the player opened from a menu, as Back closes Nebula's menus). Off by default.
    void setBackAnswersNo(bool on);
#endif
    void disappear();
    void forceKill();
    bool isWait() const;
    bool isSelectedYes() const;
    bool isDisappear() const;
    const char* getLayoutName() const;
    void exeAppear();
    void exeWait();
    void exeDisappear();
    void setYesNoSelectorSE(const char*, const char*, const char*);
    void resetYesNoSelectorSE();
    void setTextBoxArgNumber(s32, s32);
    void setTextBoxArgString(const wchar_t*, s32);

    /* 0x20 */ SysInfoWindowType mWindowType;
    /* 0x24 */ SysInfoType mType;
    /* 0x28 */ YesNoController* mYesNoSelector;
    /* 0x2C */ IconAButton* mIconAButton;
    /* 0x30 */ const char* mTextParentPaneName;
    /* 0x34 */ const char* mWindowParentPaneName;
    /* 0x38 */ bool _38;
#ifdef TARGET_PC
    bool mIsBackAnswersNo;

    // Nebula: the screen dim (the layout's BG pane) is drawn by the window itself. Where the game opens another system
    // window after this one, the caller says so after appear(): the dim then holds through this window's end and a few
    // frames after, and the next window takes it over at its level, so a chain of windows (the save's question,
    // Saving..., saved, the pause menu's question) never lifts it in between. Otherwise it fades with the window.
    enum DimHandOver {
        DimHandOver_None = 0,
        DimHandOver_Yes = 1,     // when the answer is Yes
        DimHandOver_No = 2,      // when the answer is No
        DimHandOver_Always = 3,
    };
    void setDimHandOver(u8 when);

private:
    void updateDim();
    void drawDim() const;
    f32 getOwnDim() const;
    void shapeFade();

    nw4r::lyt::Pane* mDimPane;
    f32 mDim;          // drawn this frame, 0 to 1
    f32 mDimFloor;     // taken over from the window before
    s32 mDimLinger;    // frames it still holds after the window closed
    u32 mDimEpoch;     // the hand-over this window holds the dim under
    u8 mDimHandOver;   // DimHandOver, from the caller
    bool mDimHanding;  // closing towards a window that takes the dim
    // The question window's fading parent, panel and drop shadow (shapeFade).
    nw4r::lyt::Pane* mFadePane;
    nw4r::lyt::Pane* mPanelPane;
    nw4r::lyt::Pane* mShadowPane;
#endif
};

namespace MR {
    SysInfoWindow* createSysInfoWindow();
    SysInfoWindow* createSysInfoWindowExecuteWithChildren();
    SysInfoWindow* createSysInfoWindowMiniExecuteWithChildren();
};  // namespace MR
