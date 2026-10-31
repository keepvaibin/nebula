#pragma once

// The game's names for its controllers on Nebula's input (nebula_game_api.h input_read/input_control): the players'
// channels, what is on a remote's extension port, the connection results its callbacks take, the battery's levels,
// the motor's two states in rumble patterns, and the sensor bar's place.

// Players' controllers: player 1's remote (with its Nunchuk), then the others (player 2 the co-star).
enum {
    WPAD_CHAN0 = 0,
    WPAD_CHAN1 = 1,
    WPAD_CHAN2 = 2,
    WPAD_MAX_CONTROLLERS = 4,
};

// The device on a remote's extension port (WPad::extensionCallback): none, the Nunchuk, or one the game does not use.
enum {
    WPAD_DEV_CORE = 0,
    WPAD_DEV_FREESTYLE = 1,
    WPAD_DEV_UNKNOWN = 255,
};

// A connection change (WPad::connectCallback): connected, or gone.
enum {
    WPAD_ERR_NONE = 0,
    WPAD_ERR_NO_CONTROLLER = -1,
};

// The battery's level (WPad::getBattery): 0 critical to 4 full.
enum {
    WPAD_BATTERY_LEVEL_CRITICAL = 0,
    WPAD_BATTERY_LEVEL_LOW = 1,
    WPAD_BATTERY_LEVEL_MAX = 4,
};

// One step of a rumble pattern (WPadRumbleData).
enum {
    WPAD_MOTOR_STOP = 0,
    WPAD_MOTOR_RUMBLE = 1,
};

// Where the sensor bar stands (WPadHolder's pointer mode).
enum {
    WPAD_SENSOR_BAR_POS_BOTTOM = 0,
    WPAD_SENSOR_BAR_POS_TOP = 1,
};
