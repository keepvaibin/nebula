#pragma once

#include <JSystem/J3DGraphAnimator/J3DModel.hpp>

class BpkPlayer;
class BrkPlayer;
class BtkPlayer;
class BtpPlayer;
class BvaPlayer;
class MaterialUpdater;
class J3DFrameCtrl;
class J3DModel;
class J3DModelData;
class ResourceHolder;
class XanimePlayer;
class XanimeResourceTable;
class XjointTransform;

class ModelManager {
public:
    /// @brief Creates a new `ModelManager`.
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
    J3DFrameCtrl* getBckCtrl() const;
    J3DFrameCtrl* getBtkCtrl() const;
    J3DFrameCtrl* getBrkCtrl() const;
    J3DFrameCtrl* getBtpCtrl() const;
    J3DFrameCtrl* getBpkCtrl() const;
    J3DFrameCtrl* getBvaCtrl() const;
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
    J3DModel* getJ3DModel() const;
    J3DModelData* getJ3DModelData() const;
    const char* getPlayingBckName() const;
    void initModelAndAnimation(ResourceHolder*, const char*, ResourceHolder*, J3DMdlFlag);
    void initMaterialAnm();
    void initVisibilityAnm();
    void calc();
    void changeBckSetting(const char*, const char*, XanimePlayer*);
    void init(const char*, const char*, bool);

    BtkPlayer* mBtkPlayer;
    BrkPlayer* mBrkPlayer;
    BtpPlayer* mBtpPlayer;
    BpkPlayer* mBpkPlayer;
    BvaPlayer* mBvaPlayer;
    XanimeResourceTable* mXanimeResourceTable;
    XanimePlayer* mXanimePlayer;
    J3DModel* mModel;
    ResourceHolder* mModelResourceHolder;
    MaterialUpdater* mMaterialUpdater;
};
