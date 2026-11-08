#pragma once

#include "Game/Player/PlayerEvent.hpp"

class EventFireDown : public EventSequence {
public:
    EventFireDown();

    void init(u16 eventFrame, u16 sequenceFrame);
    void sound(u16 eventFrame, u16 sequenceFrame);
    void sound2(u16 eventFrame, u16 sequenceFrame);
};
