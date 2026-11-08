#pragma once

#include "Game/Player/PlayerEvent.hpp"

class EventGameOver : public EventSequence {
public:
    EventGameOver();

    void init(u16 eventFrame, u16 sequenceFrame);
    void sound(u16 eventFrame, u16 sequenceFrame);
    void reqLayout(u16 eventFrame, u16 sequenceFrame);
    void finish(u16 eventFrame, u16 sequenceFrame);
};
