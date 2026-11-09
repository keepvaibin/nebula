#pragma once

#include "Game/NPC/Tico.hpp"

class ActorCameraInfo;

class RunawayTico : public Tico {
public:
    RunawayTico(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;


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

    // Placement arg 1, overridden by a demo cast: the meeting demo's Tico, or the one placed when all are caught.
    enum Role { Role_Meet = 1, Role_AllCaught = 2 };

    ActorCameraInfo* mCameraInfo;
    s32 mRole;
    s32 mDemoCastID;
    bool mIsStartRunaway;
    bool mIsAllCaught;
};
