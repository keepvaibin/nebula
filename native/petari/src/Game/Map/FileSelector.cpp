#include "Game/Map/FileSelector.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/FileSelectCameraController.hpp"
#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/Map/FileSelectEffect.hpp"
#include "Game/Map/FileSelectFunc.hpp"
#include "Game/Map/FileSelectItem.hpp"
#include "Game/Map/FileSelectItemDelegator.hpp"
#include "Game/Map/FileSelectSky.hpp"
#include "Game/NPC/MiiFacePartsHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/BackButton.hpp"
#include "Game/Screen/BrosButton.hpp"
#include "Game/Screen/FileSelectButton.hpp"
#include "Game/Screen/FileSelectInfo.hpp"
#include "Game/Screen/InformationMessage.hpp"
#include "Game/Screen/Manual2P.hpp"
#include "Game/Screen/MiiConfirmIcon.hpp"
#include "Game/Screen/MiiSelect.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/Screen/TitleSequenceProduct.hpp"
#include "Game/System/ConfigDataMii.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include <Game/Ui/ResFont.hpp>
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Screen/NebulaFileSelectButton.hpp"
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // The save's user files (SaveDataHandler's cUserFiles).
    const s32 cUserFileNum = 6;
    const f32 cSlopeDegree = 20.0f;
    const f32 cItemPosRadius = 5000.0f;
    const f32 cSelectEffectOffset = 1000.0f;
    const char* const cLuigiNameMessageID = "System_FileSelect_Icon001";
    const char* const cMarioNameMessageID = "System_FileSelect_Icon000";
    const s32 cBgmFarState = 5;
    const u32 cBgmFarStateChangeFrames = 60;
    const s32 cBgmNearState = 6;
    const u32 cBgmNearStateChangeFrames = 60;
    const f32 cItemThetaOffset[cUserFileNum] = {10.0f, -10.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const s32 cIndexOrder[cUserFileNum] = {1, 2, 4, 6, 5, 3};
};  // namespace

namespace {
    inline int getItemArrayIndex(s32 id) {
        const s32* pIndexOrder = cIndexOrder;

        for (int i = 0; i < ARRAY_SIZE(::cIndexOrder); i++) {
            if (id == pIndexOrder[i]) {
                return i;
            }
        }

        return -1;
    }

    MiiFacePartsHolder* getMiiFacePartsHolder() {
        return MR::getSceneObj< MiiFacePartsHolder >(SceneObj_MiiFacePartsHolder);
    }
};  // namespace

namespace NrvFileSelector {
    NEW_NERVE(FileSelectorNrvWaitBind, FileSelector, WaitBind);
    NEW_NERVE(FileSelectorNrvTitle, FileSelector, Title);
    NEW_NERVE(FileSelectorNrvTitleEnd, FileSelector, TitleEnd);
    NEW_NERVE(FileSelectorNrvMiiDataError, FileSelector, MiiDataError);
    NEW_NERVE(FileSelectorNrvMiiDataWait, FileSelector, MiiDataWait);
    NEW_NERVE(FileSelectorNrvMiiDataWaitEnd, FileSelector, MiiDataWaitEnd);
    NEW_NERVE(FileSelectorNrvFileSelectStart, FileSelector, FileSelectStart);
    NEW_NERVE(FileSelectorNrvFileSelect, FileSelector, FileSelect);
    NEW_NERVE(FileSelectorNrvFileConfirmStart, FileSelector, FileConfirmStart);
    NEW_NERVE(FileSelectorNrvFileConfirmMiiDeleteWarningStart, FileSelector, FileConfirmMiiDeleteWarningStart);
    NEW_NERVE(FileSelectorNrvFileConfirmMiiDeleteWarning, FileSelector, FileConfirmMiiDeleteWarning);
    NEW_NERVE(FileSelectorNrvFileConfirmMiiDeleteSave, FileSelector, FileConfirmMiiDeleteSave);
    NEW_NERVE(FileSelectorNrvFileConfirm, FileSelector, FileConfirm);
    NEW_NERVE(FileSelectorNrvDemoStartWait, FileSelector, DemoStartWait);
    NEW_NERVE(FileSelectorNrvDemo, FileSelector, Demo);
    NEW_NERVE(FileSelectorNrvCreateConfirmStart, FileSelector, CreateConfirmStart);
    NEW_NERVE(FileSelectorNrvCreateConfirm, FileSelector, CreateConfirm);
    NEW_NERVE(FileSelectorNrvCreate, FileSelector, Create);
    NEW_NERVE(FileSelectorNrvCopyWait, FileSelector, CopyWait);
    NEW_NERVE(FileSelectorNrvCopySelect, FileSelector, CopySelect);
    NEW_NERVE(FileSelectorNrvCopyConfirmStart, FileSelector, CopyConfirmStart);
    NEW_NERVE(FileSelectorNrvCopyConfirm, FileSelector, CopyConfirm);
    NEW_NERVE(FileSelectorNrvCopySave, FileSelector, CopySave);
    NEW_NERVE(FileSelectorNrvCopySaveMii, FileSelector, CopySaveMii);
    NEW_NERVE(FileSelectorNrvCopyDemo, FileSelector, CopyDemo);
    NEW_NERVE(FileSelectorNrvCopyRejectStart, FileSelector, CopyRejectStart);
    NEW_NERVE(FileSelectorNrvCopyReject, FileSelector, CopyReject);
    NEW_NERVE(FileSelectorNrvMiiWait, FileSelector, MiiWait);
    NEW_NERVE(FileSelectorNrvMiiTip, FileSelector, MiiTip);
    NEW_NERVE(FileSelectorNrvMiiSelectStartFirst, FileSelector, MiiSelectStart);
    NEW_NERVE(FileSelectorNrvMiiSelectStart, FileSelector, MiiSelectStart);
    NEW_NERVE(FileSelectorNrvMiiSelect, FileSelector, MiiSelect);
    NEW_NERVE(FileSelectorNrvMiiCancel, FileSelector, MiiCancel);
    NEW_NERVE(FileSelectorNrvMiiConfirmWait, FileSelector, MiiConfirmWait);
    NEW_NERVE(FileSelectorNrvMiiConfirm, FileSelector, MiiConfirm);
    NEW_NERVE(FileSelectorNrvMiiCreateWait, FileSelector, MiiCreateWait);
    NEW_NERVE(FileSelectorNrvMiiCreateDemo, FileSelector, MiiCreateDemo);
    NEW_NERVE(FileSelectorNrvMiiCaution, FileSelector, MiiCaution);
    NEW_NERVE(FileSelectorNrvMiiInfoStart, FileSelector, MiiInfoStart);
    NEW_NERVE(FileSelectorNrvMiiInfo, FileSelector, MiiInfo);
    NEW_NERVE(FileSelectorNrvDeleteConfirmStart, FileSelector, DeleteConfirmStart);
    NEW_NERVE(FileSelectorNrvDeleteConfirm, FileSelector, DeleteConfirm);
    NEW_NERVE(FileSelectorNrvDelete, FileSelector, Delete);
    NEW_NERVE(FileSelectorNrvDeleteDemo, FileSelector, DeleteDemo);
    NEW_NERVE(FileSelectorNrvFileBroken, FileSelector, FileBroken);
    NEW_NERVE(FileSelectorNrvManualStart, FileSelector, ManualStart);
    NEW_NERVE(FileSelectorNrvManual, FileSelector, Manual);
};  // namespace NrvFileSelector

FileSelector::FileSelector(const char* pName)
    : LiveActor(pName), mCameraController(), mSky(), mItems(), mOperationButton(), mBackButton(), mBrosButton(), mInfoMessage(), mSysInfoWindow(),
      mSelectedItem(), mDecidedItem(), mPointedItem(), mPointCandidate(), mUserFile(), mIsCompleteFile(), mTitle(), mMiiSelect(), mMiiId(new Mii::Id()), mManual(), mIsMiiSelectStartFirst(),
      mBgmState(), mSelectEffect()
      ,
      mQuitButton(), mSettingsButton(), mQuitWindow(), mIsAskingQuit(false), mQuitFrames(-1), mSettingsFrames(-1), mFocusWanted()
{
    lumacore::setClass(this, lumacore::kClassId< FileSelector >);
}

void FileSelector::init(const TableIter& rIter) {
    while (!initStep(rIter, UINT64_MAX)) {}
}

bool FileSelector::initStep(const TableIter& rIter, uint64_t deadline) {
    do {
        switch (mInitStep) {
        case 0:
            MR::connectToScene(this, MR::MovementType_Environment, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
            initHitSensor(1);
            MR::addHitSensorPriorBinder(this, "body", 8, 500.0f, Vec3f(0.0f, 0.0f, 0.0f));
            initUserFileArray();
            MR::invalidateClipping(this);
            break;
        case 1: createCameraController(); break;
        case 2: createSky(); break;
        case 3:
            if (!createFileItemStep()) {
                if (nebula_host()->clock_ns(nebula_host()->user) >= deadline) return false;
                continue;
            }
            break;
        case 4: createOperationButton(); break;
        case 5: createBackButton(); break;
        case 6: createBrosButton(); break;
        case 7: createInfoMessage(); break;
        case 8: createSysInfoWindow(); break;
        case 9: createCornerButtons(); break;
        case 10: createFileInfo(); break;
        case 11: createTitle(); break;
        case 12: createMiiSelect(); break;
        case 13: createMiiConfirmIcon(); break;
        case 14: createManual(); break;
        case 15: createSelectEffect(); break;
        case 16:
            initNerve(GET_NERVE(FileSelector, FileSelectorNrvWaitBind));
            MR::tryRegisterDemoCast(this, rIter);
            appear();
            ++mInitStep;
            return true;
        default: return true;
        }
        ++mInitStep;
    } while (nebula_host()->clock_ns(nebula_host()->user) < deadline);
    return false;
}

void FileSelector::appear() {
    LiveActor::appear();
    mCameraController->appear();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvWaitBind));
}

void FileSelector::kill() {
    mBackButton->kill();
    LiveActor::kill();
}

void FileSelector::callbackStart() {
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvDemoStartWait));
}

void FileSelector::callbackCopy() {
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopyWait));
}

void FileSelector::callbackMii() {
    disappearAllLayout();
    invalidateSelectAll();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiWait));
}

void FileSelector::callbackDelete() {
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvDeleteConfirmStart));
}

void FileSelector::callbackManual() {
    MR::startSystemSE("SE_SY_FILE_SEL_TIPS_OPEN");
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvManualStart));
}

void FileSelector::notifyItem(FileSelectItem* pItem, s32 msg) {
    switch (msg) {
    case 0:
        onPoint(pItem);
        break;
    case 1:
        onSelect(pItem);
        break;
    case 2:
        if (isNerve(GET_NERVE(FileSelector, FileSelectorNrvCopySelect))) {
            onPoint(pItem);
        }

        break;
    default:
        break;
    }
}

void FileSelector::control() {
    mPointCandidate = nullptr;

    for (int i = 0; i < mItems->getObjNum(); i++) {
        LiveActor* pActor = mItems->getActor(i);
        Vec3f& rPosition = pActor->mPosition;
        rPosition = mItemPositions[i] * 0.05f + rPosition * 0.95f;
    }

    Vec3f* pEffectPosition;

    for (int i = 0; i < mItems->getObjNum(); i++) {
        LiveActor* pActor = mItems->getActor(i);
        pEffectPosition = &mSelectEffect[i].mPosition;
        pEffectPosition->set(pActor->mPosition + Vec3f(0.0f, ::cSelectEffectOffset, 0.0f));
    }

    updateBgm();
    updateCornerButtons();
}

bool FileSelector::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg) && isNerve(GET_NERVE(FileSelector, FileSelectorNrvWaitBind))) {
        MR::hidePlayer();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvTitle));

        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        Mtx34f baseMtx;
        baseMtx.identity();

        MR::setPlayerBaseMtx(baseMtx.toMtxPtr());

        return true;
    }

    return false;
}

void FileSelector::initUserFileArray() {
    mUserFile = new UserFile[::cUserFileNum]();
    mIsCompleteFile = new bool[::cUserFileNum];

    initUserFile();
}

void FileSelector::createCameraController() {
    mCameraController = new FileSelectCameraController("ファイルセレクトカメラ制御");
    mCameraController->initWithoutIter();
}

void FileSelector::createSky() {
    mSky = new FileSelectSky("ファイルセレクト画面の空");
    mSky->initWithoutIter();
    mSky->appear();
}

namespace {
    inline FileSelectItem* createFileItem(s32 id) {
        FileSelectIconID iconId = FileSelectIconID();

        return new FileSelectItem(id, true, iconId, "ファイルセレクトアイテム");
    }
};  // namespace

bool FileSelector::createFileItemStep() {
    if (mInitItem >= ::cUserFileNum) return true;
    if (mInitItem == 0u) {
        mItems = new DeriveActorGroup<FileSelectItem>("File select items", ::cUserFileNum);
        mItemPositions = new Vec3f[::cUserFileNum]();
        calcBasePos(0.0f);
        MR::createSceneObj(SceneObj_MiiFacePartsHolder);
        mInitDelegator = new FileSelectItemDelegator<FileSelector>(this, &FileSelector::notifyItem);
    }
    FileSelectItem* item = ::createFileItem(::cIndexOrder[mInitItem]);
    item->initWithoutIter();
    item->setSelectDelegator(mInitDelegator);
    item->mPosition.set(mItemPositions[mInitItem]);
    mItems->registerActor(item);
    return ++mInitItem == ::cUserFileNum;
}

void FileSelector::createOperationButton() {
    mOperationButton = new FileSelectButton("FileSelectButton");
    mOperationButton->initWithoutIter();
    mOperationButton->setCallbackFunctor(MR::Functor(this, &FileSelector::callbackStart), MR::Functor(this, &FileSelector::callbackCopy),
                                         MR::Functor(this, &FileSelector::callbackMii), MR::Functor(this, &FileSelector::callbackDelete),
                                         MR::Functor(this, &FileSelector::callbackManual));
}

void FileSelector::createBackButton() {
    mBackButton = new BackButton("戻るボタン", false);
    mBackButton->initWithoutIter();

    MR::connectToScene(mBackButton, MR::MovementType_Layout, MR::CalcAnimType_Layout, MR::DrawBufferType_None, MR::DrawType_LayoutDecoration);
}

void FileSelector::createBrosButton() {
    mBrosButton = new BrosButton("ルイージ切り替えボタン");
    mBrosButton->initWithoutIter();
}

void FileSelector::createInfoMessage() {
    mInfoMessage = new InformationMessage();
    mInfoMessage->initWithoutIter();
}

void FileSelector::createSysInfoWindow() {
    mSysInfoWindow = MR::createSysInfoWindow();
    MR::connectToSceneLayout(mSysInfoWindow);
    // Its questions (create, copy, delete) come from choosing a file: B goes back, as the Back button does.
    mSysInfoWindow->setBackAnswersNo(true);

    mSysInfoWindowMini = MR::createSysInfoWindowMiniExecuteWithChildren();
    MR::connectToSceneLayout(mSysInfoWindowMini);
}

void FileSelector::createCornerButtons() {
    mQuitButton = new NebulaFileSelectButton("NebulaQuitButton", NebulaFileSelectButton::Role_Quit);
    mQuitButton->initWithoutIter();
    mSettingsButton = new NebulaFileSelectButton("NebulaSettingsButton", NebulaFileSelectButton::Role_Settings);
    mSettingsButton->initWithoutIter();

    mQuitWindow = MR::createSysInfoWindow();
    MR::connectToSceneLayout(mQuitWindow);
    mQuitWindow->setBackAnswersNo(true);
}

// Quit and Settings show while a file is being chosen and nothing else holds the top of the screen: a
// file's information (while the pointer is on a file) or the frame asking to point at the screen.
void FileSelector::updateCornerButtons() {
    const bool isChoosing = isNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect));
    const bool isTopFree = MR::isDead(mFileInfo) && !MR::isExistStarPointerGuidanceFrame1P();
    const bool isHeld = mIsAskingQuit || mSettingsFrames >= 0;
    mQuitButton->setShown(isChoosing && (isHeld || isTopFree));
    mSettingsButton->setShown(isChoosing && (isHeld || isTopFree));
}

// Nebula's menu focus: the files that can be chosen, where they are on screen (the pointer's target on each, its centre
// 900 above the file). While the D-pad or stick holds the highlight, the file it is on stands for the one the pointer
// would point at: it lights up as pointed, and A chooses it.
void FileSelector::offerFilesToMenuFocus() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        FileSelectItem* pItem = static_cast< FileSelectItem* >(mItems->getActor(i));
        Vec2f screen;
        if (!pItem->mIsInvalidateSelect && MR::calcScreenPosition(&screen, pItem->mPosition + Vec3f(0.0f, 900.0f, 0.0f))) {
            NebulaMenuFocus::offerTarget(pItem, screen);
        }
    }
    if (!NebulaMenuFocus::isActive() || !isNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect))) {
        mFocusWanted = nullptr;
    }
    if (!NebulaMenuFocus::isActive()) {
        return;
    }
    mPointCandidate = nullptr;
    for (int i = 0; i < mItems->getObjNum(); i++) {
        FileSelectItem* pItem = static_cast< FileSelectItem* >(mItems->getActor(i));
        if (NebulaMenuFocus::isTargetFocused(pItem)) {
            mPointCandidate = pItem;
        }
    }

    // Up from a file at the top: the nearer of Quit (top left) and Settings (top right), in the top a file's
    // information holds. The information goes (no file is pointed at), the buttons show, the highlight moves to it.
    // Another press first keeps the files.
    if (isNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect)) && mPointCandidate != nullptr &&
        NebulaMenuFocus::getUnmovedPress() == NebulaMenuFocus::Direction_Up) {
        // Left or right of the files' middle, as they are on screen.
        f32 sumX = 0.0f;
        s32 count = 0;
        f32 candidateX = 0.0f;
        for (int i = 0; i < mItems->getObjNum(); i++) {
            FileSelectItem* pItem = static_cast< FileSelectItem* >(mItems->getActor(i));
            Vec2f screen;
            if (MR::calcScreenPosition(&screen, pItem->mPosition)) {
                sumX += screen.x;
                count++;
                if (pItem == mPointCandidate) {
                    candidateX = screen.x;
                }
            }
        }
        mFocusWanted = count > 0 && candidateX > sumX / static_cast< f32 >(count) ? mSettingsButton : mQuitButton;
    } else if (NebulaMenuFocus::getPress() != NebulaMenuFocus::Direction_None) {
        mFocusWanted = nullptr;
    }
    if (mFocusWanted != nullptr) {
        mPointCandidate = nullptr;
        if (mFocusWanted->takeMenuFocus()) {
            mFocusWanted = nullptr;
        }
    }
}

// The quit question; true while it holds the file select.
bool FileSelector::updateQuitQuestion() {
    if (mQuitFrames >= 0) {
        // The picture and sound fade as the cutscene skip's do; then the game's own way out, its screen already
        // black.
        if (++mQuitFrames >= 2 && !MR::isSystemWipeActive()) {
            GameSystemFunction::requestQuitGame(true);
        }
        return true;
    }

    if (!mIsAskingQuit) {
        if (!mQuitButton->isDecided()) {
            return false;
        }

        mIsAskingQuit = true;
        invalidateSelectAll();
        clearPointing();
        // "Please choose a file." is drawn over every layout: it goes while the question is up, as before the game's
        // own questions.
        MR::deactiveStarPointerGuidance();
        mQuitWindow->appearYesNoWithText(NebulaFileSelectButton::getQuitQuestion());
        return true;
    }

    if (!MR::isDead(mQuitWindow)) {
        return true;
    }

    if (mQuitWindow->isSelectedYes()) {
        mQuitFrames = 0;
        MR::closeSystemWipeFade();
        AudSystem* pAudio = AudWrap::getSystem();
        if (pAudio != nullptr) {
            pAudio->fadeOutForQuit();
        }
        return true;
    }

    mIsAskingQuit = false;
    mQuitButton->resume();
    // A pad that chose Quit finds its highlight back on it.
    mFocusWanted = NebulaMenuFocus::isActive() ? mQuitButton : nullptr;
    validateSelectAll();
    MR::activeStarPointerGuidance();
    return false;
}

// Settings: the settings menu opens over the file select, which waits under it (no frame of it runs while the menu is
// open). True from the choice until the menu has closed.
bool FileSelector::updateSettings() {
    if (mSettingsFrames < 0) {
        if (!mSettingsButton->isDecided()) {
            return false;
        }
        mSettingsFrames = 0;
        invalidateSelectAll();
        clearPointing();
        NebulaSettingsMenu::requestOpen();
        return true;
    }
    // The menu opens on the next frame; the file select runs again once it has closed (or was never opened).
    if (++mSettingsFrames < 2 || NebulaSettingsMenu::isFreezingScene()) {
        return true;
    }
    mSettingsFrames = -1;
    mSettingsButton->resume();
    // A pad that chose Settings finds its highlight back on it.
    mFocusWanted = NebulaMenuFocus::isActive() ? mSettingsButton : nullptr;
    validateSelectAll();
    return false;
}

void FileSelector::createFileInfo() {
    mFileInfo = new FileSelectInfo(Mii::NameLength + 1, "ファイル情報");
    mFileInfo->initWithoutIter();
}

void FileSelector::createTitle() {
    mTitle = new TitleSequenceProduct();
    mTitle->kill();
}

void FileSelector::createMiiSelect() {
    mMiiSelect = new MiiSelect("MiiSelect");
    mMiiSelect->initWithoutIter();
}

void FileSelector::createMiiConfirmIcon() {
    mMiiConfirmIcon = new MiiConfirmIcon("Mii確認用アイコン");
    mMiiConfirmIcon->initWithoutIter();
    MR::connectToScene(mMiiConfirmIcon, MR::MovementType_Layout, MR::CalcAnimType_Layout, MR::DrawBufferType_None, MR::DrawType_LayoutDecoration);
}


void FileSelector::createManual() {
    mManual = new Manual2P("２Ｐマニュアル");
    mManual->initWithoutIter();
}

void FileSelector::createSelectEffect() {
    mSelectEffect = new FileSelectEffect[::cUserFileNum]();

    for (int i = 0; i < ::cUserFileNum; i++) {
        mSelectEffect[i].initWithoutIter();
        mSelectEffect[i].mPosition.set(mItemPositions[i]);
        mSelectEffect[i].mScale.set(0.4f);
    }
}

void FileSelector::appearAllItems() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        mItems->getActor(i)->appear();
    }
}

void FileSelector::disappearAllLayout() {
    if (!MR::isDead(mOperationButton)) {
        mOperationButton->disappear();

        if (!MR::isDead(mFileInfo)) {
            mFileInfo->slideBack();
        }
    }

    if (!MR::isDead(mFileInfo)) {
        mFileInfo->disappear();
    }

    if (!mBackButton->isHidden() && !mBackButton->isSelected()) {
        mBackButton->disappear();
    }

    if (!MR::isDead(mBrosButton)) {
        mBrosButton->disappear();
    }
}

bool FileSelector::isHiddenAllLayout() const {
    return MR::isDead(mOperationButton) && MR::isDead(mFileInfo) && mBackButton->isHidden() && MR::isDead(mBrosButton);
}

void FileSelector::updateFileInfo() {
    if (mPointCandidate != nullptr) {
        if (mPointedItem != mPointCandidate) {
            if (mPointedItem != nullptr) {
                clearPointing();
                mFileInfo->disappear();
            }

            if (mPointCandidate->isExist()) {
                setFileInfo(mPointCandidate->mFileNo);
                mFileInfo->appear();
                mFileInfo->forceChange();
            }

            mPointedItem = mPointCandidate;
            mPointedItem->onPointing();
        }
    } else if (mPointedItem != nullptr) {
        clearPointing();
        mFileInfo->disappear();
    }
}

void FileSelector::appearAllIndex() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        static_cast< FileSelectItem* >(mItems->getActor(i))->appearIndex();
    }
}

void FileSelector::disappearAllIndex() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        static_cast< FileSelectItem* >(mItems->getActor(i))->disappearIndex();
    }
}

void FileSelector::invalidateSelectAll() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        static_cast< FileSelectItem* >(mItems->getActor(i))->invalidateSelect();
    }
}

void FileSelector::validateSelectAll() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        static_cast< FileSelectItem* >(mItems->getActor(i))->validateSelect();
    }
}

void FileSelector::initUserFile() {
    for (int i = 0; i < ::cUserFileNum; i++) {
        GameSequenceFunction::restoreUserFile(&mUserFile[i], i + 1);
    }

    checkAllComplete();
}

void FileSelector::restoreUserFile() {
    for (int i = 0; i < ::cUserFileNum; i++) {
        GameSequenceFunction::restoreUserFile(&mUserFile[i], i + 1, mUserFile[i].mIsPlayerMario);
    }

    checkAllComplete();
}

void FileSelector::checkAllComplete() {
    bool isPlayerMario;

    for (int i = 0; i < ::cUserFileNum; i++) {
        isPlayerMario = mUserFile[i].mIsPlayerMario;
        mIsCompleteFile[i] = false;

        if (!isPlayerMario) {
            GameSequenceFunction::restoreUserFile(&mUserFile[i], i + 1, true);
        }

        if (mUserFile[i].isPowerStarGetFinalChallengeGalaxy()) {
            GameSequenceFunction::restoreUserFile(&mUserFile[i], i + 1, false);

            if (mUserFile[i].isPowerStarGetFinalChallengeGalaxy()) {
                mIsCompleteFile[i] = true;
            }
        }

        if (isPlayerMario != mUserFile[i].mIsPlayerMario) {
            GameSequenceFunction::restoreUserFile(&mUserFile[i], i + 1, isPlayerMario);
        }
    }
}

void FileSelector::onPoint(FileSelectItem* pItem) {
    if (!mBackButton->isHidden() && mBackButton->isPointing()) {
        return;
    }

    if (mQuitButton->isPointing() || mSettingsButton->isPointing()) {
        return;
    }

    if (mPointCandidate != nullptr) {
        if (mPointCandidate->mFileNo > pItem->mFileNo) {
            mPointCandidate = pItem;
        }
    } else {
        mPointCandidate = pItem;
    }
}

void FileSelector::onSelect(FileSelectItem* pItem) {
    if (isNerve(GET_NERVE(FileSelector, FileSelectorNrvCopySelect))) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopyConfirmStart));
    } else if (isNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect))) {
        if (pItem->isNew()) {
            setNerve(GET_NERVE(FileSelector, FileSelectorNrvCreateConfirmStart));
        } else {
            setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirmStart));
        }
    } else {
        return;
    }

    mSelectedItem = pItem;

    MR::startSystemSE("SE_SY_GALAXY_SELECTED");
    playSelectedME();
}

void FileSelector::clearPointing() {
    if (mPointedItem == nullptr) {
        return;
    }

    mPointedItem->offPointing();
    mPointedItem = nullptr;
}

namespace {
    inline NebulaCalendar::SaveTime getUserFileLastModified(const FileSelector* pSelector, s32 id) {
        return pSelector->mUserFile[id - 1].getLastModified();
    }

    inline bool isUserFileViewCompleteEnding(const FileSelector* pSelector, s32 id) {
        return pSelector->mUserFile[id - 1].isViewCompleteEnding();
    }

    inline bool isUserFileViewNormalEnding(const FileSelector* pSelector, s32 id) {
        return pSelector->mUserFile[id - 1].isViewNormalEnding();
    }

    inline s32 getUserFileStarPieceNum(const FileSelector* pSelector, s32 id) {
        return pSelector->mUserFile[id - 1].getStarPieceNum();
    }

    inline s32 getUserFilePowerStarNum(const FileSelector* pSelector, s32 id) {
        return pSelector->mUserFile[id - 1].getPowerStarNum();
    }

    inline void copyIconMiiName(const FileSelector* pSelector, u16* pName, s32 id) {
        FileSelectIconID iconId = FileSelectIconID();

        pSelector->getIconId(&iconId, id);
        FileSelectFunc::copyMiiName(pName, iconId);
    }
};  // namespace

void FileSelector::setFileInfo(s32 id) {
    NebulaCalendar::Date date;
    u16 nameBuffer[Mii::NameLength + 1];
    MR::Text16 dateText;
    MR::Text16 timeText;

    if (static_cast< FileSelectItem* >(mItems->getActor(::getItemArrayIndex(id)))->mIsMiiBroken) {
        FileSelectFunc::copyMiiName(nameBuffer, FileSelectIconID());
    } else {
        ::copyIconMiiName(this, nameBuffer, id);
    }

    date = NebulaCalendar::toDate(::getUserFileLastModified(this, id));

    MR::makeDateString(dateText, date.mYear, date.mMonth, date.mDay);
    MR::makeTimeString(timeText, date.mHour, date.mMinute);

    bool isViewCompleteEnding = ::isUserFileViewCompleteEnding(this, id);
    bool isViewNormalEnding = ::isUserFileViewNormalEnding(this, id);
    s32 starPieceNum = ::getUserFileStarPieceNum(this, id);
    s32 powerStarNum = ::getUserFilePowerStarNum(this, id);
    s32 missCount = getMissCount(id);
    bool isUserFileMario = !isUserFileLuigi(id);

    mFileInfo->setInfo(nameBuffer, id, powerStarNum, starPieceNum, isUserFileMario, isViewNormalEnding, isViewCompleteEnding, dateText.c_str(), timeText.c_str(),
                       missCount);
}

bool FileSelector::checkSelectedBackButton() {
    if (mBackButton->isHidden()) {
        return false;
    }

    if (mBackButton->isSelected()) {
        return true;
    }

    if (MR::testSystemTriggerB()) {
        MR::startSystemSE("SE_SY_GALAXY_DECIDE_CANCEL");
        mBackButton->disappear();

        return true;
    }

    return false;
}

void FileSelector::goToNearPoint() {
    calcBasePos(-16000.0f);
    mCameraController->goToNearPoint(mItemPositions[::getItemArrayIndex(mSelectedItem->mFileNo)]);
}

namespace {
    inline f32 calcSin(f32 angle) {
        f32 r = sin(angle);
        return r;
    }

    inline f32 calcCos(f32 angle) {
        f32 r = cos(angle);
        return r;
    }

    class RotateMtx : public Pos3f {
    public:
        f32& ref(int x, int y) {
            return mMtx[x][y];
        }

        void makeRotate(const Vec3f& rAxis, f32 angle) {
            zeroTrans();
            setRotate(rAxis, angle);
        }

        void setRotate(const Vec3f& rAxis, f32 angle) {
            Vec3f vec;
            vec.normalize(rAxis);

            f32 s = calcSin(angle);
            f32 c = calcCos(angle);

            f32 negc = 1.0f - c;

            f32 x, y, z;

            x = vec.x;
            y = vec.y;
            z = vec.z;

            f32 xx, yy, zz;
            xx = x * x;
            yy = y * y;
            zz = z * z;

            ref(0, 0) = c + negc * xx;
            ref(0, 1) = negc * x * y - s * z;
            ref(0, 2) = negc * x * z + s * y;
            ref(1, 0) = negc * x * y + s * z;
            ref(1, 1) = c + negc * yy;
            ref(1, 2) = negc * y * z - s * x;
            ref(2, 0) = negc * x * z - s * y;
            ref(2, 1) = negc * y * z + s * x;
            ref(2, 2) = c + negc * zz;
        }
    };
};  // namespace

void FileSelector::calcBasePos(f32 offset) {
    f32 thetaStep = 2.0f * PI / ::cUserFileNum;
    Pos3f translationMtx;
    Pos3f& translation = translationMtx;
    translation.makeTrans(0.0f, offset, 0.0f);

    RotateMtx rotation;
    rotation.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), ::cSlopeDegree * PI / 180.0f);

    Pos3f transform;
    transform.concat(translation, rotation);

    for (int i = 0; i < ::cUserFileNum; i++) {
        if (mSelectedItem != nullptr && mSelectedItem == mItems->getActor(i)) {
            continue;
        }

        f32 theta = static_cast< f32 >(-(i + 4)) * thetaStep - ::cItemThetaOffset[i] * MR::pi() / 180.0f;
        f32 cosTheta = MR::cos(theta);
        f32 sinTheta = MR::sin(theta);
        mItemPositions[i].set(::cItemPosRadius * cosTheta, 0.0f, ::cItemPosRadius * sinTheta);
        transform.mult(mItemPositions[i], mItemPositions[i]);
    }
}

namespace {
    inline void initFileItem(FileSelector* pSelector, FileSelectItem* pItem) {
        s32 id = pItem->mFileNo;

        if (!pSelector->mUserFile[id - 1].isCreated()) {
            return;
        }

        u32 tempIconId;
        Mii::Id tempMiiId;
        FileSelectIconID iconId = FileSelectIconID();

        if (pSelector->mUserFile[id - 1].getIconId(&tempIconId)) {
            iconId.setFellowID(pSelector->getUserFileFellowID(id));
        } else if (pSelector->mUserFile[id - 1].getMiiId(&tempMiiId)) {
            if (::getMiiFacePartsHolder()->isError()) {
                pItem->mIsMiiBroken = true;
            } else if (pSelector->isUserFileMiiIdValid(id)) {
                u32 miiIndex = pSelector->getUserFileMiiIndex(id);

                iconId.setMiiIndex(miiIndex);
            } else {
                pItem->mIsMiiBroken = true;
            }
        }

        pItem->forceChange(iconId, pSelector->mIsCompleteFile[id - 1]);
    }
};  // namespace

void FileSelector::initAllItems() {
    for (int i = 0; i < ::cUserFileNum; i++) {
        ::initFileItem(this, static_cast< FileSelectItem* >(mItems->getActor(i)));
    }
}

void FileSelector::validateRotateAllItems() {
    for (int i = 0; i < mItems->getObjNum(); i++) {
        static_cast< FileSelectItem* >(mItems->getActor(i))->validateRotate();
    }
}

FileSelectIconID::EFellowID FileSelector::getUserFileFellowID(s32 id) const {
    u32 iconId;

    if (mUserFile[id - 1].getIconId(&iconId)) {
        FileSelectIconID::EFellowID fellowId = FileSelectIconID::Mario;

        // An icon number of 0 (a Mii) or past Peach shows Mario.
        if (iconId >= ConfigDataMii::IconId_Mario && iconId <= ConfigDataMii::IconId_Peach) {
            fellowId = static_cast< FileSelectIconID::EFellowID >(iconId - ConfigDataMii::IconId_Mario);
        }

        return fellowId;
    }

    return FileSelectIconID::Mario;
}

bool FileSelector::isUserFileMiiIdValid(s32 id) const {
    Mii::Id createId;

    return mUserFile[id - 1].getMiiId(&createId) && Mii::find(createId, nullptr);
}

u32 FileSelector::getUserFileMiiIndex(s32 id) const {
    Mii::Id createId;
    u32 index;

    if (mUserFile[id - 1].getMiiId(&createId) && Mii::find(createId, &index)) {
        return index;
    }

    return 0;
}

bool FileSelector::isUserFileCorrupted(s32 id) const {
    id--;

    bool corrupted = false;

    if (mUserFile[id].mIsGameDataCorrupted || mUserFile[id].mIsConfigDataCorrupted) {
        corrupted = true;
    }

    return corrupted;
}

bool FileSelector::isUserFileAppearLuigi(s32 id) const {
    id--;

    return !mUserFile[id].mIsPlayerMario || mUserFile[id].isViewCompleteEnding();
}

bool FileSelector::isUserFileLuigi(s32 id) const {
    id--;

    return !mUserFile[id].mIsPlayerMario;
}

void FileSelector::setUserFileMario(s32 id, bool isMario) {
    id--;

    mUserFile[id].mIsPlayerMario = isMario;
}

void FileSelector::storeSetMiiIdUserFile(s32 id, const FileSelectIconID& rIconId) {
    if (rIconId.isFellow()) {
        u32 fellowId = rIconId.getFellowID() + 1;

        GameSequenceFunction::storeMiiOrIconIdUserFileSequence(id, nullptr, &fellowId);
    } else {
        getMiiId(mMiiId, rIconId);
        GameSequenceFunction::storeMiiOrIconIdUserFileSequence(id, mMiiId, nullptr);
    }

    GameSequenceFunction::startSaveAllUserFileSequence();
}

void FileSelector::getMiiId(Mii::Id* pCreateId, const FileSelectIconID& rIconId) const {
    Mii::Character mii;

    if (Mii::character(Mii::Ref{Mii::SourceLibrary, rIconId.getMiiIndex()}, &mii)) {
        *pCreateId = Mii::idOf(mii);
    }
}

void FileSelector::getIconId(FileSelectIconID* pIconId, s32 id) const {
    u32 iconId;
    Mii::Id miiId;

    if (mUserFile[id - 1].getIconId(&iconId)) {
        FileSelectIconID::EFellowID fellowId = getUserFileFellowID(id);

        pIconId->setFellowID(fellowId);
    } else if (mUserFile[id - 1].getMiiId(&miiId) && isUserFileMiiIdValid(id)) {
        u32 miiIndex = getUserFileMiiIndex(id);

        pIconId->setMiiIndex(miiIndex);
    } else {
        pIconId->setFellowID(FileSelectIconID::Mario);
    }
}

s32 FileSelector::getMissCount(s32 id) const {
    if (!mIsCompleteFile[id - 1]) {
        return -1;
    }

    return mUserFile[id - 1].getPlayerMissNum();
}

void FileSelector::playSelectedME() {
    switch (MR::getRandom(0, 4)) {
    case 0:
        MR::startSystemME("ME_ASTRO_DOME_SELECT1");
        break;
    case 1:
        MR::startSystemME("ME_ASTRO_DOME_SELECT2");
        break;
    case 2:
        MR::startSystemME("ME_ASTRO_DOME_SELECT3");
        break;
    case 3:
        MR::startSystemME("ME_ASTRO_DOME_SELECT4");
        break;
    default:
        break;
    }
}

void FileSelector::updateBgm() {
    if (mCameraController == nullptr) {
        return;
    }

    s32 state = 0;
    u32 stateChangeFrames = 0;

    if (mCameraController->isToOrAtFarPoint()) {
        state = ::cBgmFarState;
        stateChangeFrames = ::cBgmFarStateChangeFrames;
    } else if (mCameraController->isToOrAtNearPoint()) {
        state = ::cBgmNearState;
        stateChangeFrames = ::cBgmNearStateChangeFrames;
    }

    if (mBgmState == state) {
        return;
    }

    MR::setStageBGMState(state, stateChangeFrames);

    mBgmState = state;
}

void FileSelector::exeWaitBind() {
    mPosition.set(*MR::getPlayerPos());
}

void FileSelector::exeTitle() {
    if (MR::isFirstStep(this)) {
        mTitle->appear();
        MR::deactivateDefaultGameLayout();
        MR::startStarPointerModeTitle(this);
        MR::resetCameraMan();
    }

    mTitle->updateNerve();

    if (mTitle->isActive()) {
        return;
    }

    if (::getMiiFacePartsHolder()->isInitEnd()) {
        if (::getMiiFacePartsHolder()->isError()) {
            setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiDataError));
        } else {
            setNerve(GET_NERVE(FileSelector, FileSelectorNrvTitleEnd));
        }
    } else {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiDataWait));
    }
}

void FileSelector::exeTitleEnd() {
    if (MR::isFirstStep(this)) {
        mCameraController->goToFarPoint();
        calcBasePos(0.0f);
        appearAllItems();
        initAllItems();
        mMiiSelect->collectValidMiiIndex();
        invalidateSelectAll();
        MR::startStarPointerModeFileSelect(this);
        MR::startStageBGM("MBGM_FILE_SELECT", false);
    }

    if (mCameraController->isAtFarPoint()) {
        validateRotateAllItems();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect));
    }
}

void FileSelector::exeMiiDataError() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear("MII_02", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSysInfoWindow)) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvTitleEnd));
    }
}

void FileSelector::exeMiiDataWait() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowMini->appear("MII_01", SysInfoWindow::Type_Blocking, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (!::getMiiFacePartsHolder()->isInitEnd()) {
        return;
    }

    if (!mSysInfoWindowMini->isWait()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiDataWaitEnd));
}

void FileSelector::exeMiiDataWaitEnd() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowMini->disappear();
    }

    if (MR::isDead(mSysInfoWindowMini)) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvTitleEnd));
    }
}

void FileSelector::exeFileSelectStart() {
    if (MR::isFirstStep(this)) {
        mCameraController->goToFarPoint();
        calcBasePos(0.0f);
    }

    if (mCameraController->isAtFarPoint()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect));
    }
}

void FileSelector::exeFileSelect() {
    if (MR::isFirstStep(this)) {
        NebulaHost::presence(nullptr, nullptr, -1);  // Discord: choosing a file
        validateSelectAll();
        appearAllIndex();
        MR::activeStarPointerGuidance();
    } else {
        if (updateQuitQuestion() || updateSettings()) {
            return;
        }
        offerFilesToMenuFocus();
        updateFileInfo();
    }

    MR::requestFileSelectGuidance();
}

void FileSelector::exeFileConfirmStart() {
    if (MR::isFirstStep(this)) {
        goToNearPoint();
        invalidateSelectAll();
        disappearAllIndex();
        mSelectedItem->turnToFront(40);
    }

    if (!mCameraController->isAtNearPoint()) {
        return;
    }

    if (isUserFileCorrupted(mSelectedItem->mFileNo)) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileBroken));
    } else if (mSelectedItem->mIsMiiBroken != 0) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirmMiiDeleteWarningStart));
    } else {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
    }
}

void FileSelector::exeFileConfirmMiiDeleteWarningStart() {
    if (MR::isFirstStep(this)) {
        disappearAllLayout();
    }

    if (isHiddenAllLayout()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirmMiiDeleteWarning));
    }
}

void FileSelector::exeFileConfirmMiiDeleteWarning() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowMini->appear("System_FileSelect009", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSysInfoWindowMini)) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirmMiiDeleteSave));
    }
}

void FileSelector::exeFileConfirmMiiDeleteSave() {
    if (MR::isFirstStep(this)) {
        u32 tempIconId = 1;

        GameSequenceFunction::startSetMiiOrIconIdUserFileSequence(mSelectedItem->mFileNo, nullptr, &tempIconId);
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    mSelectedItem->mIsMiiBroken = 0;

    restoreUserFile();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
}

void FileSelector::exeFileConfirm() {
    if (MR::isFirstStep(this)) {
        mOperationButton->appear();
        mBackButton->appear();

        if (isUserFileAppearLuigi(mSelectedItem->mFileNo)) {
            bool isAppear = !isUserFileLuigi(mSelectedItem->mFileNo);

            mBrosButton->appear(isAppear);
        }

        if (MR::isDead(mFileInfo)) {
            setFileInfo(mSelectedItem->mFileNo);
            mFileInfo->appear();
            mFileInfo->forceChange();
        }

        mFileInfo->slide();
    }

    if (!MR::isDead(mBrosButton) && mBrosButton->isSelected()) {
        s32 id = mSelectedItem->mFileNo;

        if (mBrosButton->isSelectedMario()) {
            setUserFileMario(id, true);
        } else {
            setUserFileMario(id, false);
        }

        restoreUserFile();
        setFileInfo(id);
        mFileInfo->change();
        mBrosButton->resume();
    }

    if (!checkSelectedBackButton()) {
        return;
    }

    disappearAllLayout();
    clearPointing();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelectStart));
}

void FileSelector::exeDemoStartWait() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_FILE_SELECTED");
        MR::stopStageBGM(90);
        disappearAllLayout();
        MR::closeWipeFade(60);
    }

    if (!MR::isWipeBlank()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvDemo));
}

void FileSelector::exeDemo() {
    if (MR::isFirstStep(this)) {
        s32 id = mSelectedItem->mFileNo;
        bool isStartLoadSequence = !isUserFileLuigi(id);

        GameSequenceFunction::startGameDataLoadSequence(id, isStartLoadSequence);
        MR::stopStageBGM(90);
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    char16_t name[Mii::NameLength + 1];
    FileSelectIconID iconId = FileSelectIconID();

    getIconId(&iconId, mSelectedItem->mFileNo);

    if (iconId.isMii()) {
        FileSelectFunc::copyMiiName(reinterpret_cast< u16* >(name), iconId);
    } else if (isUserFileLuigi(mSelectedItem->mFileNo)) {
        const char16_t* pMessage = MR::getGameMessageDirect(::cLuigiNameMessageID);

        MR::copyString(name, pMessage, ARRAY_SIZE(name));
    } else {
        const char16_t* pMessage = MR::getGameMessageDirect(::cMarioNameMessageID);

        MR::copyString(name, pMessage, ARRAY_SIZE(name));
    }

    // A player name message past the buffer is cut and ended there.
    name[ARRAY_SIZE(name) - 1] = 0;
    GameSequenceFunction::reserveUserName(name);
    MR::requestChangeStageInGameAfterLoadingGameData();
}

void FileSelector::exeCreateConfirmStart() {
    if (MR::isFirstStep(this)) {
        goToNearPoint();
        invalidateSelectAll();
        disappearAllIndex();
    }

    if (!mCameraController->isAtNearPoint()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCreateConfirm));
}

void FileSelector::exeCreateConfirm() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear("System_FileSelect001", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        mSysInfoWindow->setYesNoSelectorSE("SE_SY_BUTTON_CURSOR_ON", "SE_SY_FILE_SEL_NEW_FILE", "SE_SY_TALK_SELECT_NO");
        // Yes saves, under the game's Saving... window.
        mSysInfoWindow->setDimHandOver(SysInfoWindow::DimHandOver_Yes);
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    mSysInfoWindow->resetYesNoSelectorSE();

    if (mSysInfoWindow->isSelectedYes()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvCreate));
    } else {
        clearPointing();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelectStart));
    }
}

void FileSelector::exeCreate() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::startCreateUserFileSequence(mSelectedItem->mFileNo);
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    if (GameSequenceFunction::isSuccessSaveDataHandleSequence()) {
        restoreUserFile();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStartFirst));
    } else {
        clearPointing();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelectStart));
    }
}

void FileSelector::exeCopyWait() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE");

        mDecidedItem = mSelectedItem;

        disappearAllLayout();
        clearPointing();
        mCameraController->goToFarPoint();
        calcBasePos(0.0f);
    }

    if (!mCameraController->isAtFarPoint()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopySelect));
}

void FileSelector::exeCopySelect() {
    if (MR::isFirstStep(this)) {
        validateSelectAll();
        mDecidedItem->invalidateSelect();
        mBackButton->appear();
        appearAllIndex();
        MR::activeStarPointerGuidance();
        mSelectEffect[::getItemArrayIndex(mDecidedItem->mFileNo)].appear();
    }

    MR::requestFileSelectCopyGuidance();
    offerFilesToMenuFocus();
    updateFileInfo();

    if (mBackButton->isPointing()) {
        clearPointing();
        mFileInfo->disappear();
    }

    if (!checkSelectedBackButton()) {
        return;
    }

    invalidateSelectAll();
    mSelectEffect[::getItemArrayIndex(mDecidedItem->mFileNo)].disappear();
    mInfoMessage->disappear();
    MR::deactiveStarPointerGuidance();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirmStart));
}

void FileSelector::exeCopyConfirmStart() {
    if (MR::isFirstStep(this)) {
        disappearAllLayout();
        invalidateSelectAll();
        mSelectEffect[::getItemArrayIndex(mDecidedItem->mFileNo)].disappear();
    }

    if (!isHiddenAllLayout()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopyConfirm));
}

void FileSelector::exeCopyConfirm() {
    if (MR::isFirstStep(this)) {
        clearPointing();

        if (mSelectedItem->isNew()) {
            mSysInfoWindow->appear("System_FileSelect016", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center,
                                   SysInfoWindow::MessageType_System);
        } else {
            mSysInfoWindow->appear("System_FileSelect014", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center,
                                   SysInfoWindow::MessageType_System);
        }

        mSysInfoWindow->setTextBoxArgNumber(mDecidedItem->mFileNo, 0);
        mSysInfoWindow->setTextBoxArgNumber(mSelectedItem->mFileNo, 1);
        // Yes saves, under the game's Saving... window.
        mSysInfoWindow->setDimHandOver(SysInfoWindow::DimHandOver_Yes);
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    if (mSysInfoWindow->isSelectedYes()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopySave));
    } else {
        mSelectedItem = mDecidedItem;

        setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopySelect));
    }
}

void FileSelector::exeCopySave() {
    if (MR::isFirstStep(this)) {
        if (isUserFileLuigi(mDecidedItem->mFileNo)) {
            setUserFileMario(mSelectedItem->mFileNo, false);
        } else {
            setUserFileMario(mSelectedItem->mFileNo, true);
        }

        if (mSelectedItem->isNew()) {
            GameSequenceFunction::startCopyUserFileSequence(mSelectedItem->mFileNo, mDecidedItem->mFileNo);
        } else {
            GameSequenceFunction::storeCopyUserFileSequence(mSelectedItem->mFileNo, mDecidedItem->mFileNo);
            setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopySaveMii));
            return;
        }
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopyDemo));
}

void FileSelector::exeCopySaveMii() {
    if (MR::isFirstStep(this)) {
        Mii::Id createId;

        if (mSelectedItem->mIsMiiBroken && mUserFile[mSelectedItem->mFileNo - 1].getMiiId(&createId)) {
            GameSequenceFunction::storeMiiOrIconIdUserFileSequence(mSelectedItem->mFileNo, &createId, nullptr);
            GameSequenceFunction::startSaveAllUserFileSequence();
        } else {
            FileSelectIconID iconId = FileSelectIconID();

            mSelectedItem->copyIconID(&iconId);
            storeSetMiiIdUserFile(mSelectedItem->mFileNo, iconId);
        }
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopyDemo));
}

void FileSelector::exeCopyDemo() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_FILE_SEL_COPY");
        restoreUserFile();

        FileSelectIconID iconId = FileSelectIconID();

        getIconId(&iconId, mSelectedItem->mFileNo);
        mSelectedItem->change(iconId, mIsCompleteFile[mSelectedItem->mFileNo - 1]);
        mBackButton->disappear();
        mInfoMessage->disappear();
        invalidateSelectAll();
    }

    if (!mSelectedItem->isExist()) {
        return;
    }

    clearPointing();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelect));
}

void FileSelector::exeCopyRejectStart() {
    if (MR::isFirstStep(this)) {
        disappearAllLayout();
    }

    if (!isHiddenAllLayout()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvCopyReject));
}

void FileSelector::exeCopyReject() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowMini->appear("System_FileSelect003", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        MR::startSystemSE("SE_SY_FILE_SEL_NG");
    }

    if (!MR::isDead(mSysInfoWindowMini)) {
        return;
    }

    mOperationButton->shiftSelect();
    setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
}

void FileSelector::exeMiiWait() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE");
    }

    if (!mBackButton->isHidden()) {
        return;
    }

    if (!MR::isDead(mFileInfo)) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStart));
}

void FileSelector::exeMiiTip() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear("System_FileSelect004", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        clearPointing();
        mFileInfo->disappear();
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStart));
}

void FileSelector::exeMiiSelectStart() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStart))) {
            FileSelectIconID iconId = FileSelectIconID();

            mSelectedItem->copyIconID(&iconId);
            mMiiSelect->prohibitIcon(iconId);
        } else {
            mIsMiiSelectStartFirst = true;

            mMiiSelect->admitIcon();
        }

        if (isUserFileAppearLuigi(mSelectedItem->mFileNo)) {
            mMiiSelect->validateAllSpecialMii();
        } else {
            mMiiSelect->invalidateSpecialMii(FileSelectIconID::Luigi);
        }

        mMiiSelect->appear();
    }

    if (mMiiSelect->isAppearing()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelect));
}

void FileSelector::exeMiiSelect() {
    if (MR::isFirstStep(this)) {
        if (!mIsMiiSelectStartFirst) {
            mBackButton->appear();
        }
    }

    if (mMiiSelect->isDummySelected()) {
        MR::startSystemSE("SE_SY_FILE_SEL_MII_SELECTED");
        mMiiSelect->disappear();

        if (!mBackButton->isHidden()) {
            mBackButton->disappear();
        }

        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiInfoStart));
    } else if (mMiiSelect->isSelected()) {
        MR::startSystemSE("SE_SY_FILE_SEL_MII_SELECTED");
        mMiiSelect->disappear();

        if (!mBackButton->isHidden()) {
            mBackButton->disappear();
        }

        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiConfirmWait));
    } else if (checkSelectedBackButton()) {
        mMiiSelect->disappear();
        mBackButton->disappear();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiCancel));
    }
}

void FileSelector::exeMiiCancel() {
    if (!MR::isDead(mMiiSelect)) {
        return;
    }

    if (!mBackButton->isHidden()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
}

void FileSelector::exeMiiConfirmWait() {
    if (!MR::isDead(mMiiSelect)) {
        return;
    }

    if (!mBackButton->isHidden()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiConfirm));
}

void FileSelector::exeMiiConfirm() {
    if (MR::isFirstStep(this)) {
        if (mIsMiiSelectStartFirst) {
            mSysInfoWindow->appear("System_FileSelect013", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Bottom,
                                   SysInfoWindow::MessageType_System);
        } else {
            mSysInfoWindow->appear("System_FileSelect005", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Bottom,
                                   SysInfoWindow::MessageType_System);
        }

        u16 name[Mii::NameLength + 1];
        FileSelectIconID iconId = FileSelectIconID();

        mMiiSelect->getSelectedID(&iconId);
        FileSelectFunc::copyMiiName(name, iconId);
        mMiiConfirmIcon->appear(mMiiSelect->getSelectedMiiTexMap(), reinterpret_cast< char16_t* >(name));
        mSysInfoWindow->setYesNoSelectorSE("SE_SY_BUTTON_CURSOR_ON", "SE_SY_FILE_SEL_MII_CHANGE", "SE_SY_TALK_SELECT_NO");
        // Yes saves, under the game's Saving... window.
        mSysInfoWindow->setDimHandOver(SysInfoWindow::DimHandOver_Yes);
    }

    if (!mMiiConfirmIcon->isDisappear() && mSysInfoWindow->isDisappear()) {
        mMiiConfirmIcon->disappear();
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    mSysInfoWindow->resetYesNoSelectorSE();

    if (mSysInfoWindow->isSelectedYes()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiCreateWait));
    } else if (mIsMiiSelectStartFirst) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStartFirst));
    } else {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStart));
    }
}

void FileSelector::exeMiiCreateWait() {
    if (MR::isFirstStep(this)) {
        FileSelectIconID iconId = FileSelectIconID();

        mMiiSelect->getSelectedID(&iconId);
        storeSetMiiIdUserFile(mSelectedItem->mFileNo, iconId);

        mIsMiiSelectStartFirst = false;
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiCreateDemo));
}

void FileSelector::exeMiiCreateDemo() {
    if (MR::isFirstStep(this)) {
        restoreUserFile();

        s32 id = mSelectedItem->mFileNo;
        FileSelectIconID iconId = FileSelectIconID();

        mMiiSelect->getSelectedID(&iconId);
        mSelectedItem->change(iconId, mIsCompleteFile[id - 1]);
        setFileInfo(id);
    }

    if (!mSelectedItem->isExist()) {
        return;
    }

    FileSelectIconID iconId = FileSelectIconID();

    mMiiSelect->getSelectedID(&iconId);

    if (iconId.isMii()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiCaution));
    } else {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
    }
}

void FileSelector::exeMiiCaution() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowMini->appear("System_FileSelect006", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSysInfoWindowMini)) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
    }
}

void FileSelector::exeMiiInfoStart() {
    if (!MR::isDead(mMiiSelect)) {
        return;
    }

    if (!mBackButton->isHidden()) {
        return;
    }

    setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiInfo));
}

void FileSelector::exeMiiInfo() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowMini->appear("System_FileSelect015", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (!MR::isDead(mSysInfoWindowMini)) {
        return;
    }

    if (mIsMiiSelectStartFirst) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStartFirst));
    } else {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvMiiSelectStart));
    }
}

void FileSelector::exeDeleteConfirmStart() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE");
        disappearAllLayout();
    }

    if (isHiddenAllLayout()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvDeleteConfirm));
    }
}

void FileSelector::exeDeleteConfirm() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->setYesNoSelectorSE("SE_SY_BUTTON_CURSOR_ON", "SE_SY_FILE_SEL_DELETE", "SE_SY_TALK_SELECT_NO");
        mSysInfoWindow->appear("System_FileSelect007", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        // Yes saves, under the game's Saving... window.
        mSysInfoWindow->setDimHandOver(SysInfoWindow::DimHandOver_Yes);
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    mSysInfoWindow->resetYesNoSelectorSE();

    if (mSysInfoWindow->isSelectedYes()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvDelete));
    } else {
        mOperationButton->shiftSelect();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
    }
}

void FileSelector::exeDelete() {
    if (MR::isFirstStep(this)) {
        setUserFileMario(mSelectedItem->mFileNo, true);
        GameSequenceFunction::startDeleteUserFileSequence(mSelectedItem->mFileNo);
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    if (GameSequenceFunction::isSuccessSaveDataHandleSequence()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvDeleteDemo));
    } else {
        clearPointing();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelectStart));
    }
}

void FileSelector::exeDeleteDemo() {
    if (MR::isFirstStep(this)) {
        mSelectedItem->format();
    }

    if (mSelectedItem->isNew()) {
        initUserFile();
        clearPointing();
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileSelectStart));
    }
}

void FileSelector::exeFileBroken() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear("System_FileSelect012", SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        MR::startSystemSE("SE_SY_FILE_SEL_NG");
    }

    if (MR::isDead(mSysInfoWindow)) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
    }
}

void FileSelector::exeManualStart() {
    if (MR::isFirstStep(this)) {
        disappearAllLayout();
    }

    if (isHiddenAllLayout()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvManual));
    }
}

void FileSelector::exeManual() {
    if (MR::isFirstStep(this)) {
        mManual->appear();
    }

    if (mManual->isClosed()) {
        setNerve(GET_NERVE(FileSelector, FileSelectorNrvFileConfirm));
    }
}

FileSelector::~FileSelector() {
}
