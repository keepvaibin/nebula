#include "Game/Util/TextString.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/CustomTagProcessor.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/LayoutPaneCtrl.hpp"
#include "Game/Screen/PaneEffectKeeper.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Render/GameTexture.hpp"
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Material.hpp>
#include <Game/Ui/Picture.hpp>
#include <Game/Ui/TextBox.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setLayoutAlphaFloat_f1, "setLayoutAlphaFloat.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setPaneAlphaFloat_f1, "setPaneAlphaFloat.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_copyLayoutDrawInfoWithAspect_f1, "copyLayoutDrawInfoWithAspect.f1", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_getCometColorAnimFrameFromId_f1, "getCometColorAnimFrameFromId.f1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getCometColorAnimFrameFromId_f2, "getCometColorAnimFrameFromId.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getCometColorAnimFrameFromId_f3, "getCometColorAnimFrameFromId.f3", 3.0f);




namespace {
    void setInfluencedAlphaToChild(Ui::Pane* pPane) {
        pPane->SetInfluencedAlpha(true);

        Ui::PaneList& rChildren = pPane->GetChildList();

        for (Ui::Pane* pChild : rChildren) {
            pChild->SetInfluencedAlpha(true);
        }
    }

    void initFrameCtrlReverse(FrameCtrl* pFrameCtrl) {
        pFrameCtrl->setAttribute(pFrameCtrl->EMode_RESET);
        pFrameCtrl->setRate(-pFrameCtrl->mRate);
        pFrameCtrl->setFrame(pFrameCtrl->mEnd);
    }

    bool getTextDrawRectRecursive(Ui::Rect* pRect, const Ui::Pane* pPane, bool initialized) {
        const Ui::PaneList& rChildren = pPane->GetChildList();

        for (const Ui::Pane* pChild : rChildren) {
            initialized = getTextDrawRectRecursive(pRect, pChild, initialized);
        }

        const Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(pPane);

        if (pTextBox != nullptr) {
            Ui::Rect rect = pTextBox->GetTextDrawRect(Ui::DrawInfo());

            if (initialized) {
                if (rect.left < pRect->left) {
                    pRect->left = rect.left;
                }

                if (rect.top < pRect->top) {
                    pRect->top = rect.top;
                }

                if (rect.right > pRect->right) {
                    pRect->right = rect.right;
                }

                if (rect.bottom > pRect->bottom) {
                    pRect->bottom = rect.bottom;
                }
            } else {
                *pRect = rect;
                initialized = true;
            }
        }

        return initialized;
    }

    u32 getTextLineNumMaxRecursiveSub(const Ui::Pane* pPane) {
        u32 max = 0;
        const Ui::PaneList& rChildren = pPane->GetChildList();

        for (const Ui::Pane* pChild : rChildren) {
            u32 count = getTextLineNumMaxRecursiveSub(pChild);

            if (count > max) {
                max = count;
            }
        }

        const Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(pPane);

        if (pTextBox != nullptr) {
            u32 count = MR::countMessageLine(pTextBox->mTextBuf);

            if (count > max) {
                max = count;
            }
        }

        return max;
    }
};  // namespace

LayoutHolder* MR::createAndAddLayoutHolder(const char* pArcName) {
    return SingletonHolder< ResourceHolderManager >::get()->createAndAddLayoutHolder(pArcName, nullptr);
}

LayoutHolder* MR::createAndAddGameLayoutHolder(const char* pArcName) {
    return SingletonHolder< ResourceHolderManager >::get()->createAndAddGameLayoutHolder(pArcName);
}

LayoutHolder* MR::createAndAddLayoutHolderRawData(const char* pArcPath) {
    return SingletonHolder< ResourceHolderManager >::get()->createAndAddLayoutHolderRawData(pArcPath);
}

void MR::createAndAddPaneCtrl(LayoutActor* pActor, const char* pPaneName, u32 animLayerNum) {
    pActor->getLayoutManager()->createAndAddPaneCtrl(pPaneName, animLayerNum);
}

void MR::createAndAddGroupCtrl(LayoutActor* pActor, const char* pGroupName, u32 animLayerNum) {
    pActor->getLayoutManager()->createAndAddGroupCtrl(pGroupName, animLayerNum);
}

bool MR::isExistPaneCtrl(LayoutActor* pActor, const char* pPaneName) {
    return pActor->getLayoutManager()->isExistPaneCtrl(pPaneName);
}

u8 MR::getPaneAlpha(const LayoutActor* pActor, const char* pPaneName) {
    // A pane the layout lacks (a mod's layout) is fully transparent; the pane setters below leave it alone.
    const Ui::Pane* pPane = getPane(pActor, pPaneName);
    return pPane != nullptr ? pPane->mAlpha : 0;
}

void MR::setInfluencedAlphaToChild(const LayoutActor* pActor) {
    ::setInfluencedAlphaToChild(getRootPane(pActor));
}

void MR::setLayoutAlpha(const LayoutActor* pActor, u8 alpha) {
    getRootPane(pActor)->mAlpha = alpha;
}

void MR::setLayoutAlphaFloat(const LayoutActor* pActor, f32 alpha) {
    f32 alphaFloat = clamp(alpha, 0.0f, 1.0f);

    setLayoutAlpha(pActor, (s32)(alphaFloat * cNum_setLayoutAlphaFloat_f1));
}

void MR::setPaneAlpha(const LayoutActor* pActor, const char* pPaneName, u8 alpha) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        pPane->mAlpha = alpha;
    }
}

void MR::setPaneAlphaFloat(const LayoutActor* pActor, const char* pPaneName, f32 alpha) {
    f32 alphaFloat = MR::clamp(alpha, 0.0f, 1.0f);

    setPaneAlpha(pActor, pPaneName, (s32)(alphaFloat * cNum_setPaneAlphaFloat_f1));
}

void MR::executeTextBoxRecursive(LayoutActor* pActor, const char* pPaneName, const TextBoxRecursiveOperation& rOperation) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);

    if (pPane == nullptr) {
        return;
    }

    Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(pPane);

    if (pTextBox != nullptr) {
        rOperation.execute(pTextBox);
    }

    for (Ui::Pane* pChild : pPane->GetChildList()) {
        executeTextBoxRecursive(pActor, pChild->mName, rOperation);
    }
}

void MR::setTextBoxGameMessageRecursive(LayoutActor* pActor, const char* pPaneName, const char* pMessageId) {
    setTextBoxMessageRecursive(pActor, pPaneName, getGameMessageDirect(pMessageId));
}

void MR::setTextBoxLayoutMessageRecursive(LayoutActor* pActor, const char* pPaneName, const char* pMessageId) {
    setTextBoxMessageRecursive(pActor, pPaneName, getLayoutMessageDirect(pMessageId));
}

void MR::setTextBoxSystemMessageRecursive(LayoutActor* pActor, const char* pPaneName, const char* pMessageId) {
    setTextBoxMessageRecursive(pActor, pPaneName, getSystemMessageDirect(pMessageId));
}

void MR::setTextBoxMessageRecursive(LayoutActor* pActor, const char* pPaneName, const char16_t* pMessage) {
    executeTextBoxRecursive(pActor, pPaneName, TextBoxRecursiveSetMessage(pMessage));
}

void MR::setTextBoxFormatRecursive(LayoutActor* pActor, const char* pPaneName, const char16_t* pFormat, ...) {
    MR::Text16 message;
    va_list list;

    va_start(list, pFormat);
    message.format(pFormat, list);
    va_end(list);

    setTextBoxMessageRecursive(pActor, pPaneName, message.c_str());
}

void MR::setTextBoxArgNumberRecursive(LayoutActor* pActor, const char* pPaneName, s32 number, s32 argIndex) {
    executeTextBoxRecursive(pActor, pPaneName, TextBoxRecursiveSetArgNumber(number, argIndex));
}

void MR::setTextBoxArgStringRecursive(LayoutActor* pActor, const char* pPaneName, const char16_t* pMessage, s32 argIndex) {
    executeTextBoxRecursive(pActor, pPaneName, TextBoxRecursiveSetArgString(pMessage, argIndex));
}

void MR::setTextBoxArgGameMessageRecursive(LayoutActor* pActor, const char* pPaneName, const char* pMessageId, s32 argIndex) {
    setTextBoxArgStringRecursive(pActor, pPaneName, getGameMessageDirect(pMessageId), argIndex);
}

void setTextBoxVerticalPositionRecursive(LayoutActor* pActor, const char* pPaneName, u8 position) {
    MR::executeTextBoxRecursive(pActor, pPaneName, TextBoxRecursiveSetVerticalPosition(position));
}

void MR::setTextBoxVerticalPositionTopRecursive(LayoutActor* pActor, const char* pPaneName) {
    setTextBoxVerticalPositionRecursive(pActor, pPaneName, 0);
}

void MR::setTextBoxVerticalPositionCenterRecursive(LayoutActor* pActor, const char* pPaneName) {
    setTextBoxVerticalPositionRecursive(pActor, pPaneName, 1);
}

void MR::setTextBoxVerticalPositionBottomRecursive(LayoutActor* pActor, const char* pPaneName) {
    setTextBoxVerticalPositionRecursive(pActor, pPaneName, 2);
}

void setTextBoxHorizontalPositionRecursive(LayoutActor* pActor, const char* pPaneName, u8 position) {
    MR::executeTextBoxRecursive(pActor, pPaneName, TextBoxRecursiveSetHorizontalPosition(position));
}

void MR::setTextBoxHorizontalPositionLeftRecursive(LayoutActor* pActor, const char* pPaneName) {
    setTextBoxHorizontalPositionRecursive(pActor, pPaneName, 0);
}

void MR::setTextBoxHorizontalPositionCenterRecursive(LayoutActor* pActor, const char* pPaneName) {
    setTextBoxHorizontalPositionRecursive(pActor, pPaneName, 1);
}

void MR::updateClearTimeTextBox(LayoutActor* pActor, const char* pPaneName, u32 step) {
    Text16 text;
    setTextBoxMessageRecursive(pActor, pPaneName, makeClearTimeString(text, step));
}

void MR::updateMinuteAndSecondTextBox(LayoutActor* pActor, const char* pPaneName, u32 step) {
    Text16 text;
    setTextBoxMessageRecursive(pActor, pPaneName, makeMinuteAndSecondString(text, step));
}

void MR::setTextBoxFontRecursive(LayoutActor* pActor, const char* pPaneName, Ui::Font* pFont) {
    executeTextBoxRecursive(pActor, pPaneName, TextBoxRecursiveSetFont(pFont));
}

void MR::showPane(LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        pPane->SetVisible(true);
    }
}

void MR::showPaneRecursive(Ui::Pane* pPane) {
    // A pane a mod's layout lacks is null: nothing to show.
    if (pPane == nullptr) {
        return;
    }
    pPane->SetVisible(true);
    for (Ui::Pane* pChild : pPane->GetChildList()) {
        showPaneRecursive(pChild);
    }
}

void MR::hidePaneRecursive(Ui::Pane* pPane) {
    // A pane a mod's layout lacks is null: nothing to hide.
    if (pPane == nullptr) {
        return;
    }
    pPane->SetVisible(false);
    for (Ui::Pane* pChild : pPane->GetChildList()) {
        hidePaneRecursive(pChild);
    }
}

void MR::showPaneRecursive(LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        showPaneRecursive(pPane);
    }
}

void MR::hidePane(LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        pPane->SetVisible(false);
    }
}

void MR::hidePaneRecursive(LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        hidePaneRecursive(pPane);
    }
}

bool MR::isHiddenPane(const LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    return pPane == nullptr || !pPane->IsVisible();
}

void MR::showScreen(LayoutActor* pActor) {
    LayoutManager* pLayoutManager = pActor->getLayoutManager();

    pLayoutManager->mIsScreenHidden = false;
}

void MR::hideScreen(LayoutActor* pActor) {
    LayoutManager* pLayoutManager = pActor->getLayoutManager();

    pLayoutManager->mIsScreenHidden = true;
}

void MR::setFollowPos(const Vec2f* pFollowPos, const LayoutActor* pActor, const char* pPaneName) {
    pActor->getLayoutManager()->getPaneCtrl(pPaneName)->setFollowPos(pFollowPos);
}

void MR::setFollowTypeReplace(const LayoutActor* pActor, const char* pPaneName) {
    LayoutPaneCtrl* pCtrl = pActor->getLayoutManager()->getPaneCtrl(pPaneName);
    pCtrl->mFollowType = 0;
}

void MR::setFollowTypeAdd(const LayoutActor* pActor, const char* pPaneName) {
    LayoutPaneCtrl* pCtrl = pActor->getLayoutManager()->getPaneCtrl(pPaneName);
    pCtrl->mFollowType = 1;
}

void MR::copyPaneTrans(Vec2f* pTrans, const LayoutActor* pActor, const char* pPaneName) {
    // A pane the layout lacks (a mod's layout) sits at the layout's origin.
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    pTrans->x = pPane != nullptr ? pPane->mGlbMtx.mMtx[0][3] : 0.0f;
    pTrans->y = pPane != nullptr ? pPane->mGlbMtx.mMtx[1][3] : 0.0f;
    convertLayoutPosToScreenPos(pTrans, *pTrans);
}

f32 MR::getPaneTransX(const LayoutActor* pActor, const char* pPaneName) {
    Vec2f trans;
    copyPaneTrans(&trans, pActor, pPaneName);
    return trans.x;
}

f32 MR::getPaneTransY(const LayoutActor* pActor, const char* pPaneName) {
    Vec2f trans;
    copyPaneTrans(&trans, pActor, pPaneName);
    return trans.y;
}

void MR::setLayoutPosAtPaneTrans(LayoutActor* pActor, const LayoutActor* pFollowActor, const char* pPaneName) {
    Vec2f trans;
    copyPaneTrans(&trans, pFollowActor, pPaneName);
    pActor->setTrans(trans);
}

void MR::copyPaneScale(Vec2f* pScale, const LayoutActor* pActor, const char* pPaneName) {
    // A pane the layout lacks (a mod's layout) has scale 1.
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane == nullptr) {
        pScale->set< f32 >(1.0f, 1.0f);
        return;
    }

    pScale->set< f32 >(pPane->mScale.x, pPane->mScale.y);
}

void MR::setPaneScale(const LayoutActor* pActor, f32 x, f32 y, const char* pPaneName) {
    Vec2f scale(x, y);
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        pPane->mScale = scale;
    }
}

void MR::setLayoutScaleAtPaneScale(LayoutActor* pActor, const LayoutActor* pFollowActor, const char* pPaneName) {
    Vec2f scale;
    copyPaneScale(&scale, pFollowActor, pPaneName);
    setPaneScale(pActor, scale.x, scale.y, nullptr);
}

void MR::setPaneRotate(const LayoutActor* pActor, f32 x, f32 y, f32 z, const char* pPaneName) {
    Vec3f rotate;
    rotate.x = x;
    rotate.y = y;
    rotate.z = z;
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane != nullptr) {
        pPane->mRotate = rotate;
    }
}

void MR::setLayoutScalePosAtPaneScaleTrans(LayoutActor* pActor, const LayoutActor* pFollowActor, const char* pPaneName) {
    setLayoutPosAtPaneTrans(pActor, pFollowActor, pPaneName);
    setLayoutScaleAtPaneScale(pActor, pFollowActor, pPaneName);
}

void MR::setLayoutScalePosAtPaneScaleTransIfExecCalcAnim(LayoutActor* pActor, const LayoutActor* pFollowActor, const char* pPaneName) {
    if (isExecuteCalcAnimLayout(pActor)) {
        setLayoutScalePosAtPaneScaleTrans(pActor, pFollowActor, pPaneName);
    }
}

Ui::TexMap* MR::createLytTexMap(const char* pArcName, const char* pTextureName) {
    GameTexture texture(loadTexFromArc(pArcName, pTextureName));

    return new Ui::TexMap(texture.site());
}

Ui::TexMap* MR::createLytTexMap(ResTIMG* pImage) {
    GameTexture texture(pImage);

    return new Ui::TexMap(texture.site());
}

Ui::TexMap* MR::getLytTexMap(LayoutActor* pActor, const char* pPaneName, u8 textureIndex) {
    Ui::Picture* pPicture = Ui::PaneCast< Ui::Picture >(getPane(pActor, pPaneName));

    // A pane that is not a picture (a mod's layout) has no texture map.
    if (pPicture == nullptr) {
        return nullptr;
    }

    return const_cast< Ui::TexMap* >(&pPicture->GetMaterial()->GetTexture(textureIndex));
}

void MR::replacePaneTexture(LayoutActor* pActor, const char* pPaneName, const Ui::TexMap* pTexture, u8 textureIndex) {
    Ui::Picture* pPicture = Ui::PaneCast< Ui::Picture >(getPane(pActor, pPaneName));

    // A picture the caller found no texture for keeps the one it has.
    if (pPicture == nullptr || pTexture == nullptr) {
        return;
    }

    pPicture->GetMaterial()->SetTexture(textureIndex, *pTexture);
}

void MR::startAnim(LayoutActor* pActor, const char* pAnimName, u32 animLayer) {
    pActor->getLayoutManager()->getPaneCtrl(nullptr)->start(pAnimName, animLayer);

    if (pActor->mEffectKeeper != nullptr) {
        pActor->mEffectKeeper->changeAnim();
    }
}

void MR::startAnimAtFirstStep(LayoutActor* pActor, const char* pAnimName, u32 animLayer) {
    if (isFirstStep(pActor)) {
        startAnim(pActor, pAnimName, animLayer);
    }
}

void MR::startPaneAnim(LayoutActor* pActor, const char* pPaneName, const char* pAnimName, u32 animLayer) {
    pActor->getLayoutManager()->getPaneCtrl(pPaneName)->start(pAnimName, animLayer);
}

void MR::startPaneAnimAtStep(LayoutActor* pActor, const char* pPaneName, const char* pAnimName, s32 step, u32 animLayer) {
    if (isStep(pActor, step)) {
        startPaneAnim(pActor, pPaneName, pAnimName, animLayer);
    }
}

void MR::startPaneAnimAtFirstStep(LayoutActor* pActor, const char* pPaneName, const char* pAnimName, u32 animLayer) {
    if (isFirstStep(pActor)) {
        startPaneAnim(pActor, pPaneName, pAnimName, animLayer);
    }
}

void MR::startAnimReverseOneTime(LayoutActor* pActor, const char* pAnimName, u32 animLayer) {
    LayoutPaneCtrl* pPaneCtrl = pActor->getLayoutManager()->getPaneCtrl(nullptr);

    pPaneCtrl->start(pAnimName, animLayer);
    ::initFrameCtrlReverse(pPaneCtrl->getFrameCtrl(animLayer));
}

void MR::startPaneAnimReverseOneTime(LayoutActor* pActor, const char* pPaneName, const char* pAnimName, u32 animLayer) {
    LayoutPaneCtrl* pPaneCtrl = pActor->getLayoutManager()->getPaneCtrl(pPaneName);

    pPaneCtrl->start(pAnimName, animLayer);
    ::initFrameCtrlReverse(pPaneCtrl->getFrameCtrl(animLayer));
}

void MR::startAnimAndSetFrameAndStop(LayoutActor* pActor, const char* pAnimName, f32 animFrame, u32 animLayer) {
    startAnim(pActor, pAnimName, animLayer);
    setAnimFrameAndStop(pActor, animFrame, animLayer);
}

void MR::setAnimFrameAndStop(LayoutActor* pActor, f32 animFrame, u32 animLayer) {
    FrameCtrl* pFrameCtrl = getAnimCtrl(pActor, animLayer);

    pFrameCtrl->setFrame(animFrame);
    pFrameCtrl->setRate(0.0f);
}

void MR::setAnimFrameAndStopAtEnd(LayoutActor* pActor, u32 animLayer) {
    setAnimFrameAndStop(pActor, getAnimFrameMax(pActor, animLayer), animLayer);
}

void MR::setAnimFrameAndStopAdjustTextWidth(LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane == nullptr) {
        return;
    }

    Ui::Rect rect;
    ::getTextDrawRectRecursive(&rect, pPane, false);
    setAnimFrameAndStop(pActor, rect.GetWidth(), animLayer);
}

void MR::setAnimFrameAndStopAdjustTextHeight(LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    if (pPane == nullptr) {
        return;
    }

    Ui::Rect rect;
    ::getTextDrawRectRecursive(&rect, pPane, false);
    setAnimFrameAndStop(pActor, rect.GetHeight(), animLayer);
}

void MR::setPaneAnimFrameAndStop(LayoutActor* pActor, const char* pPaneName, f32 animFrame, u32 animLayer) {
    FrameCtrl* pFrameCtrl = getPaneAnimCtrl(pActor, pPaneName, animLayer);

    pFrameCtrl->setFrame(animFrame);
    pFrameCtrl->setRate(0.0f);
}

void MR::setPaneAnimFrameAndStopAtEnd(LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    setPaneAnimFrameAndStop(pActor, pPaneName, getPaneAnimFrameMax(pActor, pPaneName, animLayer), animLayer);
}

void MR::setAnimFrame(LayoutActor* pActor, f32 animFrame, u32 animLayer) {
    getAnimCtrl(pActor, animLayer)->setFrame(animFrame);
}

void MR::setPaneAnimFrame(LayoutActor* pActor, const char* pPaneName, f32 animFrame, u32 animLayer) {
    getPaneAnimCtrl(pActor, pPaneName, animLayer)->setFrame(animFrame);
}

void MR::setAnimRate(LayoutActor* pActor, f32 animRate, u32 animLayer) {
    getAnimCtrl(pActor, animLayer)->setRate(animRate);
}

void MR::setPaneAnimRate(LayoutActor* pActor, const char* pPaneName, f32 animRate, u32 animLayer) {
    getPaneAnimCtrl(pActor, pPaneName, animLayer)->setRate(animRate);
}

void MR::stopAnim(LayoutActor* pActor, u32 animLayer) {
    pActor->getLayoutManager()->getPaneCtrl(nullptr)->stop(animLayer);
}

void MR::stopPaneAnim(LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    pActor->getLayoutManager()->getPaneCtrl(pPaneName)->stop(animLayer);
}

bool MR::isAnimStopped(const LayoutActor* pActor, u32 animLayer) {
    return pActor->getLayoutManager()->getPaneCtrl(nullptr)->isAnimStopped(animLayer);
}

bool MR::isPaneAnimStopped(const LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    return pActor->getLayoutManager()->getPaneCtrl(pPaneName)->isAnimStopped(animLayer);
}

f32 MR::getAnimFrame(const LayoutActor* pActor, u32 animLayer) {
    return getAnimCtrl(pActor, animLayer)->getFrame();
}

f32 MR::getPaneAnimFrame(const LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    return getPaneAnimCtrl(pActor, pPaneName, animLayer)->getFrame();
}

s16 MR::getAnimFrameMax(const LayoutActor* pActor, u32 animLayer) {
    return getAnimCtrl(pActor, animLayer)->getEnd();
}

s16 MR::getPaneAnimFrameMax(const LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    return getPaneAnimCtrl(pActor, pPaneName, animLayer)->getEnd();
}

s16 MR::getAnimFrameMax(const LayoutActor* pActor, const char* pAnimName) {
    // An animation the layout lacks (a mod's layout) has no frames.
    Ui::AnimTransform* pAnimTransform = pActor->getLayoutManager()->getAnimTransform(pAnimName);
    return pAnimTransform != nullptr ? pAnimTransform->GetFrameSize() : 0;
}

FrameCtrl* MR::getAnimCtrl(const LayoutActor* pActor, u32 animLayer) {
    return pActor->getLayoutManager()->getPaneCtrl(nullptr)->getFrameCtrl(animLayer);
}

FrameCtrl* MR::getPaneAnimCtrl(const LayoutActor* pActor, const char* pPaneName, u32 animLayer) {
    return pActor->getLayoutManager()->getPaneCtrl(pPaneName)->getFrameCtrl(animLayer);
}

void MR::emitEffect(LayoutActor* pActor, const char* pEffectName) {
    pActor->mEffectKeeper->createEmitter(pEffectName);
}

void MR::deleteEffect(LayoutActor* pActor, const char* pEffectName) {
    pActor->mEffectKeeper->deleteEmitter(pEffectName);
}

void MR::forceDeleteEffect(LayoutActor* pActor, const char* pEffectName) {
    pActor->mEffectKeeper->forceDeleteEmitter(pEffectName);
}

void MR::deleteEffectAll(LayoutActor* pActor) {
    pActor->mEffectKeeper->deleteEmitterAll();
}

void MR::forceDeleteEffectAll(LayoutActor* pActor) {
    pActor->mEffectKeeper->forceDeleteEmitterAll();
}

void MR::setEffectHostMtx(LayoutActor* pActor, const char* pParam2, MtxPtr pHostMtx) {
    // An effect name the layout's effect list lacks (a mod's list) is left alone.
    MultiEmitter* pEmitter = getEffect(pActor, pParam2);
    if (pEmitter != nullptr) {
        pEmitter->setHostMtx(pHostMtx);
    }
}

void MR::setEffectRate(LayoutActor* pActor, const char* pEffectName, f32 rate) {
    MultiEmitter* pEmitter = getEffect(pActor, pEffectName);
    if (pEmitter != nullptr) {
        pEmitter->setRate(rate, -1);
    }
}

void MR::setEffectDirectionalSpeed(LayoutActor* pActor, const char* pEffectName, f32 speed) {
    MultiEmitter* pEmitter = getEffect(pActor, pEffectName);
    if (pEmitter != nullptr) {
        pEmitter->setDirectionalSpeed(speed, -1);
    }
}

void MR::pauseOffEffectAll(LayoutActor* pActor) {
    MultiEmitter* pEmitter;
    PaneEffectKeeper* pKeeper = pActor->mEffectKeeper;

    for (s32 i = 0; i < pKeeper->mEmitters.size(); i++) {
        pEmitter = pKeeper->mEmitters.begin()[i];

        if (pEmitter != nullptr && pEmitter->isValid()) {
            pEmitter->pauseOff(-1);
        }
    }
}

bool MR::isRegisteredEffect(const LayoutActor* pActor, const char* pParam2) {
    if (pParam2 != nullptr) {
        return pActor->mEffectKeeper->getEmitter(pParam2) != nullptr;
    }

    return pActor->mEffectKeeper != nullptr;
}

void MR::copyLayoutDrawInfoWithAspect(Ui::DrawInfo* pDrawInfo, const LayoutActor* pActor, bool aspect) {
    *pDrawInfo = pActor->getLayoutManager()->mDrawInfo;

    if (aspect) {
        pDrawInfo->mFlag.locationAdjust = true;
        pDrawInfo->mLocationAdjustScale = Vec2f(cNum_copyLayoutDrawInfoWithAspect_f1, 1.0f);
    } else {
        pDrawInfo->mFlag.locationAdjust = false;
        pDrawInfo->mLocationAdjustScale = Vec2f(1.0f, 1.0f);
    }
}

void MR::calcAnimLayoutWithDrawInfo(const LayoutActor* pActor, const Ui::DrawInfo& rDrawInfo) {
    if (isExecuteCalcAnimLayout(pActor)) {
        pActor->getLayoutManager()->calcAnimWithoutLocationAdjust(rDrawInfo);
    }
}

void MR::drawLayoutWithDrawInfoWithoutProjectionSetup(const LayoutActor* pActor, const Ui::DrawInfo& rDrawInfo) {
    if (isExecuteDrawLayout(pActor)) {
        pActor->getLayoutManager()->mLayout->Draw(rDrawInfo);
    }
}

bool MR::isStep(const LayoutActor* pActor, s32 step) {
    return pActor->getNerveStep() == step;
}

bool MR::isFirstStep(const LayoutActor* pActor) {
    return isStep(pActor, 0);
}

bool MR::isLessStep(const LayoutActor* pActor, s32 step) {
    if (!isNewNerve(pActor) && pActor->getNerveStep() < step) {
        return true;
    }

    return false;
}

bool MR::isGreaterStep(const LayoutActor* pActor, s32 step) {
    return pActor->getNerveStep() > step;
}

bool MR::isGreaterEqualStep(const LayoutActor* pActor, s32 step) {
    return pActor->getNerveStep() >= step;
}

bool MR::isIntervalStep(const LayoutActor* pActor, s32 step) {
    return (step != 0 ? pActor->getNerveStep() % step : pActor->getNerveStep()) == 0;
}

bool MR::isNewNerve(const LayoutActor* pActor) {
    return pActor->getNerveStep() < 0;
}

f32 MR::calcNerveRate(const LayoutActor* pActor, s32 stepMax) {
    return stepMax <= 0 ? 1.0f : clamp(static_cast< f32 >(pActor->getNerveStep()) / stepMax, 0.0f, 1.0f);
}

f32 MR::calcNerveRate(const LayoutActor* pActor, s32 stepMin, s32 stepMax) {
    return clamp(normalize(pActor->getNerveStep(), stepMin, stepMax), 0.0f, 1.0f);
}

f32 MR::calcNerveEaseInRate(const LayoutActor* pActor, s32 stepMax) {
    return getEaseInValue(calcNerveRate(pActor, stepMax), 0.0f, 1.0f, 1.0f);
}

f32 MR::calcNerveEaseInValue(const LayoutActor* pActor, s32 stepMin, s32 stepMax, f32 valueMin, f32 valueMax) {
    return getEaseInValue(calcNerveRate(pActor, stepMin, stepMax), valueMin, valueMax, 1.0f);
}

void MR::setNerveAtStep(LayoutActor* pActor, const Nerve* pNerve, s32 step) {
    if (pActor->getNerveStep() == step) {
        pActor->setNerve(pNerve);
    }
}

void MR::setNerveAtAnimStopped(LayoutActor* pActor, const Nerve* pNerve, u32 animLayer) {
    if (isAnimStopped(pActor, animLayer)) {
        pActor->setNerve(pNerve);
    }
}

void MR::setNerveAtPaneAnimStopped(LayoutActor* pActor, const char* pPaneName, const Nerve* pNerve, u32 animLayer) {
    if (isPaneAnimStopped(pActor, pPaneName, animLayer)) {
        pActor->setNerve(pNerve);
    }
}

void MR::killAtAnimStopped(LayoutActor* pActor, u32 animLayer) {
    if (isAnimStopped(pActor, animLayer)) {
        pActor->kill();
    }
}

bool MR::isDead(const LayoutActor* pActor) {
    return pActor->mFlag.mIsDead;
}

bool MR::isHiddenLayout(const LayoutActor* pActor) {
    return pActor->mFlag.mIsHidden;
}

void MR::showLayout(LayoutActor* pActor) {
    pActor->mFlag.mIsHidden = false;
    pActor->mFlag.mIsOffCalcAnim = false;
}

void MR::hideLayout(LayoutActor* pActor) {
    pActor->mFlag.mIsHidden = true;
    pActor->mFlag.mIsOffCalcAnim = true;
}

bool MR::isStopAnimFrame(const LayoutActor* pActor) {
    return pActor->mFlag.mIsStopAnimFrame;
}

void MR::stopAnimFrame(LayoutActor* pActor) {
    pActor->mFlag.mIsStopAnimFrame = true;
}

void MR::releaseAnimFrame(LayoutActor* pActor) {
    pActor->mFlag.mIsStopAnimFrame = false;
}

void MR::onCalcAnim(LayoutActor* pActor) {
    pActor->mFlag.mIsOffCalcAnim = false;
}

void MR::offCalcAnim(LayoutActor* pActor) {
    pActor->mFlag.mIsOffCalcAnim = true;
}

bool MR::isExecuteCalcAnimLayout(const LayoutActor* pActor) {
    if (pActor->mFlag.mIsDead) {
        return false;
    }

    if (pActor->mLayoutManager == nullptr) {
        return false;
    }

    return !pActor->mFlag.mIsOffCalcAnim;
}

bool MR::isExecuteDrawLayout(const LayoutActor* pActor) {
    if (pActor->mFlag.mIsDead) {
        return false;
    }

    if (pActor->mLayoutManager == nullptr) {
        return false;
    }

    return !pActor->mFlag.mIsHidden;
}

SimpleLayout* MR::createSimpleLayout(const char* pName, const char* pArcName, u32 rootPaneAnimLayerNum) {
    SimpleLayout* simpleLayout;

    simpleLayout = new SimpleLayout(pName, pArcName, rootPaneAnimLayerNum, MR::DrawType_Layout);
    simpleLayout->initWithoutIter();

    return simpleLayout;
}

SimpleLayout* MR::createSimpleLayoutTalkParts(const char* pName, const char* pArcName, u32 rootPaneAnimLayerNum) {
    SimpleLayout* simpleLayout;

    simpleLayout = new SimpleLayout(pName, pArcName, rootPaneAnimLayerNum, MR::DrawType_TalkPartsLayout);
    simpleLayout->initWithoutIter();

    return simpleLayout;
}

Ui::Pane* MR::getPane(const LayoutActor* pActor, const char* pPaneName) {
    return pActor->getLayoutManager()->getPane(pPaneName);
}

Ui::Pane* MR::getRootPane(const LayoutActor* pActor) {
    return pActor->getLayoutManager()->getPane(nullptr);
}

void MR::calcTextBoxRectRecursive(Box2f* pBox, const LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);
    Ui::Rect rect;

    // A pane the layout lacks (a mod's layout) has an empty box.
    if (pPane != nullptr && ::getTextDrawRectRecursive(&rect, pPane, false)) {
        Vec2f min;
        Vec2f max;
        convertPaneLocalPosToScreenPos(&min, pPane, Vec2f(rect.left, rect.top));
        convertPaneLocalPosToScreenPos(&max, pPane, Vec2f(rect.right, rect.bottom));
        pBox->set(min, max);
    } else {
        pBox->i.zero();
        pBox->f.zero();
    }
}

u32 MR::getTextLineNumMaxRecursive(const LayoutActor* pActor, const char* pPaneName) {
    Ui::Pane* pPane = getPane(pActor, pPaneName);

    if (pPane != nullptr) {
        return ::getTextLineNumMaxRecursiveSub(pPane);
    }

    return 0;
}

void MR::invalidateParentAnim(LayoutActor* pActor) {
    LayoutManager* pLayoutManager = pActor->getLayoutManager();

    pLayoutManager->mParentAnimValid = false;
}

namespace {
    f32 getCometColorAnimFrameFromId(s32 id) {
        f32 frame = 0.0f;

        switch (id) {
        case 0:
            return frame;
        case 1:
            frame = cNum_getCometColorAnimFrameFromId_f1;
            break;
        case 2:
            frame = 1.0f;
            break;
        case 3:
            frame = cNum_getCometColorAnimFrameFromId_f2;
            break;
        case 4:
            frame = cNum_getCometColorAnimFrameFromId_f3;
            break;
        default:
            break;
        }

        return frame;
    }
};  // namespace

void MR::setCometPaneAnimFromId(LayoutActor* pActor, const char* pPaneName, int cometId, u32 animLayer) {
    startPaneAnim(pActor, pPaneName, "Color", animLayer);
    setPaneAnimFrameAndStop(pActor, pPaneName, ::getCometColorAnimFrameFromId(cometId), animLayer);
}

void MR::setTextBoxNumberRecursive(LayoutActor* pActor, const char* pPaneName, s32 number) {
    setTextBoxFormatRecursive(pActor, pPaneName, u"%d", number);
}

void MR::clearTextBoxMessageRecursive(LayoutActor* pActor, const char* pPaneName) {
    setTextBoxMessageRecursive(pActor, pPaneName, u"");
}

IconAButton* MR::createAndSetupIconAButton(LayoutActor* pActor, bool connectToScene, bool connectToPause) {
    IconAButton* iconAButton;

    iconAButton = new IconAButton(connectToScene, connectToPause);
    iconAButton->initWithoutIter();
    iconAButton->setFollowActorPane(pActor, "AButtonPosition");

    return iconAButton;
}

void MR::setCometAnimFromId(LayoutActor* pActor, int cometId, u32 animLayer) {
    startAnim(pActor, "Color", animLayer);
    setAnimFrameAndStop(pActor, ::getCometColorAnimFrameFromId(cometId), animLayer);
}

