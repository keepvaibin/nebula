#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

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
    Ui::TexMap* getTexMap();
    bool isPointing() const;
    void exeCreate();
    void exeWait();
    void exeSelected();
    void exeDisappear();
    void exeInvalid();
    void createButton();
    void createFaceImageObj();

private:
    ButtonPaneController* mActiveButton;
    ButtonPaneController* mFellowButton;
    ButtonPaneController* mMiiButton;
    MiiFaceIcon* mIcon;
    Ui::TexMap* mMiiTexMap;
    Ui::TexMap* mFellowTexMap;
    FileSelectIconID* mIconID;
    bool mIsMiiDummy;
    bool mIsEnabled;
};
