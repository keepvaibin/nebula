// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Windows virtual-key codes, the runtime's key numbering on every platform (key bindings in the settings files use them):
// winuser.h's values where Windows does not provide them. SDL hosts translate scancodes to these. Letters and digits
// are their ASCII upper-case codes, as on Windows.

#if !defined(_WIN32)

inline constexpr int VK_BACK = 0x08;
inline constexpr int VK_TAB = 0x09;
inline constexpr int VK_RETURN = 0x0D;
inline constexpr int VK_SHIFT = 0x10;
inline constexpr int VK_CONTROL = 0x11;
inline constexpr int VK_MENU = 0x12;
inline constexpr int VK_PAUSE = 0x13;
inline constexpr int VK_CAPITAL = 0x14;
inline constexpr int VK_ESCAPE = 0x1B;
inline constexpr int VK_SPACE = 0x20;
inline constexpr int VK_PRIOR = 0x21;
inline constexpr int VK_NEXT = 0x22;
inline constexpr int VK_END = 0x23;
inline constexpr int VK_HOME = 0x24;
inline constexpr int VK_LEFT = 0x25;
inline constexpr int VK_UP = 0x26;
inline constexpr int VK_RIGHT = 0x27;
inline constexpr int VK_DOWN = 0x28;
inline constexpr int VK_SNAPSHOT = 0x2C;
inline constexpr int VK_INSERT = 0x2D;
inline constexpr int VK_DELETE = 0x2E;
inline constexpr int VK_LWIN = 0x5B;
inline constexpr int VK_RWIN = 0x5C;
inline constexpr int VK_APPS = 0x5D;
inline constexpr int VK_NUMPAD0 = 0x60;
inline constexpr int VK_NUMPAD1 = 0x61;
inline constexpr int VK_NUMPAD2 = 0x62;
inline constexpr int VK_NUMPAD3 = 0x63;
inline constexpr int VK_NUMPAD4 = 0x64;
inline constexpr int VK_NUMPAD5 = 0x65;
inline constexpr int VK_NUMPAD6 = 0x66;
inline constexpr int VK_NUMPAD7 = 0x67;
inline constexpr int VK_NUMPAD8 = 0x68;
inline constexpr int VK_NUMPAD9 = 0x69;
inline constexpr int VK_MULTIPLY = 0x6A;
inline constexpr int VK_ADD = 0x6B;
inline constexpr int VK_SEPARATOR = 0x6C;
inline constexpr int VK_SUBTRACT = 0x6D;
inline constexpr int VK_DECIMAL = 0x6E;
inline constexpr int VK_DIVIDE = 0x6F;
inline constexpr int VK_F1 = 0x70;
inline constexpr int VK_F2 = 0x71;
inline constexpr int VK_F3 = 0x72;
inline constexpr int VK_F4 = 0x73;
inline constexpr int VK_F5 = 0x74;
inline constexpr int VK_F6 = 0x75;
inline constexpr int VK_F7 = 0x76;
inline constexpr int VK_F8 = 0x77;
inline constexpr int VK_F9 = 0x78;
inline constexpr int VK_F10 = 0x79;
inline constexpr int VK_F11 = 0x7A;
inline constexpr int VK_F12 = 0x7B;
inline constexpr int VK_F13 = 0x7C;
inline constexpr int VK_F14 = 0x7D;
inline constexpr int VK_F15 = 0x7E;
inline constexpr int VK_F16 = 0x7F;
inline constexpr int VK_F17 = 0x80;
inline constexpr int VK_F18 = 0x81;
inline constexpr int VK_F19 = 0x82;
inline constexpr int VK_F20 = 0x83;
inline constexpr int VK_F21 = 0x84;
inline constexpr int VK_F22 = 0x85;
inline constexpr int VK_F23 = 0x86;
inline constexpr int VK_F24 = 0x87;
inline constexpr int VK_NUMLOCK = 0x90;
inline constexpr int VK_SCROLL = 0x91;
inline constexpr int VK_LSHIFT = 0xA0;
inline constexpr int VK_RSHIFT = 0xA1;
inline constexpr int VK_LCONTROL = 0xA2;
inline constexpr int VK_RCONTROL = 0xA3;
inline constexpr int VK_LMENU = 0xA4;
inline constexpr int VK_RMENU = 0xA5;
inline constexpr int VK_OEM_1 = 0xBA;
inline constexpr int VK_OEM_PLUS = 0xBB;
inline constexpr int VK_OEM_COMMA = 0xBC;
inline constexpr int VK_OEM_MINUS = 0xBD;
inline constexpr int VK_OEM_PERIOD = 0xBE;
inline constexpr int VK_OEM_2 = 0xBF;
inline constexpr int VK_OEM_3 = 0xC0;
inline constexpr int VK_OEM_4 = 0xDB;
inline constexpr int VK_OEM_5 = 0xDC;
inline constexpr int VK_OEM_6 = 0xDD;
inline constexpr int VK_OEM_7 = 0xDE;
inline constexpr int VK_OEM_102 = 0xE2;

#endif
