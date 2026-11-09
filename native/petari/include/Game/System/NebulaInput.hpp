#pragma once

// The game's names for its players' input (nebula_game_api.h input_read/input_control).

enum {
    PLAYER_1 = 0,
    PLAYER_2 = 1,
    PLAYER_3 = 2,
};

// The controllers the game reads, 0 to count() - 1: the host's players, never fewer than the game's own two (PlayerInputs'
// pads); the rest are let go in play. Fixed for the session.
namespace NebulaInputPlayers {
    u32 count();
}  // namespace NebulaInputPlayers

// A player's two hands: motion in each; the sub hand has the move stick.
enum {
    INPUT_HAND_MAIN = 0,
    INPUT_HAND_SUB = 1,
    INPUT_HAND_UNKNOWN = 255,
};

// A connection change (PlayerInput::connectCallback): connected, or gone.
enum {
    INPUT_CONNECTED = 0,
    INPUT_DISCONNECTED = -1,
};

// The battery's level (PlayerInput::getBattery): 0 critical to 4 full.
enum {
    INPUT_BATTERY_CRITICAL = 0,
    INPUT_BATTERY_LOW = 1,
    INPUT_BATTERY_MAX = 4,
};

// One step of a rumble pattern (InputRumbleData).
enum {
    RUMBLE_OFF = 0,
    RUMBLE_ON = 1,
};

// In play, or in the HOME Menu (PlayerInputs' mode).
enum {
    INPUT_MODE_HOME = 0,
    INPUT_MODE_GAME = 1,
};
