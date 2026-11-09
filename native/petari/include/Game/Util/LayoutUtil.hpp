#pragma once

#include "Game/Screen/CustomTagProcessor.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class IconAButton;
class FrameCtrl;
class LayoutActor;
class LayoutHolder;
class Nerve;
struct ResTIMG;
class SimpleLayout;

namespace Ui {
    class DrawInfo;
    class Pane;
    class TexMap;
    class TextBox;

    class Font;
    struct Rect;
}  // namespace Ui

class TextBoxRecursiveOperation {
public:
    /// @brief Destroys the `TextBoxRecursiveOperation`.
    virtual ~TextBoxRecursiveOperation() {
    }

    virtual void execute(Ui::TextBox* pTextBox) const = 0;
};

class TextBoxRecursiveSetMessage : public TextBoxRecursiveOperation {
public:
    /// @brief Creates a new `TextBoxRecursiveSetMessage`.
    TextBoxRecursiveSetMessage(const char16_t* pMessage) : mMessage(pMessage) {
    }

    virtual void execute(Ui::TextBox* pTextBox) const override {
        LayoutCoreUtil::setTextBoxMessage(pTextBox, mMessage);
    }

private:
    const char16_t* mMessage;
};

class TextBoxRecursiveSetArgNumber : public TextBoxRecursiveOperation {
public:
    /// @brief Creates a new `TextBoxRecursiveSetArgNumber`.
    TextBoxRecursiveSetArgNumber(s32 arg, s32 argIndex) : mArg(arg), mArgIndex(argIndex) {
    }

    virtual void execute(Ui::TextBox* pTextBox) const override {
        static_cast< CustomTagProcessor* >(pTextBox->mpTagProcessor)->setArgNumber(mArg, mArgIndex);
    }

private:
    s32 mArg;
    s32 mArgIndex;
};

class TextBoxRecursiveSetArgString : public TextBoxRecursiveOperation {
public:
    /// @brief Creates a new `TextBoxRecursiveSetArgString`.
    TextBoxRecursiveSetArgString(const char16_t* pArg, s32 argIndex) : mArg(pArg), mArgIndex(argIndex) {
    }

    virtual void execute(Ui::TextBox* pTextBox) const override {
        static_cast< CustomTagProcessor* >(pTextBox->mpTagProcessor)->setArgString(mArg, mArgIndex);
    }

private:
    const char16_t* mArg;
    s32 mArgIndex;
};

class TextBoxRecursiveSetVerticalPosition : public TextBoxRecursiveOperation {
public:
    /// @brief Creates a new `TextBoxRecursiveSetVerticalPosition`.
    TextBoxRecursiveSetVerticalPosition(u8 position) : mPosition(position) {
    }

    virtual void execute(Ui::TextBox* pTextBox) const override {
        pTextBox->SetTextPositionV(mPosition);
    }

private:
    u8 mPosition;
};

class TextBoxRecursiveSetHorizontalPosition : public TextBoxRecursiveOperation {
public:
    /// @brief Creates a new `TextBoxRecursiveSetHorizontalPosition`.
    TextBoxRecursiveSetHorizontalPosition(u8 position) : mPosition(position) {
    }

    virtual void execute(Ui::TextBox* pTextBox) const override {
        pTextBox->SetTextPositionH(mPosition);
    }

private:
    u8 mPosition;
};

class TextBoxRecursiveSetFont : public TextBoxRecursiveOperation {
public:
    /// @brief Creates a new `TextBoxRecursiveSetFont`.
    TextBoxRecursiveSetFont(Ui::Font* pFont) : mFont(pFont) {
    }

    virtual void execute(Ui::TextBox* pTextBox) const override {
        Ui::Size size = pTextBox->mFontSize;
        pTextBox->SetFont(mFont);
        pTextBox->SetFontSize(size);
    }

private:
    Ui::Font* mFont;
};

namespace MR {
    LayoutHolder* createAndAddLayoutHolder(const char*);
    LayoutHolder* createAndAddLayoutHolderRawData(const char*);
    // The game's own copy of the layout archive pArcName under a mod's (ResourceHolderManager); null with no mod's
    // copy over it.
    LayoutHolder* createAndAddGameLayoutHolder(const char* pArcName);
    void createAndAddPaneCtrl(LayoutActor*, const char*, u32);
    void createAndAddGroupCtrl(LayoutActor*, const char*, u32);
    bool isExistPaneCtrl(LayoutActor*, const char*);
    u8 getPaneAlpha(const LayoutActor*, const char*);
    void setInfluencedAlphaToChild(const LayoutActor*);
    void setLayoutAlpha(const LayoutActor*, u8);
    void setLayoutAlphaFloat(const LayoutActor*, f32);
    void setPaneAlpha(const LayoutActor*, const char*, u8);
    void setPaneAlphaFloat(const LayoutActor*, const char*, f32);
    void executeTextBoxRecursive(LayoutActor*, const char*, const TextBoxRecursiveOperation&);
    void setTextBoxGameMessageRecursive(LayoutActor*, const char*, const char*);
    void setTextBoxLayoutMessageRecursive(LayoutActor*, const char*, const char*);
    void setTextBoxSystemMessageRecursive(LayoutActor*, const char*, const char*);
    void setTextBoxMessageRecursive(LayoutActor*, const char*, const char16_t*);
    void setTextBoxFormatRecursive(LayoutActor*, const char*, const char16_t*, ...);
    void setTextBoxArgNumberRecursive(LayoutActor*, const char*, s32, s32);
    void setTextBoxArgStringRecursive(LayoutActor*, const char*, const char16_t*, s32);
    void setTextBoxArgGameMessageRecursive(LayoutActor*, const char*, const char*, s32);
    void setTextBoxVerticalPositionTopRecursive(LayoutActor*, const char*);
    void setTextBoxVerticalPositionCenterRecursive(LayoutActor*, const char*);
    void setTextBoxVerticalPositionBottomRecursive(LayoutActor*, const char*);
    void setTextBoxHorizontalPositionLeftRecursive(LayoutActor*, const char*);
    void setTextBoxHorizontalPositionCenterRecursive(LayoutActor*, const char*);
    void updateClearTimeTextBox(LayoutActor*, const char*, u32);
    void updateMinuteAndSecondTextBox(LayoutActor*, const char*, u32);
    void setTextBoxFontRecursive(LayoutActor*, const char*, Ui::Font*);
    void showPane(LayoutActor*, const char*);
    void showPaneRecursive(LayoutActor*, const char*);
    void hidePane(LayoutActor*, const char*);
    void hidePaneRecursive(LayoutActor*, const char*);
    // The same for a pane a screen bound at set-up: the pane and everything under it.
    void showPaneRecursive(Ui::Pane*);
    void hidePaneRecursive(Ui::Pane*);
    bool isHiddenPane(const LayoutActor*, const char*);
    void showScreen(LayoutActor*);
    void hideScreen(LayoutActor*);
    void setFollowPos(const Vec2f*, const LayoutActor*, const char*);
    void setFollowTypeReplace(const LayoutActor*, const char*);
    void setFollowTypeAdd(const LayoutActor*, const char*);
    void copyPaneTrans(Vec2f*, const LayoutActor*, const char*);
    f32 getPaneTransX(const LayoutActor*, const char*);
    f32 getPaneTransY(const LayoutActor*, const char*);
    void setLayoutPosAtPaneTrans(LayoutActor*, const LayoutActor*, const char*);
    void copyPaneScale(Vec2f*, const LayoutActor*, const char*);
    void setPaneScale(const LayoutActor*, f32, f32, const char*);
    void setLayoutScaleAtPaneScale(LayoutActor*, const LayoutActor*, const char*);
    void setPaneRotate(const LayoutActor*, f32, f32, f32, const char*);
    void setLayoutScalePosAtPaneScaleTrans(LayoutActor*, const LayoutActor*, const char*);
    void setLayoutScalePosAtPaneScaleTransIfExecCalcAnim(LayoutActor*, const LayoutActor*, const char*);
    Ui::TexMap* createLytTexMap(const char*, const char*);
    Ui::TexMap* createLytTexMap(ResTIMG*);
    Ui::TexMap* getLytTexMap(LayoutActor*, const char*, u8);
    void replacePaneTexture(LayoutActor*, const char*, const Ui::TexMap*, u8);
    void startAnim(LayoutActor*, const char*, u32);
    void startAnimAtFirstStep(LayoutActor*, const char*, u32);
    void startPaneAnim(LayoutActor*, const char*, const char*, u32);
    void startPaneAnimAtStep(LayoutActor*, const char*, const char*, s32, u32);
    void startPaneAnimAtFirstStep(LayoutActor*, const char*, const char*, u32);
    void startAnimReverseOneTime(LayoutActor*, const char*, u32);
    void startPaneAnimReverseOneTime(LayoutActor*, const char*, const char*, u32);
    void startAnimAndSetFrameAndStop(LayoutActor*, const char*, f32, u32);
    void setAnimFrameAndStop(LayoutActor*, f32, u32);
    void setAnimFrameAndStopAtEnd(LayoutActor*, u32);
    void setAnimFrameAndStopAdjustTextWidth(LayoutActor*, const char*, u32);
    void setAnimFrameAndStopAdjustTextHeight(LayoutActor*, const char*, u32);
    void setPaneAnimFrameAndStop(LayoutActor*, const char*, f32, u32);
    void setPaneAnimFrameAndStopAtEnd(LayoutActor*, const char*, u32);
    void setAnimFrame(LayoutActor*, f32, u32);
    void setPaneAnimFrame(LayoutActor*, const char*, f32, u32);
    void setAnimRate(LayoutActor*, f32, u32);
    void setPaneAnimRate(LayoutActor*, const char*, f32, u32);
    void stopAnim(LayoutActor*, u32);
    void stopPaneAnim(LayoutActor*, const char*, u32);
    bool isAnimStopped(const LayoutActor*, u32);
    bool isPaneAnimStopped(const LayoutActor*, const char*, u32);
    f32 getAnimFrame(const LayoutActor*, u32);
    f32 getPaneAnimFrame(const LayoutActor*, const char*, u32);
    s16 getAnimFrameMax(const LayoutActor*, u32);
    s16 getPaneAnimFrameMax(const LayoutActor*, const char*, u32);
    s16 getAnimFrameMax(const LayoutActor*, const char*);
    FrameCtrl* getAnimCtrl(const LayoutActor*, u32);
    FrameCtrl* getPaneAnimCtrl(const LayoutActor*, const char*, u32);
    void emitEffect(LayoutActor*, const char*);
    void deleteEffect(LayoutActor*, const char*);
    void forceDeleteEffect(LayoutActor*, const char*);
    void deleteEffectAll(LayoutActor*);
    void forceDeleteEffectAll(LayoutActor*);
    void setEffectHostMtx(LayoutActor*, const char*, MtxPtr);
    void setEffectRate(LayoutActor*, const char*, f32);
    void setEffectDirectionalSpeed(LayoutActor*, const char*, f32);
    void pauseOffEffectAll(LayoutActor*);
    bool isRegisteredEffect(const LayoutActor*, const char*);
    void copyLayoutDrawInfoWithAspect(Ui::DrawInfo*, const LayoutActor*, bool);
    void calcAnimLayoutWithDrawInfo(const LayoutActor*, const Ui::DrawInfo&);
    void drawLayoutWithDrawInfoWithoutProjectionSetup(const LayoutActor*, const Ui::DrawInfo&);
    bool isStep(const LayoutActor*, s32);
    bool isFirstStep(const LayoutActor*);
    bool isLessStep(const LayoutActor*, s32);
    bool isGreaterStep(const LayoutActor*, s32);
    bool isGreaterEqualStep(const LayoutActor*, s32);
    bool isIntervalStep(const LayoutActor*, s32);
    bool isNewNerve(const LayoutActor*);
    f32 calcNerveRate(const LayoutActor*, s32);
    f32 calcNerveRate(const LayoutActor*, s32, s32);
    f32 calcNerveEaseInRate(const LayoutActor*, s32);
    f32 calcNerveEaseInValue(const LayoutActor*, s32, s32, f32, f32);
    void setNerveAtStep(LayoutActor*, const Nerve*, s32);
    void setNerveAtAnimStopped(LayoutActor*, const Nerve*, u32);
    void setNerveAtPaneAnimStopped(LayoutActor*, const char*, const Nerve*, u32);
    void killAtAnimStopped(LayoutActor*, u32);
    bool isDead(const LayoutActor*);
    bool isHiddenLayout(const LayoutActor*);
    void showLayout(LayoutActor*);
    void hideLayout(LayoutActor*);
    bool isStopAnimFrame(const LayoutActor*);
    void stopAnimFrame(LayoutActor*);
    void releaseAnimFrame(LayoutActor*);
    void onCalcAnim(LayoutActor*);
    void offCalcAnim(LayoutActor*);
    bool isExecuteCalcAnimLayout(const LayoutActor*);
    bool isExecuteDrawLayout(const LayoutActor*);
    SimpleLayout* createSimpleLayout(const char*, const char*, u32);
    SimpleLayout* createSimpleLayoutTalkParts(const char*, const char*, u32);
    Ui::Pane* getPane(const LayoutActor*, const char*);
    Ui::Pane* getRootPane(const LayoutActor*);
    void calcTextBoxRectRecursive(Box2f*, const LayoutActor*, const char*);
    u32 getTextLineNumMaxRecursive(const LayoutActor*, const char*);
    void invalidateParentAnim(LayoutActor*);
    void setCometPaneAnimFromId(LayoutActor*, const char*, int, u32);
    void setTextBoxNumberRecursive(LayoutActor*, const char*, s32);
    void clearTextBoxMessageRecursive(LayoutActor*, const char*);
    IconAButton* createAndSetupIconAButton(LayoutActor*, bool, bool);
    void setCometAnimFromId(LayoutActor*, int, u32);
};  // namespace MR
