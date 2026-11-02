#pragma once

#include "Game/NPC/NPCActor.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/NPCUtil.hpp"

class TicoDemoGetPower;

/// @brief Luma
class Tico : public NPCActor {
public:
    /// @brief Creates a new `Tico`.
    /// @param pName A pointer to the null-terminated name of the object.
    Tico(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    void initBase(s32);
    void initBase(const JMapInfoIter&, s32);
    void initMessage(const JMapInfoIter&, const char*);
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

    u32 _15C;
    TVec3f _160;
    f32 _16C;
    TicoDemoGetPower* mDemoGetPower;
    TalkMessageCtrl* _174;
    MtxPtr _178;
    Color8 _17C;
    const Nerve* _180;
    DemoStarter mDemoStarter;
};
