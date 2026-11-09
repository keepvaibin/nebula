#pragma once

#include "Game/Render/Model.hpp"

class BpkPlayer;
class BrkPlayer;
class BtkPlayer;
class BtpPlayer;
class BvaPlayer;
class MaterialUpdater;
class FrameCtrl;
class Model;
class ModelData;
class NebulaArena;
class ResourceHolder;
class XanimePlayer;
class XanimeResourceTable;
class XjointTransform;

class ModelManager {
public:
    ModelManager();

    void update();
    void calcAnim();
    void calcView();
    void entry();
    void prepareMaterials();
    void updateList();
    void startBck(const char*, const char*);
    void startBckWithInterpole(const char*, s32);
    void startBtk(const char*);
    void startBrk(const char*);
    void startBtp(const char*);
    void startBpk(const char*);
    void startBva(const char*);
    void stopBtk();
    void stopBrk();
    void stopBtp();
    void stopBpk();
    void stopBva();
    FrameCtrl* getBckCtrl() const;
    FrameCtrl* getBtkCtrl() const;
    FrameCtrl* getBrkCtrl() const;
    FrameCtrl* getBtpCtrl() const;
    FrameCtrl* getBpkCtrl() const;
    FrameCtrl* getBvaCtrl() const;
    bool isBckStopped() const;
    bool isBtkStopped() const;
    bool isBrkStopped() const;
    bool isBtpStopped() const;
    bool isBpkStopped() const;
    bool isBvaStopped() const;
    bool isBtkPlaying(const char*) const;
    bool isBrkPlaying(const char*) const;
    bool isBpkPlaying(const char*) const;
    bool isBtpPlaying(const char*) const;
    bool isBvaPlaying(const char*) const;
    void initJointTransform();
    XjointTransform* getJointTransform(const char*);
    ResourceHolder* getResourceHolder() const;
    ResourceHolder* getModelResourceHolder() const;
    Model* getModel() const;
    ModelData* getModelData() const;
    const char* getPlayingBckName() const;
    void initModelAndAnimation(ResourceHolder*, const char*, ResourceHolder*, ModelFlag);
    void initMaterialAnm();
    void initVisibilityAnm();
    void calc();
    void changeBckSetting(const char*, const char*, XanimePlayer*);
    void init(const char*, const char*, bool);

    // A kind of animation the model has none of, reached by its class's code (a mod's model made like a game class can
    // lack one the class plays): an empty player, made then. Starting one of its names does nothing, it counts as
    // stopped, and its frame is its own. The game's models have what their code plays, and make none.
    BtkPlayer* btkPlayer() const;
    BrkPlayer* brkPlayer() const;
    BtpPlayer* btpPlayer() const;
    BpkPlayer* bpkPlayer() const;
    BvaPlayer* bvaPlayer() const;
    NebulaArena* arena() const;

    mutable BtkPlayer* mBtkPlayer;
    mutable BrkPlayer* mBrkPlayer;
    mutable BtpPlayer* mBtpPlayer;
    mutable BpkPlayer* mBpkPlayer;
    mutable BvaPlayer* mBvaPlayer;
    XanimeResourceTable* mXanimeResourceTable;
    XanimePlayer* mXanimePlayer;
    Model* mModel;
    ResourceHolder* mModelResourceHolder;
    MaterialUpdater* mMaterialUpdater;
    // The frame of a model with no skeletal animations, made when its class's code first asks for it.
    mutable FrameCtrl* mNoBckCtrl;
};
