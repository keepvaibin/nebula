#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CameraTargetMtx;
class ModelObj;
class PrologueLetter;
class ProloguePictureBook;

class PrologueDirector : public LiveActor {
public:
    /// @brief Creates a new `PrologueDirector`.
    /// @param pName A pointer to the null-terminated name of the object.
    PrologueDirector(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void control();

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
    TMtx34f _A0;
    bool _D0;
};

class PrologueHolder : public NameObj {
public:
    /// @brief Creates a new `PrologueHolder`.
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
