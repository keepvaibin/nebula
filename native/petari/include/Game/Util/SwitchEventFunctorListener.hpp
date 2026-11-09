#pragma once

#include "Game/Map/ActorAppearSwitchListener.hpp"

namespace MR {
    class FunctorBase;
};  // namespace MR

class SwitchEventFunctorListener : public SwitchEventListener {
public:
    SwitchEventFunctorListener();

    virtual void listenSwitchOnEvent() override;
    virtual void listenSwitchOffEvent() override;

    void setOnFunctor(const MR::FunctorBase&);
    void setOffFunctor(const MR::FunctorBase&);

    MR::FunctorBase* mOnFunctor;
    MR::FunctorBase* mOffFunctor;
};
