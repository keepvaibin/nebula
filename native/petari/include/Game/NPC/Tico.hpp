#pragma once

#include "Game/NPC/NPCActor.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/NPCUtil.hpp"

class TicoDemoGetPower;

/// @brief Luma
class Tico : public NPCActor {
public:
    Tico(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;


    void initBase(s32);
    void initBase(const TableIter&, s32);
    void initMessage(const TableIter&, const char*);
    void initMessage(const char*);
    bool tryReaction();
    void setNerveMeta();
    void setNerveWait();
    bool killFunc(u32);

    void exeReaction();
    void exeDelight();
    void exeAppear();
    void exeNoReaction();
    void exeWait();
    void exeTalk();
    void exeMeta();
    void exeBlue0();
    void exeBlue1();
    void exeRed0();
    void exeRed1();
    void exeRed2();
    void exeSpin0();
    void exeGuide0();
    void exeGuide1();
    void exeGuide2();
    void exeGuide3();
    void exeLead0();

    static const f32 sFloatSeMinVolume;

    u32 mBehavior;
    Vec3f mPrevPos;
    f32 mFloatOffset;
    TicoDemoGetPower* mDemoGetPower;
    TalkMessageCtrl* mTicoTalkCtrl;
    MtxPtr mLightJointMtx;
    Color8 mLightColor;
    const Nerve* mMetaNerve;
    DemoStarter mDemoStarter;
};
