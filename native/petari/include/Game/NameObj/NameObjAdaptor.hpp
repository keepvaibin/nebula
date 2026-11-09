#pragma once

#include "Game/NameObj/NameObj.hpp"

namespace MR {
    class FunctorBase;
};  // namespace MR

/// @brief Implementation of a NameObj that stores function pointers to movement, calcAnim, calcView, and draw functions.
class NameObjAdaptor : public NameObj {
public:
    NameObjAdaptor(const char*);

    virtual ~NameObjAdaptor() override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;

    void connectToMovement(const MR::FunctorBase&);
    void connectToCalcAnim(const MR::FunctorBase&);
    void connectToDraw(const MR::FunctorBase&);

    MR::FunctorBase* mMovementFunc;
    MR::FunctorBase* mCalcAnimFunc;
    MR::FunctorBase* mCalcViewFunc;
    MR::FunctorBase* mDrawAnimFunc;
};
