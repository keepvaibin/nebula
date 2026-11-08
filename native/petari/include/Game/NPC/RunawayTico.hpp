#pragma once

#include "Game/NPC/Tico.hpp"

class ActorCameraInfo;

class RunawayTico : public Tico {
public:
    RunawayTico(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();


    void appearBushComment(const Vec3f&);
    void appearHoleComment(const Vec3f&);
    void appearPipeComment(const Vec3f&);
    void appearMamaComment(const Vec3f&);
    void setPosAfterCaught(const Vec3f&);
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
