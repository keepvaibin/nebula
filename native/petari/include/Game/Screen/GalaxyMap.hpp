#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/Array.hpp"

class GalaxyMapCometIcon;
class GalaxyMapDomeIcon;
class GalaxyMapGalaxyDetail;
class GalaxyMapGalaxyPlain;
class GalaxyMapIcon;
class GalaxyMapMarioIcon;
class GalaxyMapTicoIcon;
class GalaxyMapTitle;
class IconAButton;

namespace nw4r {
    namespace lyt {
        class DrawInfo;
    };  // namespace lyt
};  // namespace nw4r

class GalaxyMap : public LayoutActor {
public:
    /// @brief Creates a new `GalaxyMap`.
    GalaxyMap();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();

    void movementForCapture();
    void calcAnimForCapture(const nw4r::lyt::DrawInfo&);
    void drawForCapture(const nw4r::lyt::DrawInfo&);
    void setModeNormal();
    void setModeNewGalaxyDiscover();
    void setModeNewTicoGalaxyDiscover();
    void setModeNewDomeDiscover();
    void setModeTicoCometPray();
    void setModeCapture();
    void drawGalaxyInfo() const;
    bool isTransition() const;
    bool isPointingAnything() const;
    bool isShowDetail() const;
    void dispIconAButton();
    void changeToGalaxyMap();
    void changeToAstroMap();
    void forceToGalaxyMap();
    void forceToAstroMap();
    void initPaneCtrlPointing();
    void initDomeIcon();
    void initMarioIcon();
    void initTicoIcon();
    bool tryFocusIn();
    void updateGalaxyName();
    void drawGalaxyIconWithOrder(int) const;
    GalaxyMapIcon* getPointingGalaxyIcon() const;

    void exeIdle();
    void exeFocusIn();
    void exeFocusOut();
    void exeFadeinGalaxyMap();
    void exeFadeinAstroMap();
    void exeShowDetail();

    MR::Vector< MR::AssignableArray< GalaxyMapIcon* > > mIcon;
    MR::Vector< MR::AssignableArray< GalaxyMapCometIcon* > > mCometIcon;
    MR::Vector< MR::AssignableArray< GalaxyMapDomeIcon* > > mDomeIcon;
    MR::Vector< MR::AssignableArray< GalaxyMapTicoIcon* > > mTicoIcon;
    GalaxyMapMarioIcon* mMarioIcon1;
    GalaxyMapMarioIcon* mMarioIcon2;
    GalaxyMapGalaxyPlain* mGalaxyPlain;
    GalaxyMapGalaxyDetail* mGalaxyDetail;
    GalaxyMapIcon* mPointingIcon;
    GalaxyMapTitle* mTitle;
    const char* mGalaxyName;
    bool _70;
    bool _71;
};
