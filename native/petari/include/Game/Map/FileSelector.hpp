#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/FileSelectIconID.hpp"

namespace Mii {
    struct Id;
}  // namespace Mii

namespace Ui {
    class ResFont;
}  // namespace Ui

class BackButton;
class BrosButton;
class FileSelectButton;
class FileSelectCameraController;
class FileSelectEffect;
class FileSelectInfo;
class FileSelectItem;
class FileSelectItemDelegatorBase;
class FileSelectSky;
class InformationMessage;
class Manual2P;
class MiiConfirmIcon;
class MiiSelect;
class NebulaFileSelectButton;
class TitleSequenceProduct;
class UserFile;
class SysInfoWindow;

template < typename T >
class DeriveActorGroup;

class FileSelector : public LiveActor {
public:
    FileSelector(const char* pName);

    virtual ~FileSelector() override;
    virtual void init(const TableIter& rIter) override;
    virtual bool initStep(const TableIter& rIter, uint64_t deadline) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void callbackStart();
    void callbackCopy();
    void callbackMii();
    void callbackDelete();
    void callbackManual();
    void notifyItem(FileSelectItem* pItem, s32 msg);
    void initUserFileArray();
    void createCameraController();
    void createSky();
    bool createFileItemStep();
    unsigned mInitStep = 0;
    unsigned mInitItem = 0;
    FileSelectItemDelegatorBase* mInitDelegator = nullptr;
    void createOperationButton();
    void createBackButton();
    void createBrosButton();
    void createInfoMessage();
    void createSysInfoWindow();
    void createFileInfo();
    void createTitle();
    void createMiiSelect();
    void createMiiConfirmIcon();
    void createManual();
    void createSelectEffect();
    void appearAllItems();
    void disappearAllLayout();
    bool isHiddenAllLayout() const;
    void updateFileInfo();
    void appearAllIndex();
    void disappearAllIndex();
    void invalidateSelectAll();
    void validateSelectAll();
    void initUserFile();
    void restoreUserFile();
    void checkAllComplete();
    void onPoint(FileSelectItem* pItem);
    void onSelect(FileSelectItem* pItem);
    void clearPointing();
    void setFileInfo(s32 id);
    bool checkSelectedBackButton();
    void goToNearPoint();
    void calcBasePos(f32 offset);
    void initAllItems();
    void validateRotateAllItems();
    FileSelectIconID::EFellowID getUserFileFellowID(s32 id) const;
    bool isUserFileMiiIdValid(s32 id) const;
    u32 getUserFileMiiIndex(s32 id) const;
    bool isUserFileCorrupted(s32 id) const;
    bool isUserFileAppearLuigi(s32 id) const;
    bool isUserFileLuigi(s32 id) const;
    void setUserFileMario(s32 id, bool isMario);
    void storeSetMiiIdUserFile(s32 id, const FileSelectIconID& rIconId);
    void getMiiId(Mii::Id* pCreateId, const FileSelectIconID& rIconId) const;
    void getIconId(FileSelectIconID* pIconId, s32 id) const;
    s32 getMissCount(s32 id) const;
    void playSelectedME();
    void updateBgm();
    void exeWaitBind();
    void exeTitle();
    void exeTitleEnd();
    void exeMiiDataError();
    void exeMiiDataWait();
    void exeMiiDataWaitEnd();
    void exeFileSelectStart();
    void exeFileSelect();
    void exeFileConfirmStart();
    void exeFileConfirmMiiDeleteWarningStart();
    void exeFileConfirmMiiDeleteWarning();
    void exeFileConfirmMiiDeleteSave();
    void exeFileConfirm();
    void exeDemoStartWait();
    void exeDemo();
    void exeCreateConfirmStart();
    void exeCreateConfirm();
    void exeCreate();
    void exeCopyWait();
    void exeCopySelect();
    void exeCopyConfirmStart();
    void exeCopyConfirm();
    void exeCopySave();
    void exeCopySaveMii();
    void exeCopyDemo();
    void exeCopyRejectStart();
    void exeCopyReject();
    void exeMiiWait();
    void exeMiiTip();
    void exeMiiSelectStart();
    void exeMiiSelect();
    void exeMiiCancel();
    void exeMiiConfirmWait();
    void exeMiiConfirm();
    void exeMiiCreateWait();
    void exeMiiCreateDemo();
    void exeMiiCaution();
    void exeMiiInfoStart();
    void exeMiiInfo();
    void exeDeleteConfirmStart();
    void exeDeleteConfirm();
    void exeDelete();
    void exeDeleteDemo();
    void exeFileBroken();
    void exeManualStart();
    void exeManual();
    void createCornerButtons();
    void updateCornerButtons();
    bool updateQuitQuestion();
    bool updateSettings();
    void offerFilesToMenuFocus();

    FileSelectCameraController* mCameraController;
    FileSelectSky* mSky;
    DeriveActorGroup< FileSelectItem >* mItems;
    Vec3f* mItemPositions;
    FileSelectButton* mOperationButton;
    BackButton* mBackButton;
    BrosButton* mBrosButton;
    InformationMessage* mInfoMessage;
    SysInfoWindow* mSysInfoWindow;
    SysInfoWindow* mSysInfoWindowMini;
    FileSelectItem* mSelectedItem;
    FileSelectItem* mDecidedItem;
    FileSelectItem* mPointedItem;
    FileSelectItem* mPointCandidate;
    FileSelectInfo* mFileInfo;
    UserFile* mUserFile;
    bool* mIsCompleteFile;
    TitleSequenceProduct* mTitle;
    MiiSelect* mMiiSelect;
    MiiConfirmIcon* mMiiConfirmIcon;
    Mii::Id* mMiiId;
    Manual2P* mManual;
    bool mIsMiiSelectStartFirst;
    s32 mBgmState;
    FileSelectEffect* mSelectEffect;
    NebulaFileSelectButton* mQuitButton;      // top left
    NebulaFileSelectButton* mSettingsButton;  // top right
    SysInfoWindow* mQuitWindow;  // its own: the file select's window keeps its messages' text positions
    bool mIsAskingQuit;
    s32 mQuitFrames;  // from Yes: the fade to black, then the game's own way out
    s32 mSettingsFrames;  // from Settings chosen, the file select's frames (none pass while the menu is open)
    NebulaFileSelectButton* mFocusWanted;  // Up from the top files: the highlight goes to the nearer button once it shows
};
