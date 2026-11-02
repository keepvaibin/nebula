#pragma once


class LiveActor;

struct ActorAnimDataInfo {
    const char* mName;
    f32 mStartFrame;
    u8 mIsKeepAnim;
};

struct ActorAnimKeeperInfo {
    const char* mName;
    ActorAnimDataInfo mBckInfo;
    ActorAnimDataInfo mBtkInfo;
    ActorAnimDataInfo mBrkInfo;
    ActorAnimDataInfo mBpkInfo;
    ActorAnimDataInfo mBtpInfo;
    ActorAnimDataInfo mBvaInfo;
};

class ActorAnimKeeper {
public:
    ActorAnimKeeper(LiveActor*);

    bool start(const char*);
    void update();
    bool isPlaying(const char*) const;
    bool initAnimData();
    ActorAnimKeeperInfo* findAnimInfo(const char*) const;
    static ActorAnimKeeper* tryCreate(LiveActor*);

    LiveActor* mActor;
    s32 mNumInfo;
    ActorAnimKeeperInfo* mInfoArray;
    ActorAnimKeeperInfo* mCurrentInfo;
};
