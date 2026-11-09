#pragma once

#include "Game/Screen/WipeHolderBase.hpp"
#include <nebula_geometry.hpp>

class SystemWipeHolder : public WipeHolderBase {
public:
    /// @brief Creates a new `SystemWipeHolder`.
    SystemWipeHolder();

    /// @brief Intializes the `SystemWipeHolder` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter) override;

    virtual void movement() override;

    /// @brief Draws the `SystemWipeHolder` to the screen.
    virtual void draw() const override;

    virtual void calcAnim() override;
    virtual void updateWipe(const char*) override;

    void setWipeRingCenter(const Vec3f&);
    void startGameScreenCapture();
    void drawGameScreenCapture() const;

    bool isCurrentAlive() const;

private:
    bool mIsCapturing;  // the game screen capture is held
};

namespace MR {
    SystemWipeHolder* createSystemWipeHolder();
};  // namespace MR
