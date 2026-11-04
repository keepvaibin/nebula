#pragma once

// Around a frame's drawing: the frame cleared at its start, shown and handed to the host at its end.
namespace GameFrame {
    void begin();
    void end();
    // Whether the frame's alpha holds anything this scene (the logo scene's does not).
    void setKeepAlpha(bool keep);
}  // namespace GameFrame
