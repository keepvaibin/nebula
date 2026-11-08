#pragma once


struct GameEventFlag {
    enum Type {
        Type_None = 0x00,
        Type_StarNum = 0x01,
        Type_GalaxyOpenStar = 0x02,
        Type_SpecialStar = 0x03,
        Type_EventFlag = 0x04,
        Type_StoryEvent = 0x05,
        Type_Galaxy = 0x06,
        Type_Comet = 0x07,
        Type_StarPiece = 0x08,
        Type_EventValueIsZero = 0x09,
        Type_CompleteMarioAndLuigi = 0x0A,
        Type_StoryEventSync = 0x0B,
    };

    const char* mName;
    u8 mType;
    u8 mSaveFlag;

    union {
        u16 mCondition1;
        u16 mStarID;
        u16 mStarNum;
        u16 mStarPieceIndex;
    };

    union {
        u16 mCondition2;
        u16 mNeedStarPieceNum;
    };

    u32 mExactStarPieceNum;  // when set, the exact star bit cost; otherwise mNeedStarPieceNum * 10

    union {
        const char* mCondition3;
        const char* mGalaxyName;
        const char* mEventFlagName;
        const char* mEventValueName;
        const char* mStoryEventName;
        const char* mRequirement1;
    };

    union {
        const char* mCondition4;
        const char* mRequirement;
        const char* mRequirement2;
    };
};

class GameEventFlagAccessor {
public:
    GameEventFlagAccessor(const GameEventFlag*);

    const char* getName() const;
    bool isTypeGalaxyOpenStar() const;
    bool isTypeSpecialStar() const;
    bool isTypeStarPiece() const;
    bool isTypeEventValueIsZero() const;
    const char* getGalaxyName() const;
    s32 getStarId() const;
    s32 getStarPieceIndex() const;
    const char* getGalaxyNameWithStarPiece() const;
    s32 getNeedStarPieceNum() const;
    const char* getEventValueName() const;
    const char* getRequirement() const;

    const GameEventFlag* mFlag;
};
