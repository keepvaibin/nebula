#pragma once

#include <nebula_geometry.hpp>

class LayoutActor;
class RumbleCalculatorCosMultLinear;

class CountUpPaneRumbler {
public:
    /// @brief Creates a new `CountUpPaneRumbler`.
    /// @param pHost The pointer to the owning actor instance.
    /// @param pPaneName The pointer to the null-terminated name of the pane.
    CountUpPaneRumbler(LayoutActor* pHost, const char* pPaneName);

    void update();
    void start();
    void reset();
    bool isRumbling() const;
    void connectPane(LayoutActor*, const char*);

private:
    RumbleCalculatorCosMultLinear* mRumbleCalculator;
    Vec2f mPosition;
};
