#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class FileSelectIconID;
class FileSelectItem;
class FileSelectItemDelegatorBase;
class FileSelectModel;
class FileSelectNumber;
class MiiFaceParts;
class PartsModel;

namespace FileSelectItemSub {
    class ScaleController;
    class BlinkController;
};  // namespace FileSelectItemSub

class FileSelectItem : public LiveActor {
public:
    FileSelectItem(s32 fileNo, bool isNewFile, const FileSelectIconID&, const char* pName);

    virtual ~FileSelectItem() override;
    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;

    bool isNew() const;
    bool isExist() const;
    void format();
    void change(const FileSelectIconID&, bool isComplete);
    void forceChange(const FileSelectIconID&, bool isComplete);
    void invalidateSelect();
    void validateSelect();
    void appearIndex();
    void disappearIndex();
    void copyIconID(FileSelectIconID*);
    void setSelectDelegator(FileSelectItemDelegatorBase*);
    void onPointing();
    void offPointing();
    void validateRotate();
    void turnToFront(s32);
    void exeFormat();
    void exeChangeFellow();
    void exeChangeMii();
    void createNew();
    void createFellows();
    void createMii();
    void createNumber();
    void updatePointing();
    void updateRotate();
    void playPointedME();
    void playPointedNotUsingME();
    void appearFellowModel();
    void killAllModels();
    void emitOpen();
    void emitVanish();
    void emitCompleteEffect();
    void deleteCompleteEffect();

    void exeExistWait();
    void exeNewWait();

    bool mIsNewFile;
    PartsModel* mPlanetMapObj;
    FileSelectIconID* mIconID;
    FileSelectModel** mModels;
    MiiFaceParts* mFaceParts;
    FileSelectNumber* mNumber;
    Pos3f mPlanetMtx;
    Mtx34f mFellowMtx;
    Mtx34f mFaceMtx;
    s32 mFileNo;
    u8 mIsPointed;
    bool mIsInvalidateSelect;
    u8 mIsMiiBroken;
    u8 mIsComplete;
    FileSelectItemSub::ScaleController* mScaleCtrl;
    FileSelectItemSub::BlinkController* mBlinkCtrl;
    FileSelectItemDelegatorBase* mDelegator;
    u8 mIsPointerOut;
    u8 mIsDragging;
    Vec2f mPrevPointerPos;
    f32 mRotateSpeed;
    bool mIsInvalidRotate;
    s32 mTurnTime;
    s32 mTurnStep;
};

namespace FileSelectItemSub {
    class ScaleController : public NerveExecutor {
    public:
        ScaleController();

        void exeToSmall();
        void exeToBig();
        void exeSmall();
        void exeBig();

        f32 getScale() const {
            return mScale;
        }

        f32 mScale;
    };

    class BlinkController : public NerveExecutor {
    public:
        BlinkController(FileSelectItem*);

        void exeOpen();
        void exeShut();
        void exeSleep();
        void exeBlink();
        void shut();
        void open();
        void sleep();

        FileSelectItem* mItem;
        s32 mWaitStep;
        s32 mBlinkStep;
    };
};  // namespace FileSelectItemSub
