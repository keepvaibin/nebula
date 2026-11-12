#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/LayoutAspect.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutGroupCtrl.hpp"
#include "Game/Screen/LayoutPaneCtrl.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <cstdio>
#include <cstring>
#include <Game/Ui/Group.hpp>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Material.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/Picture.hpp>
#include <Game/Ui/TexMap.hpp>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/Rect.hpp>
#include <nebula_math.h>
#include <nebula_format.h>

namespace {
    namespace Local {
        class BitFlagBase {
        public:
            virtual ~BitFlagBase() {
            }
            virtual void onBit(int) = 0;
            virtual void offBit(int) = 0;
            virtual bool isTrue(int) const = 0;
            virtual bool isAnythingTrue() const = 0;
        };

        template < int N >
        class BitFlag : public BitFlagBase {
        public:
            BitFlag() {
                MR::zeroMemory(mBits, sizeof(mBits));
            }

            virtual bool isAnythingTrue() const;
            virtual void onBit(int);
            virtual void offBit(int);
            virtual bool isTrue(int) const;

            u32 mBits[N / 32];
        };
    };  // namespace Local

    static const char* const cRemoveString[] = {
        "4x3",
        "16x9",
        "Replace",
    };
};  // namespace

namespace {
    namespace Local {
        // Whether the first four characters of two names match (a name shorter than four ends the compare).
        inline bool isSameFirstFour(const char* pA, const char* pB) {
            for (u32 i = 0; i < 4; i++) {
                if (pA[i] != pB[i]) {
                    return false;
                }
                if (pA[i] == '\0') {
                    return true;
                }
            }
            return true;
        }

        inline void collectLanguagePanes(Ui::Pane* pPane, BitFlagBase& rLanguagePanes, BitFlagBase& rCurrentLanguagePanes) {
            Ui::PaneList& rPaneList = pPane->mChildList;
            s32 index = -1;
            for (Ui::Pane* pChild : rPaneList) {
                index++;
                const char* pName = pChild->mName;
                u32 length = strlen(pName);
                if (length < 4) {
                    continue;
                }

                const char* pSuffix = pName + (length - 4);
                for (u32 i = 0; i < MR::getLanguageNum(); i++) {
                    if (isSameFirstFour(pSuffix, MR::getLanguagePrefixByIndex(i))) {
                        rLanguagePanes.onBit(index);
                        if (isSameFirstFour(pSuffix, MR::getCurrentLanguagePrefix())) {
                            rCurrentLanguagePanes.onBit(index);
                        }

                        break;
                    }
                }
            }
        }

        inline void removeLanguagePanes(Ui::Pane* pPane, const BitFlagBase& rLanguagePanes, const BitFlagBase& rCurrentLanguagePanes) {
            const char* pName;
            Ui::PaneList& rChildren = pPane->mChildList;
            u32 length;
            s32 childIndex = 0;
            if (rCurrentLanguagePanes.isAnythingTrue()) {
                for (Ui::Pane *pChild = rChildren.First(), *pNext; pChild != nullptr; pChild = pNext) {
                    pNext = Ui::PaneList::Next(pChild);
                    if (rCurrentLanguagePanes.isTrue(childIndex)) {
                        pName = pChild->mName;
                        length = strlen(pName) - 4;
                        const MR::FormattedName name("%.*s", static_cast< int >(length), pName);
                        pChild->SetName(name.c_str());
                    } else {
                        pPane->RemoveChild(pChild);
                    }

                    childIndex++;
                }
            } else {
                for (Ui::Pane *pChild = rChildren.First(), *pNext; pChild != nullptr; pChild = pNext) {
                    pNext = Ui::PaneList::Next(pChild);
                    if (rLanguagePanes.isTrue(childIndex)) {
                        pPane->RemoveChild(pChild);
                    }

                    childIndex++;
                }
            }
        }
    };  // namespace Local
};  // namespace

LayoutManager::LayoutManager(const char* pLayoutName, bool useArchiveNamePrefix, u32 rootPaneAnimLayerNum, u32 textBoxBufferLength)
    : mLayoutHolder(), mLayout(), mAnimTransList(), mDrawInfo(), mIsScreenHidden(), mParentAnimValid(true), mIndDummyTexMap(), mGroupCtrlCount(),
      mGroupCtrlList(), mLayoutName() {
    // pLayoutName points into fileNameWithoutExtension below, so it lives as long as the constructor
    // (declared outside the if block so it outlives it).
    MR::FormattedName fileNameWithoutExtension;
    if (useArchiveNamePrefix) {
        MR::FormattedName fileNameFromPrefix;
        MR::makeLayoutArchiveFileNameFromPrefix(fileNameFromPrefix, pLayoutName, true);
        MR::removeExtensionString(fileNameWithoutExtension, fileNameFromPrefix.c_str());
        pLayoutName = MR::getBasename(fileNameWithoutExtension.c_str());
    }

    MR::FormattedName archiveName("%s", pLayoutName);

    u32 nameLength = strlen(pLayoutName);
    for (u32 i = 0; i < ARRAY_SIZE(::cRemoveString); i++) {
        const char* const pos = strstr(pLayoutName, ::cRemoveString[i]);

        if (pos != nullptr) {
            nameLength = static_cast< u32 >(pos - pLayoutName);
            break;
        }
    }

    mLayoutName = new char[nameLength + 1];
    memcpy(mLayoutName, pLayoutName, nameLength);
    mLayoutName[nameLength] = '\0';

    initArc(archiveName.c_str(), pLayoutName);
    initGroupCtrlList();
    initDrawInfo();
    initTextBoxRecursive(mLayout->mpRootPane, nullptr, mLayoutName, textBoxBufferLength);
    replaceIndDummyTexture();

    if (rootPaneAnimLayerNum != 0) {
        createAndAddRootPaneCtrl(rootPaneAnimLayerNum);
    }

    mLayout->Animate(0);
    mLayout->CalculateMtx(mDrawInfo);
}

void LayoutManager::movement() {
    const Ui::Layout::Order& rOrder = mLayout->GetOrder();
    for (u32 i = 0; i < rOrder.mNum; i++) {
        const LayoutPaneState* pState = findState(rOrder.mPanes[i]);
        if (pState != nullptr && pState->mPaneCtrl != nullptr) {
            pState->mPaneCtrl->movement();
        }
    }

    for (u32 i = 0; i < mGroupCtrlCount; i++) {
        if (mGroupCtrlList[i] != nullptr) {
            mGroupCtrlList[i]->movement();
        }
    }
}

void LayoutManager::calcAnim() {
    if (MR::isScreen16Per9()) {
        Vec2f scale;
        scale.x = 0.75f;
        scale.y = 1.0f;
        mDrawInfo.SetLocationAdjustScale(scale);
    } else {
        Vec2f scale;
        scale.x = 1.0f;
        scale.y = 1.0f;
        mDrawInfo.SetLocationAdjustScale(scale);
    }

    calcAnimWithoutLocationAdjust(mDrawInfo);
}

void LayoutManager::draw() const {
    if (mIsScreenHidden) {
        return;
    }

    MR::setupDrawForLayout();
    mLayout->Draw(mDrawInfo);
}

void LayoutManager::setDrawGeneration(u32 generation) const {
    mLayout->mDrawGeneration = generation;
}

void LayoutManager::addPaneCtrl(LayoutPaneCtrl* pPaneCtrl) {
    Ui::Pane* const pPane = findPaneOrRoot(pPaneCtrl->mPane->mName);
    LayoutPaneState& rState = stateOf(pPane);

    if (rState.mPaneCtrl == nullptr) {
        pPaneCtrl->mStatePane = pPane;
        rState.mPaneCtrl = pPaneCtrl;
    }
}

LayoutPaneCtrl* LayoutManager::createAndAddRootPaneCtrl(u32 animLayerNum) {
    if (LayoutPaneCtrl* pPaneCtrl = stateOf(findPaneOrRoot(mLayout->mpRootPane->mName)).mPaneCtrl) {
        return pPaneCtrl;
    }

    LayoutPaneCtrl* pPaneCtrl = new LayoutPaneCtrl(this, mLayout->mpRootPane->mName, animLayerNum);
    addPaneCtrl(pPaneCtrl);
    return pPaneCtrl;
}

LayoutPaneCtrl* LayoutManager::createAndAddPaneCtrl(const char* pName, u32 animLayerNum) {
    if (LayoutPaneCtrl* pPaneCtrl = stateOf(findPaneOrRoot(pName)).mPaneCtrl) {
        return pPaneCtrl;
    }

    LayoutPaneCtrl* pPaneCtrl = new LayoutPaneCtrl(this, pName, animLayerNum);
    addPaneCtrl(pPaneCtrl);
    return pPaneCtrl;
}

LayoutPaneCtrl* LayoutManager::getPaneCtrl(const char* pName) const {
    const LayoutPaneState* pState = findState(pName == nullptr ? mLayout->mpRootPane : findPaneOrRoot(pName));
    return pState != nullptr ? pState->mPaneCtrl : nullptr;
}

Ui::Pane* LayoutManager::findPaneOrRoot(const char* pName) const {
    Ui::Pane* const pPane = findPaneByName(pName);
    return pPane != nullptr ? pPane : mLayout->mpRootPane;
}

const LayoutPaneState* LayoutManager::findState(const Ui::Pane* pPane) const {
    if (pPane == nullptr || pPane->GetLayout() != mLayout || pPane->GetId() >= mPaneStateNum) {
        return nullptr;
    }
    return &mPaneStates[pPane->GetId()];
}

LayoutPaneState& LayoutManager::stateOf(const Ui::Pane* pPane) {
    // Every caller passes a pane of this layout (a name lookup's answer or the root).
    const u32 id = pPane->GetId();
    if (id >= mPaneStateNum) {
        u32 num = mPaneStateNum == 0 ? 64 : mPaneStateNum;
        while (num <= id || num < mLayout->GetPaneNum()) {
            num *= 2;
        }
        LayoutPaneState* const pStates = new LayoutPaneState[num];
        for (u32 i = 0; i < num; i++) {
            pStates[i] = i < mPaneStateNum ? mPaneStates[i] : LayoutPaneState{nullptr, nullptr, nullptr};
        }
        delete[] mPaneStates;
        mPaneStates = pStates;
        mPaneStateNum = num;
    }
    return mPaneStates[id];
}

bool LayoutManager::isExistPaneCtrl(const char* pName) const {
    return getPaneCtrl(pName) != nullptr;
}

void LayoutManager::addGroupCtrl(LayoutGroupCtrl* pGroupCtrl) {
    // A group the layout does not have (no group, or an index past the end) is not registered.
    if (pGroupCtrl->mGroup == nullptr) {
        return;
    }

    s32 index = getIndexOfGroupCtrl(pGroupCtrl->mGroup->mName);
    if (index < 0 || static_cast< u32 >(index) >= mGroupCtrlCount) {
        return;
    }

    mGroupCtrlList[index] = pGroupCtrl;

    for (u32 i = 0; i < pGroupCtrl->getPaneNum(); i++) {
        LayoutPaneState& rState = stateOf(findPaneOrRoot(pGroupCtrl->getPane(i)->mName));
        LayoutGroupCtrlLink* pLink = new LayoutGroupCtrlLink;
        pLink->mGroupCtrl = pGroupCtrl;
        pLink->mNext = rState.mGroupCtrlLink;
        rState.mGroupCtrlLink = pLink;
    }
}

bool LayoutManager::isPointing(const Ui::Pane* pPane, const Vec2f& rPos) const {
    Mtx inverse;
    NebulaMtxInverse(pPane->mGlbMtx, inverse);
    s32 horizontalPosition = static_cast< u8 >(pPane->mBasePosition % 3);
    s32 verticalPosition = static_cast< u8 >(pPane->mBasePosition / 3);
    Vec3f pos(0.0f, 0.0f, 0.0f);
    MR::convertScreenPosToLayoutPos(reinterpret_cast< Vec2f* >(&pos), rPos);
    NebulaMtxMultVec(inverse, &pos, &pos);

    switch (horizontalPosition) {
    case 0:
        if (pos.x < 0.0f || pPane->GetDrawSize().width < pos.x) {
            return false;
        }

        break;
    case 1:
        if (pos.x < -pPane->GetDrawSize().width / 2.0f || pPane->GetDrawSize().width / 2.0f < pos.x) {
            return false;
        }

        break;
    case 2:
        if (pos.x < -pPane->GetDrawSize().width || 0.0f < pos.x) {
            return false;
        }

        break;
    }

    switch (verticalPosition) {
    case 0:
        if (pos.y < -pPane->GetDrawSize().height || 0.0f < pos.y) {
            return false;
        }

        break;
    case 1:
        if (pos.y < -pPane->GetDrawSize().height / 2.0f || pPane->GetDrawSize().height / 2.0f < pos.y) {
            return false;
        }

        break;
    case 2:
        if (pos.y < 0.0f || pPane->GetDrawSize().height < pos.y) {
            return false;
        }

        break;
    }

    return true;
}

LayoutGroupCtrl* LayoutManager::createAndAddGroupCtrl(const char* pName, u32 animLayerNum) {
    LayoutGroupCtrl* pGroupCtrl = new LayoutGroupCtrl(this, pName, animLayerNum);
    addGroupCtrl(pGroupCtrl);
    return pGroupCtrl;
}

s32 LayoutManager::getIndexOfGroupCtrl(const char* pName) const {
    u32 index = 0;
    Ui::GroupList& rGroupList = mLayout->mpGroupContainer->mGroupList;
    for (Ui::Group* pGroup : rGroupList) {
        if (MR::isEqualString(pGroup->mName, pName)) {
            return index;
        }

        index++;
    }

    return index;
}

void LayoutManager::createPaneMtxRef(const char* pName) {
    stateOf(pName == nullptr ? mLayout->mpRootPane : findPaneOrRoot(pName)).mMtxRef = new f32[3][4];
}

MtxPtr LayoutManager::getPaneMtxRef(const char* pName) const {
    const LayoutPaneState* pState = findState(pName == nullptr ? mLayout->mpRootPane : findPaneOrRoot(pName));
    return pState != nullptr ? pState->mMtxRef : nullptr;
}

bool LayoutManager::isExistPaneMtxRef(const char* pName) const {
    return getPaneMtxRef(pName) != nullptr;
}

bool LayoutManager::isPointing(const char* pName, const Vec2f& rPos) const {
    // A pane the layout does not have is never pointed at.
    const Ui::Pane* const pPane = findPaneByName(pName);
    if (pPane == nullptr) {
        return false;
    }

    return isPointing(pPane, rPos);
}

Ui::AnimTransform* LayoutManager::getAnimTransform(const char* pName) const {
    MR::FormattedName fileName("%s.brlan", pName);
    u32 hash = MR::getHashCodeLower(fileName.c_str());
    for (u32 i = 0; i < mLayoutHolder->mAnimRes.mCount; i++) {
        if (mLayoutHolder->isAnimationHashEqual(hash, i)) {
            return mAnimTransList[i];
        }
    }

    return nullptr;
}

void LayoutManager::bindPaneCtrlAnim(LayoutPaneCtrl* pPaneCtrl, Ui::AnimTransform* pAnimTransform) {
    const Ui::Layout::Order& rOrder = mLayout->GetOrder();
    const u32 first = mLayout->GetOrderIndex(pPaneCtrl->mStatePane);
    if (first == Ui::Pane::cNoListIndex) {
        return;
    }
    for (u32 i = first; i < rOrder.mEnd[first];) {
        const LayoutPaneState* pState = findState(rOrder.mPanes[i]);
        if (i != first && pState != nullptr && pState->mPaneCtrl != nullptr) {
            i = rOrder.mEnd[i];
            continue;
        }
        rOrder.mPanes[i]->UnbindAnimationSelf(pAnimTransform);
        rOrder.mPanes[i]->BindAnimation(pAnimTransform, false);
        i++;
    }
}

void LayoutManager::unbindPaneCtrlAnim(LayoutPaneCtrl* pPaneCtrl, Ui::AnimTransform* pAnimTransform) {
    const Ui::Layout::Order& rOrder = mLayout->GetOrder();
    const u32 first = mLayout->GetOrderIndex(pPaneCtrl->mStatePane);
    if (first == Ui::Pane::cNoListIndex) {
        return;
    }
    for (u32 i = first; i < rOrder.mEnd[first];) {
        const LayoutPaneState* pState = findState(rOrder.mPanes[i]);
        if (i != first && pState != nullptr && pState->mPaneCtrl != nullptr) {
            i = rOrder.mEnd[i];
            continue;
        }
        rOrder.mPanes[i]->UnbindAnimationSelf(pAnimTransform);
        i++;
    }
}

void LayoutManager::calcAnimWithoutLocationAdjust(const Ui::DrawInfo& rDrawInfo) {
    animatePanes();
    if (mIsScreenHidden) {
        return;
    }

    // The follows and anchors are part of the pass; the copies come from its final matrices.
    LayoutAspect::prepare(this);
    mLayout->CalculateMtx(rDrawInfo);
    copyPaneMtxRefs();
}

void LayoutManager::copyPaneMtxRef(const Ui::Pane* pPane, MtxPtr pMtxRef) {
    NebulaMtxCopy(pPane->mGlbMtx, pMtxRef);
    if (MR::isScreen16Per9()) {
        pMtxRef[0][3] *= MR::getScreenWidth() / NebulaFrameClock::cLayoutWidth;
    }
}

void LayoutManager::copyPaneMtxRefs() {
    const Ui::Layout::Order& rOrder = mLayout->GetOrder();
    for (u32 i = 0; i < rOrder.mNum; i++) {
        const LayoutPaneState* pState = findState(rOrder.mPanes[i]);
        if (pState != nullptr && pState->mMtxRef != nullptr) {
            copyPaneMtxRef(rOrder.mPanes[i], pState->mMtxRef);
        }
    }
}

Ui::Group* LayoutManager::getGroup(const char* pName) const {
    for (Ui::Group* pGroup : mLayout->mpGroupContainer->mGroupList) {
        if (MR::isEqualString(pGroup->mName, pName)) {
            return pGroup;
        }
    }

    return nullptr;
}

void LayoutManager::initArc(const char* pArchiveName, const char* pLayoutName) {
    mLayoutHolder = MR::createAndAddLayoutHolder(pArchiveName);

    MR::FormattedName layoutResName("%s.brlyt", pLayoutName);
    void* pLayoutRes = mLayoutHolder->GetResource('blyt', layoutResName.c_str(), nullptr);

    mLayout = new Ui::Layout();
    mLayout->Build(pLayoutRes, mLayoutHolder);

    if (mLayoutHolder->mAnimRes.mCount != 0) {
        mAnimTransList = new Ui::AnimTransform*[mLayoutHolder->mAnimRes.mCount];

        for (u32 i = 0; i < mLayoutHolder->mAnimRes.mCount; i++) {
            void* pAnimRes = mLayoutHolder->mAnimRes.getRes(i);
            mAnimTransList[i] = mLayout->CreateAnimTransform(pAnimRes, mLayoutHolder);
        }
    }

    removeUnnecessaryPanes(mLayout->mpRootPane);
}

void LayoutManager::initDrawInfo() {
    Mtx34f mtx;
    NebulaMtxIdentity(mtx);

    mDrawInfo.mViewMtx = mtx;
    mDrawInfo.SetMultipleViewMtxOnDraw(true);
    mDrawInfo.mViewRect = mLayout->GetLayoutRect();
    mDrawInfo.SetLocationAdjust(true);
}

void LayoutManager::initGroupCtrlList() {
    mGroupCtrlCount = mLayout->mpGroupContainer->mGroupList.Size();
    mGroupCtrlList = new LayoutGroupCtrl*[mGroupCtrlCount];

    for (u32 i = 0; i < mGroupCtrlCount; i++) {
        mGroupCtrlList[i] = nullptr;
    }
}

void LayoutManager::initTextBoxRecursive(Ui::Pane* pPane, Ui::Pane* pUserDataPane, const char* pLayoutName, u32 textBoxBufferLength) {
    Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(pPane);

    char userData[9];  // the layout file's 8-byte user data field and the terminator
    LayoutCoreUtil::getPaneUserData(pPane, userData);

    if (!MR::isEqualString("", userData)) {
        pUserDataPane = pPane;
    }

    if (pTextBox != nullptr) {
        if (pUserDataPane != nullptr) {
            MR::FormattedName messageID;
            LayoutCoreUtil::getPaneUserData(pUserDataPane, userData);
            MR::getLayoutMessageID(messageID, pLayoutName, userData);
            LayoutCoreUtil::initTextBoxPane(pTextBox, messageID.c_str(), 0x100);
        } else {
            LayoutCoreUtil::initTextBoxPane(pTextBox, nullptr, textBoxBufferLength);
        }
    }

    Ui::PaneList& rPaneList = pPane->mChildList;
    for (Ui::Pane* pChild : rPaneList) {
        initTextBoxRecursive(pChild, pUserDataPane, pLayoutName, textBoxBufferLength);
    }
}

void LayoutManager::animatePanes() {
    const Ui::Layout::Order& rOrder = mLayout->GetOrder();
    for (u32 i = 0; i < rOrder.mNum; i++) {
        Ui::Pane* const pPane = rOrder.mPanes[i];
        if (const LayoutPaneState* pState = findState(pPane)) {
            if (pState->mPaneCtrl != nullptr) {
                pState->mPaneCtrl->calcAnim();
            }
            for (LayoutGroupCtrlLink* pLink = pState->mGroupCtrlLink; pLink != nullptr; pLink = pLink->mNext) {
                pLink->mGroupCtrl->calcAnim();
            }
        }
        pPane->AnimateSelf(1);
    }
}

Ui::Pane* LayoutManager::getPane(const char* pName) const {
    return pName == nullptr ? mLayout->mpRootPane : mLayout->FindPane(pName);
}

Ui::Pane* LayoutManager::findPaneByName(const char* pName) const {
    Ui::Pane* const pPane = mLayout->FindPane(pName);
    return pPane != nullptr && MR::isEqualString(pPane->mName, pName) ? pPane : nullptr;
}

void LayoutManager::replaceIndDummyTexture() {
    if (!mLayoutHolder->isExistResOther("IndDummy.tpl")) {
        return;
    }

    GameTexture screenTex(MR::getScreenResTIMG());

    mIndDummyTexMap = new Ui::TexMap(screenTex.site());

    void* pIndDummyResStart = mLayoutHolder->getResOther("IndDummy.tpl");
    void* pIndDummyResEnd = reinterpret_cast< void* >(-1);

    for (u32 i = 0; i < mLayoutHolder->getResOtherNum(); i++) {
        void* pRes = mLayoutHolder->getResOther(i);

        if (pIndDummyResStart < pRes && pIndDummyResEnd > pRes) {
            pIndDummyResEnd = pRes;
        }
    }

    const Ui::Layout::Order& rOrder = mLayout->GetOrder();
    for (u32 paneIdx = 0; paneIdx < rOrder.mNum; paneIdx++) {
        Ui::Pane* pCurrPane = rOrder.mPanes[paneIdx];
        Ui::Picture* pPicPane = Ui::PaneCast< Ui::Picture >(pCurrPane);

        if (pPicPane == nullptr) {
            continue;
        }

        Ui::Material* pMaterial = pPicPane->GetMaterial();

        for (u32 texMapIdx = 0; texMapIdx < pMaterial->GetTextureNum(); texMapIdx++) {
            void* pImage = pMaterial->GetTexture(texMapIdx).mImage;

            if (pImage < pIndDummyResStart) {
                continue;
            }

            if (pImage >= pIndDummyResEnd) {
                continue;
            }

            pMaterial->SetTexture(texMapIdx, *mIndDummyTexMap);
        }
    }
}

void LayoutManager::removeUnnecessaryPanes(Ui::Pane* pPane) {
    Local::BitFlag< 128 > languagePanes;
    Local::BitFlag< 128 > currentLanguagePanes;
    Local::collectLanguagePanes(pPane, languagePanes, currentLanguagePanes);
    if (languagePanes.isAnythingTrue()) {
        Local::removeLanguagePanes(pPane, languagePanes, currentLanguagePanes);
    } else {
        Ui::PaneList& rChildren = pPane->mChildList;
        for (Ui::Pane* pChild : rChildren) {
            removeUnnecessaryPanes(pChild);
        }
    }
}

namespace {
    namespace Local {
        template < int N >
        bool BitFlag< N >::isAnythingTrue() const {
            for (u32 i = 0; i < N / 32; i++) {
                if (mBits[i] != 0) {
                    return true;
                }
            }

            return false;
        }

        template < int N >
        void BitFlag< N >::onBit(int index) {
            // A layout with more children than bits (a mod's) marks the first N.
            if (index < 0 || index >= N) {
                return;
            }

            int word = index / 32;
            index -= word * 32;
            mBits[word] |= 1 << index;
        }

        template < int N >
        void BitFlag< N >::offBit(int index) {
            if (index < 0 || index >= N) {
                return;
            }

            int word = index / 32;
            index -= word * 32;
            mBits[word] &= ~(1 << index);
        }

        template < int N >
        bool BitFlag< N >::isTrue(int index) const {
            if (index < 0 || index >= N) {
                return false;
            }

            int word = index / 32;
            index -= word * 32;
            return (mBits[word] & (1 << index)) != 0;
        }
    };  // namespace Local
};  // namespace
