#pragma once

namespace MR {
    // What keeps running while the scene is stopped. The demo kinds keep the demo set running (casts, sensors, camera context,
    // clipping, lights, effects and layouts on pause).
    enum MovementControlType {
        MovementControlType_None = -1,
        MovementControlType_Resume,                // ends a stop: everything runs again
        MovementControlType_Demo,                  // the demo set and switch watchers
        MovementControlType_DemoMarioPuppetable,   // also the player, which the demo drives, and the screen effects
        MovementControlType_StopEnemy,             // only enemies stop; the demo drives the player
        MovementControlType_StopPlayer,            // only the player stops
        MovementControlType_DemoWithCameraDirector,  // the demo set and the camera director; replaces a running stop
    };

    // The demo drives the player (MarioAccess remote demo) for these.
    inline bool isMarioRemoteDemo(MovementControlType type) {
        return type == MovementControlType_DemoMarioPuppetable || type == MovementControlType_StopEnemy;
    }
};  // namespace MR
