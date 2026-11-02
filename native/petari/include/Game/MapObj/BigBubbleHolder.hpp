#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BigBubble;
class BigBubbleDrawer;
class BigBubbleMoveLimitter;

class BigBubbleHolder : public NameObj {
public:
    BigBubbleHolder(const char*);
    virtual ~BigBubbleHolder();
    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;

    void addMoveLimitter(BigBubbleMoveLimitter*);
    BigBubble* generateBigBubble(LiveActor*, const TVec3f&, const TVec3f&, f32, bool, s32, s32, s32);
    BigBubbleMoveLimitter* getMoveLimitter(s32) const;
    void sortDrawOrder();

    BigBubbleDrawer* mDrawer;
    BigBubble* mBubbles[16];
    BigBubble* mDrawBubbles[16];
    BigBubbleMoveLimitter* mLimitters[64];
    s32 mNumLimitters;
    s32 mNumBubbles;
    bool mIsMarioBinded;
};

namespace MR {
    void setUpBigBubbleDraw();
    void createBigBubbleHolder();
    BigBubble* generateBigBubble(LiveActor*, const TVec3f&, const TVec3f&, f32, bool, s32, s32, s32);
    void limitBigBubblePosition(TVec3f*, f32, s32);
    void addBigBubbleMoveLimitter(BigBubbleMoveLimitter*);
    bool isPlayerBindedBigBubble();
};  // namespace MR
