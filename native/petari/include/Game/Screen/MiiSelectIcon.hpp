#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace nw4r {
    namespace lyt {
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

class ButtonPaneController;
class FileSelectIconID;
class MiiFaceIcon;

class MiiSelectIcon : public LayoutActor {
public:
    MiiSelectIcon(int, int, int, const char*);

    virtual void appear();
    virtual void control();

    void appear(const FileSelectIconID&);
    void appearMiiDummy();
    void validate();
    void invalidate();
    void prohibit();
    bool isSelected() const;
    bool isMiiDummy() const;
    void copyIconID(FileSelectIconID*);
    nw4r::lyt::TexMap* getTexMap();
    bool isPointing() const;
    void exeCreate();
    void exeWait();
    void exeSelected();
    void exeDisappear();
    void exeInvalid();
    void createButton();
    void createFaceImageObj();

private:
    ButtonPaneController* _20;
    ButtonPaneController* _24;
    ButtonPaneController* _28;
    MiiFaceIcon* mIcon;
    nw4r::lyt::TexMap* mMiiTexMap;
    nw4r::lyt::TexMap* mFellowTexMap;
    FileSelectIconID* mIconID;
    bool mIsMiiDummy;
    bool _3D;
};
