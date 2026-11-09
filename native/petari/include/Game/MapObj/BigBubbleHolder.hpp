#pragma once

#include "Game/Util/Array.hpp"

#include "Game/LiveActor/LiveActor.hpp"

class BigBubble;
class BigBubbleDrawer;
class BigBubbleMoveLimitter;

class BigBubbleHolder : public NameObj {
public:
    BigBubbleHolder(const char*);
    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;

    void addMoveLimitter(BigBubbleMoveLimitter*);
    BigBubble* generateBigBubble(LiveActor*, const Vec3f&, const Vec3f&, f32, bool, s32, s32, s32);
    BigBubbleMoveLimitter* getMoveLimitter(s32) const;
    void sortDrawOrder();

    BigBubble* createBubble();

    // Made at init; when all are out, generate makes one more.
    static const s32 cBubbleCount = 16;

    BigBubbleDrawer* mDrawer;
    MR::Vector< MR::AssignableArray< BigBubble* > > mBubbles;
    MR::Vector< MR::AssignableArray< BigBubble* > > mDrawBubbles;  // the drawn ones in order (sortDrawOrder), as much room as mBubbles
    MR::GrowTable< BigBubbleMoveLimitter*, 64 > mLimitters;
    s32 mNumLimitters;
    s32 mNumBubbles;
    bool mIsMarioBinded;
};

namespace MR {
    void setUpBigBubbleDraw();
    void createBigBubbleHolder();
    BigBubble* generateBigBubble(LiveActor*, const Vec3f&, const Vec3f&, f32, bool, s32, s32, s32);
    void limitBigBubblePosition(Vec3f*, f32, s32);
    void addBigBubbleMoveLimitter(BigBubbleMoveLimitter*);
    bool isPlayerBindedBigBubble();
};  // namespace MR
