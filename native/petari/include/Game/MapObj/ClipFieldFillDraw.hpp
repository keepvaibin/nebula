#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Color.hpp"

class ClipFieldFillDraw : public NameObj {
public:
    /// @brief Creates a new `ClipFieldFillDraw`.
    /// @param pName A pointer to the null-terminated name of the object.
    ClipFieldFillDraw(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;

    void setModeSubColor();
    void setUpFillScreen() const;
    void sendFillScreen(NebulaColor8) const;

    // The fill's blend (nebula_game_api.h NEBULA_BLEND_* and NEBULA_FACTOR_*).
    u8 mFillBlendOp;
    u8 mFillSrcFactor;
    u8 mFillDstFactor;
    Color8 _1C;
    Color8 _20;
    bool _24;
    bool _25;
};

namespace MR {
    NameObj* createClipFieldFillDraw(const char*);
    NameObj* createVolumeDrawSpotLight(const char*);
};  // namespace MR
