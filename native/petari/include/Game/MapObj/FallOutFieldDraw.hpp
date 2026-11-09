#pragma once

#include "Game/NameObj/NameObj.hpp"

class GameTexture;

class FallOutFieldDraw : public NameObj {
public:
    FallOutFieldDraw(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;

    void activate();
    void deactivate();

    bool mFillInsideVolume;
    GameTexture* mMask;      // the screen mask at half size
    GameTexture* mDepthKey;  // the frame's depth, in the mask's pass
    GameTexture* mEdge;
    GameTexture* mEdgeBlurred;
    GameTexture* mBlurScratch;
};

namespace MR {
    NameObj* createFallOutFieldDraw(const char*);
};  // namespace MR
