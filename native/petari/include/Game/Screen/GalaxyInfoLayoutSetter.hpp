#pragma once


class IconComet;
class LayoutActor;

namespace Ui {
    class TexMap;
}  // namespace Ui

class GalaxyInfoLayoutSetter {
public:
    enum GalaxyState {
        GalaxyState_Hatena,
        GalaxyState_Unknown,
        GalaxyState_New,
        GalaxyState_Opened,
    };

    /// @brief Creates a new `GalaxyInfoLayoutSetter`.
    /// @param pHost A pointer to the owning actor instance.
    GalaxyInfoLayoutSetter(LayoutActor* pHost);

    void replaceTexture(const char*);
    void updateInfo(const char*, GalaxyState);
    void updateInfo(const char*, GalaxyState, int);
    void updateCometPos();
    static u32 getAnimLayerNum();
    void initReplaceTexture();
    void updateInfoWithoutComet(const char*, GalaxyState);
    void setTextBoxStarNum(const char*);
    void showCometWindow(int);
    void hideCometWindow();

    LayoutActor* mHost;
    Ui::TexMap** mReplaceTexture;
    Ui::TexMap* mHiddenGalaxyTexture;  // picture shown for a galaxy that is not open yet
    IconComet* mIconComet;
};
