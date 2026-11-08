#pragma once

class LiveActor;

// What a defeated enemy drops.
class ItemGenerator {
public:
    enum Type {
        Type_None = 0,
        Type_Coin = 1,
        Type_StarPiece = 2,
    };

    ItemGenerator();

    void setTypeNone();
    void setTypeCoin(s32 num);
    void setTypeStarPeace(s32 num);
    void generate(const LiveActor*);
    bool isUseFarSE() const;

    s32 mNum;
    u8 mType;
};
