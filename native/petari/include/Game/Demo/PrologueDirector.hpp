#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CameraTargetMtx;
class ModelObj;
class PrologueLetter;
class ProloguePictureBook;

class PrologueDirector : public LiveActor {
public:
    PrologueDirector(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void exeWait();
    void exePictureBook();
    void exePeachLetterStart();
    void exePeachLetter();
    void exePeachLetterWait();
    void exePeachLetterEnd();
    void exeBindWait();
    void exeArrive();
    void exeGameStart();
    void createPictureBook();
    void createLetter();
    void createScenery();
    void createMarioPosDummyModel();
    void createCameraTarget();
    void pauseOff();

private:
    ProloguePictureBook* mPictureBook;
    PrologueLetter* mLetter;
    ModelObj* mScenery;
    ModelObj* mMarioPosDummyModel;
    CameraTargetMtx* mCameraTarget;
    Mtx34f mPlayerBaseMtx;
    bool mUseSwitchA;
};

class PrologueHolder : public NameObj {
public:
    /// @param pName The pointer to the null-terminated name of the object.
    PrologueHolder(const char* pName);

    void registerPrologueObj(PrologueDirector* pDirector);
    void start();

private:
    PrologueDirector* mDirector;
};

namespace MR {
    void startPrologue();
};  // namespace MR
