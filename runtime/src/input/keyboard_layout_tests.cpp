// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/key_positions.h"
#include "galaxy/input/key_codes.h"
#include <clocale>
#include <iostream>
#include <stdexcept>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Keyboard position policy must not include Windows.h
#endif

namespace {
using namespace galaxy::input;
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void layouts() {
    const auto config = default_controls_config();
    const auto& table = config.table(ControlScheme::Keyboard);
    KeyPositionMap azerty;
    azerty.keys['A'-'A'] = 'Q'; azerty.keys['Q'-'A'] = 'A';
    azerty.keys['W'-'A'] = 'Z'; azerty.keys['Z'-'A'] = 'W';
    KeyboardMouseLevels levels;
    levels.set_key('Z', true); levels.set_key('Q', true);
    auto input = evaluate_keyboard_bindings(table, project_key_positions(levels, azerty));
    const auto old = evaluate_keyboard_bindings(legacy_binding_table(ControlScheme::Keyboard), levels);
    check(old.stick_x == 0 && old.stick_y == 0, "legacy character defaults miss AZERTY movement positions");
    check(input.stick_y == 1 && input.stick_x == -1 && !input.held(GameInput::Z),
          "AZERTY physical movement positions without a false crouch");
    std::cout << "AZERTY fixture: old stick=0,0; position bindings=-1,1\n";
    const auto labels = key_position_labels(table, azerty);
    check(labels[GameInput::StickUp][0] == InputSource{SourceKind::Key, 'Z'} &&
          labels[GameInput::StickLeft][0] == InputSource{SourceKind::Key, 'Q'}, "AZERTY binding labels follow positions");

    KeyPositionMap dvorak;
    dvorak.keys = {'A','X','J','E',keys::OEM_PERIOD,'U','I','D','C','H','T','N','M','B','R','L',
                   keys::OEM_7,'P','O','Y','G','K',keys::OEM_COMMA,'Q','F',keys::OEM_1};
    levels = {}; levels.set_key(keys::OEM_COMMA, true); levels.set_key('E', true);
    input = evaluate_keyboard_bindings(table, project_key_positions(levels, dvorak));
    const auto old_dvorak = evaluate_keyboard_bindings(legacy_binding_table(ControlScheme::Keyboard), levels);
    check(old_dvorak.stick_x == 0 && old_dvorak.stick_y == 0, "legacy character defaults miss Dvorak movement positions");
    check(input.stick_y == 1 && input.stick_x == 1 && !input.held(GameInput::Shake),
          "Dvorak punctuation and letter positions drive movement");
    std::cout << "Dvorak fixture: old stick=0,0; position bindings=1,1\n";
    const auto dvorak_labels = key_position_labels(table, dvorak);
    check(dvorak_labels[GameInput::StickUp][0] == InputSource{SourceKind::Key, keys::OEM_COMMA},
          "Dvorak key label is the source at physical W");

    BindingTable custom;
    custom[GameInput::A] = {InputSource{SourceKind::Key, 'W'}};
    levels = {}; levels.set_key('W', true);
    input = evaluate_keyboard_bindings(custom, project_key_positions(levels, azerty));
    check(input.held(GameInput::A) && !levels.position('W'), "custom logical key stays a character binding");
    auto missing = azerty; missing.keys['W'-'A'] = 0;
    levels = {}; levels.set_key('Z', true);
    input = evaluate_keyboard_bindings(table, project_key_positions(levels, missing));
    check(input.stick_y == 0 && key_position_labels(table, missing)[GameInput::StickUp][0].empty(),
          "unmapped position is unavailable");
}

void persistence() {
    const InputSource position{SourceKind::KeyPosition, 'W'};
    check(format_input_source(ControlScheme::Keyboard, position) == "Position.W" &&
          parse_input_source(ControlScheme::Keyboard, "position.w") == position, "position binding round trip");
    const std::string original_locale = std::setlocale(LC_CTYPE, nullptr);
    const bool turkish = std::setlocale(LC_CTYPE, "tr-TR") != nullptr ||
                         std::setlocale(LC_CTYPE, "Turkish_Turkey.1254") != nullptr;
    if (turkish) {
        const bool ok = parse_input_source(ControlScheme::Keyboard, "POSITION.i") ==
                        InputSource{SourceKind::KeyPosition, 'I'};
        (void)std::setlocale(LC_CTYPE, original_locale.c_str());
        check(ok, "position binding IDs remain ASCII under Turkish case rules");
        std::cout << "Turkish C locale: ASCII position IDs passed\n";
    } else std::cout << "Turkish C locale unavailable; locale-specific check skipped\n";
    check(!parse_input_source(ControlScheme::Keyboard, "Position.1") &&
          !binding_source_allowed(ControlScheme::Xbox, GameInput::A, position), "position source validation");
    auto config = default_controls_config();
    config.table(ControlScheme::Keyboard) = legacy_binding_table(ControlScheme::Keyboard);
    config.table(ControlScheme::Keyboard)[GameInput::StickUp] = {InputSource{SourceKind::Key, 'T'}};
    config.tune(ControlScheme::Xbox).pointer_hide_seconds = 5;
    config.tune(ControlScheme::Xbox).rumble = 50;
    const auto tuning = config.tune(ControlScheme::Xbox);
    check(migrate_legacy_controls(config, 5) > 0, "version five keyboard defaults migrate");
    check(config.table(ControlScheme::Keyboard)[GameInput::StickUp][0] == InputSource{SourceKind::Key, 'T'} &&
          config.table(ControlScheme::Keyboard)[GameInput::StickLeft][0] == InputSource{SourceKind::KeyPosition, 'A'},
          "migration preserves custom keys and converts unchanged defaults");
    check(config.tune(ControlScheme::Xbox) == tuning, "keyboard migration leaves version-five controller tuning alone");
    config.table(ControlScheme::Keyboard)[GameInput::StickUp] = {InputSource{SourceKind::Key, 'W'}};
    check(migrate_legacy_controls(config, kControlsVersion) == 0 &&
          config.table(ControlScheme::Keyboard)[GameInput::StickUp][0].kind == SourceKind::Key,
          "current-version intentional logical defaults are preserved");
    auto loaded = default_controls_config();
    const auto text = format_controls_settings(config);
    std::size_t first = 0;
    while (first < text.size()) {
        const auto end = text.find('\n', first);
        const auto line = std::string_view(text).substr(first, end == std::string::npos ? text.size()-first : end-first);
        const auto equals = line.find('=');
        if (equals != std::string_view::npos) (void)apply_controls_setting(loaded, line.substr(0, equals), line.substr(equals+1));
        if (end == std::string::npos) break;
        first = end + 1;
    }
    check(loaded == config, "mixed logical/position settings preserve every binding and tuning value");
}
}
int main() {
    try { layouts(); persistence(); std::cout << "keyboard positions, labels and migration passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
