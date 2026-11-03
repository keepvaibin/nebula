#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/NerveExecutor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

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
    FileSelectItem(s32, bool, const FileSelectIconID&, const char*);

    virtual ~FileSelectItem();
    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();

    bool isNew() const;
    bool isExist() const;
    void format();
    void change(const FileSelectIconID&, bool);
    void forceChange(const FileSelectIconID&, bool);
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
    void emitCopy();
    void emitCompleteEffect();
    void deleteCompleteEffect();

    void exeExistWait();
    void exeNewWait();

    bool _8C;
    PartsModel* mPlanetMapObj;
    FileSelectIconID* mIconID;
    FileSelectModel** mModels;
    MiiFaceParts* mFaceParts;
    FileSelectNumber* _A0;
    TPos3f _A4;
    TMtx34f _D4;
    TMtx34f _104;
    TVec3f _134;
    s32 _140;
    u8 _144;
    bool mIsInvalidateSelect;
    u8 _146;
    u8 _147;
    FileSelectItemSub::ScaleController* mScaleCtrl;
    FileSelectItemSub::BlinkController* mBlinkCtrl;
    FileSelectItemDelegatorBase* mDelegator;
    u8 _154;
    u8 _155;
    u8 _156;
    u8 _157;
    TVec2f _158;
    f32 _160;
    bool mIsInvalidRotate;
    u8 _165;
    u8 _166;
    u8 _167;
    s32 _168;
    s32 _16C;
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
            return _8;
        }

        f32 _8;
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
        s32 _C;
        s32 _10;
    };
};  // namespace FileSelectItemSub
