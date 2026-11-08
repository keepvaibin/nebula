#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Color.hpp"

class ClipFieldFillDraw : public NameObj {
public:
    ClipFieldFillDraw(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void draw() const;

    void setModeSubColor();
    void setUpFillScreen() const;
    void sendFillScreen(NebulaColor8) const;

    // The fill's blend (nebula_game_api.h NEBULA_BLEND_* and NEBULA_FACTOR_*).
    u8 mFillBlendOp;
    u8 mFillSrcFactor;
    u8 mFillDstFactor;
    Color8 mFillColor;
    bool mFillInsideVolume;
};

namespace MR {
    NameObj* createClipFieldFillDraw(const char*);
    NameObj* createVolumeDrawSpotLight(const char*);
};  // namespace MR
