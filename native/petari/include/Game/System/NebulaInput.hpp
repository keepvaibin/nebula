#pragma once

// The game's names for its controllers on Nebula's input (nebula_game_api.h input_read/input_control): the players'
// channels, what is on a remote's extension port, the connection results its callbacks take, the battery's levels,
// the motor's two states in rumble patterns, and the sensor bar's place.

// Players' controllers: player 1's remote (with its Nunchuk), then the others (player 2 the co-star).
enum {
    GAMEPAD_CHAN0 = 0,
    GAMEPAD_CHAN1 = 1,
    GAMEPAD_CHAN2 = 2,
    GAMEPAD_MAX_CONTROLLERS = 4,
};

// The device on a remote's extension port (GamePad::extensionCallback): none, the Nunchuk, or one the game does not use.
enum {
    GAMEPAD_DEV_CORE = 0,
    GAMEPAD_DEV_FREESTYLE = 1,
    GAMEPAD_DEV_UNKNOWN = 255,
};

// A connection change (GamePad::connectCallback): connected, or gone.
enum {
    GAMEPAD_ERR_NONE = 0,
    GAMEPAD_ERR_NO_CONTROLLER = -1,
};

// The battery's level (GamePad::getBattery): 0 critical to 4 full.
enum {
    GAMEPAD_BATTERY_LEVEL_CRITICAL = 0,
    GAMEPAD_BATTERY_LEVEL_LOW = 1,
    GAMEPAD_BATTERY_LEVEL_MAX = 4,
};

// One step of a rumble pattern (GamePadRumbleData).
enum {
    GAMEPAD_MOTOR_STOP = 0,
    GAMEPAD_MOTOR_RUMBLE = 1,
};

// Where the sensor bar stands (GamePadHolder's pointer mode).
enum {
    GAMEPAD_SENSOR_BAR_POS_BOTTOM = 0,
    GAMEPAD_SENSOR_BAR_POS_TOP = 1,
};
