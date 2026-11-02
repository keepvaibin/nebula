#pragma once

#include "Game/NPC/Tico.hpp"

class ActorCameraInfo;

class RunawayTico : public Tico {
public:
    /// @brief Creates a new `RunawayTico`.
    /// @param pName A pointer to the null-terminated name of the object.
    RunawayTico(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    void appearBushComment(const TVec3f&);
    void appearHoleComment(const TVec3f&);
    void appearPipeComment(const TVec3f&);
    void appearMamaComment(const TVec3f&);
    void setPosAfterCaught(const TVec3f&);
    void setPosAllCaught();
    bool isStartRunaway() const;
    void startRunaway();
    void setDemoTrans();

    void exeGuide0();
    void exeGuide1();
    void exeWhiteIn();
    void exeWhiteOut();
    void exeWait();
    void exeAppear();
    void exeTalk();

    ActorCameraInfo* mCameraInfo;
    s32 mObjArg1;
    s32 mDemoCastID;
    bool mIsStartRunaway;
    bool mIsAllCaught;
};
