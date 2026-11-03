#pragma once

#include "Game/System/NerveExecutor.hpp"

class ButtonPaneController;
class LayoutActor;

class YesNoController : public NerveExecutor {
public:
    /// @brief Creates a new `YesNoController`.
    /// @param pHost The pointer to the owning actor instance.
    YesNoController(LayoutActor* pHost);

    void appear();
    void kill();
    void update();
    bool isSelected() const;
    bool isSelectedYes() const;
    bool isDisappearStart() const;
    void setSE(const char*, const char*, const char*);
    bool trySelect();
    void decideNo();
    void emitEffectIfExist(const char*);
    void deleteEffectIfExist(const char*);
    void forceDeleteEffectAllIfExist();
    void exeSelecting();
    void exeDecided();
    void exeDisappear();
    void exeSelected();
    void exeNotSelected();

    LayoutActor* mHost;
    bool _C;
    ButtonPaneController* mButtonYesPaneCtrl;
    ButtonPaneController* mButtonNoPaneCtrl;
    const char* mCursorSE;
    const char* mYesSE;
    const char* mNoSE;
};
