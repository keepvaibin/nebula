#include "launcher_model.h"

#include <commctrl.h>
#include <hidsdi.h>
#include <psapi.h>
#include <setupapi.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <thread>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "comctl32.lib")

namespace nebula::launcher {

// ---- settings files ----------------------------------------------------------------------------------------------

Snapshot MakeSnapshot(const KeyValues& values) {
    Snapshot snapshot;
    for (const auto& pair : values) snapshot[pair.first] = pair.second;
    return snapshot;
}

static std::optional<wstring> LineKey(const wstring& line) {
    size_t equals = line.find(L'=');
    if (equals == wstring::npos || equals == 0) return std::nullopt;
    return Trim(line.substr(0, equals));
}

void SaveSettingsFile(const wstring& path, const KeyValues& values, const Snapshot* read) {
    std::vector<wstring> lines = FileExists(path) ? ReadAllLines(path) : std::vector<wstring>();
    for (const auto& pair : values) {
        int index = -1;
        for (size_t i = 0; i < lines.size(); i++) {
            auto key = LineKey(lines[i]);
            if (key && IEquals(*key, pair.first)) { index = int(i); break; }
        }
        if (index >= 0 && read) {
            auto before = read->find(pair.first);
            if (before != read->end() && before->second == pair.second) continue;
        }
        wstring line = pair.first + L"=" + pair.second;
        if (index >= 0) lines[size_t(index)] = line;
        else lines.push_back(line);
    }
    CreateDirs(DirName(path));
    wstring temp = path + L".tmp";
    WriteAllText(temp, Join(lines, L"\n") + L"\n");
    ReplaceFileWith(temp, path);
}

wstring RuntimeSettingsPath() { return PathCombine(paths::DataRoot(), L"runtime_settings.ini"); }

static std::vector<wstring> ReadLinesOrEmpty(const wstring& path, bool& failed) {
    failed = false;
    try {
        return FileExists(path) ? ReadAllLines(path) : std::vector<wstring>();
    } catch (const Error& error) {
        if (error.kind != ErrorKind::Io && error.kind != ErrorKind::Access && error.kind != ErrorKind::NotFound) throw;
        failed = true;
        return {};
    }
}

// ---- graphics ----------------------------------------------------------------------------------------------------

const std::vector<wstring> GraphicsSettings::kDisplayModes = {L"Windowed", L"Borderless fullscreen"};
const std::vector<wstring> GraphicsSettings::kAspects = {L"Auto (match the window)", L"4:3", L"16:10", L"16:9", L"21:9", L"32:9"};
const std::vector<wstring> GraphicsSettings::kAntiAliasingModes = {L"Off", L"SMAA", L"Auto (SMAA unless supersampled)", L"FXAA"};
const std::vector<wstring> GraphicsSettings::kAntiAliasingQualities = {L"Low", L"Medium", L"High", L"Ultra"};
// Release runtimes are built without FSR 3 (upscaler 2); a saved 2 reads as FSR 1.
const std::vector<wstring> GraphicsSettings::kUpscalers = {L"Bilinear", L"AMD FidelityFX FSR 1"};
const std::vector<wstring> GraphicsSettings::kDlssQualities = {L"DLAA", L"Quality", L"Balanced", L"Performance", L"Ultra Performance"};
const std::vector<wstring> GraphicsSettings::kTextureFilters = {L"Trilinear", L"2x anisotropic", L"4x anisotropic", L"8x anisotropic",
                                                                L"16x anisotropic"};
const std::vector<wstring> GraphicsSettings::kLightingModes = {L"Per pixel (enhanced)", L"Per vertex (original)"};
static const int kTemporalQualityCount = 5;
static const int kAnisotropy[] = {1, 2, 4, 8, 16};
static const int kStandardFrameRates[] = {30, 60, 120, 144, 165, 240, 360};
static const int kAspectRatios[6][2] = {{0, 0}, {4, 3}, {16, 10}, {16, 9}, {21, 9}, {32, 9}};
static const int kWindowHeights[] = {360, 480, 540, 600, 720, 768, 800, 900, 960, 1050, 1080, 1200, 1440, 1536, 1600, 1800, 2160, 2400, 2880, 4320};

std::vector<int> GraphicsSettings::UpscalerChoices() {
    std::vector<int> choices = {0, 1};
    if (DlssAdapter()) choices.push_back(3);
    return choices;
}

bool GraphicsSettings::PackagePrefersIntegratedGpu() {
    try {
        wstring path = PathCombine(ModuleDirectory(), L"runtime-env.json");
        if (!FileExists(path)) return false;
        Json list = ParseJson(ReadAllText(path));
        for (const auto& entry : list.items) {
            if (JStr(&entry, L"name").value_or(L"") == L"GALAXY_GPU_PREFERENCE")
                return IEquals(JStr(&entry, L"value").value_or(L""), L"low");
        }
    } catch (const Error&) {
    }
    return false;
}

bool GraphicsSettings::DlssAdapter() {
    bool lowPower = PackagePrefersIntegratedGpu();
    wstring path = PathCombine(paths::DataRoot(), L"adapter.ini");
    try {
        if (FileExists(path)) {
            std::map<wstring, wstring> values;
            for (const auto& line : ReadAllLines(path)) {
                size_t equals = line.find(L'=');
                if (equals != wstring::npos && equals > 0) values[Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
            }
            auto preference = values.find(L"gpu_preference");
            auto vendor = values.find(L"vendor_id");
            auto description = values.find(L"description");
            if (preference != values.end() && (preference->second == L"low") == lowPower && vendor != values.end() &&
                description != values.end())
                return vendor->second == L"4318" && IFind(description->second, L"RTX") != wstring::npos;
        }
    } catch (const Error&) {
    }
    return !lowPower && dlss::RtxAdapterPresent();
}

wstring GraphicsSettings::UpscalerName(int value) {
    if (value == 3) return dlss::Installed() ? L"NVIDIA DLSS" : L"NVIDIA DLSS (download)";
    return kUpscalers[size_t(std::max(0, std::min(value, int(kUpscalers.size()) - 1)))];
}

std::vector<int> GraphicsSettings::FrameRates(int refreshHz) {
    std::vector<int> rates;
    bool listed = false;
    for (int rate : kStandardFrameRates) {
        if (refreshHz != 0 && rate > refreshHz + 1) break;
        rates.push_back(rate);
        listed = listed || (refreshHz + 1 >= rate && refreshHz <= rate + 1);
    }
    if (refreshHz > 30 && !listed) {
        auto at = std::find_if(rates.begin(), rates.end(), [&](int r) { return r > refreshHz; });
        rates.insert(at, refreshHz);
    }
    rates.push_back(0);
    return rates;
}

std::vector<Size> GraphicsSettings::AspectWindowSizes(int aspect, int maxWidth, int maxHeight) {
    std::vector<Size> sizes;
    if (aspect <= 0 || aspect >= int(kAspects.size())) return sizes;
    int ratioWidth = kAspectRatios[aspect][0], ratioHeight = kAspectRatios[aspect][1];
    for (int height : kWindowHeights) {
        if (height * ratioWidth % ratioHeight != 0) continue;
        int width = height * ratioWidth / ratioHeight;
        if (width <= maxWidth && height <= maxHeight) sizes.push_back({width, height});
    }
    return sizes;
}

int GraphicsSettings::SnapWindowSize(const std::vector<Size>& sizes, Size want) {
    for (size_t i = 0; i < sizes.size(); i++)
        if (sizes[i].h >= want.h) return int(i);
    return int(sizes.size()) - 1;
}

wstring GraphicsSettings::ConfigPath() { return PathCombine(paths::DataRoot(), L"config.ini"); }

static int Choice(const std::map<wstring, wstring>& values, const wstring& key, int count, int fallback) {
    auto it = values.find(key);
    long long value;
    if (it == values.end() || !ParseInt(it->second, value) || value < 0 || value >= count) return fallback;
    return int(value);
}

static std::optional<wstring> Value(const std::map<wstring, wstring>& values, const wstring& key) {
    auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
}

GraphicsSettings GraphicsSettings::Load() {
    GraphicsSettings settings;
    std::map<wstring, wstring> values;  // keys lower-cased: the file's keys compare without case
    bool failed = false;
    for (const auto& line : ReadLinesOrEmpty(ConfigPath(), failed)) {
        size_t equals = line.find(L'=');
        wstring key = equals != wstring::npos && equals > 0 ? Trim(line.substr(0, equals)) : L"";
        if (!key.empty() && key[0] != L'#' && key[0] != L';') values[ToLower(key)] = line.substr(equals + 1);
    }
    if (failed) return settings;
    // Exclusive fullscreen (2) is not offered; it opens borderless.
    settings.DisplayMode = std::min(Choice(values, L"display_mode", 3, settings.DisplayMode), 1);
    if (auto monitor = Value(values, L"monitor")) settings.Monitor = Trim(*monitor);
    settings.WindowWidth = Choice(values, L"window_width", 65536, settings.WindowWidth);
    settings.WindowHeight = Choice(values, L"window_height", 65536, settings.WindowHeight);
    settings.FullscreenWidth = Choice(values, L"fullscreen_width", 16385, 0);
    settings.FullscreenHeight = Choice(values, L"fullscreen_height", 16385, 0);
    settings.FullscreenRefresh = Choice(values, L"fullscreen_refresh_hz", 1001, 0);
    settings.Aspect = Choice(values, L"aspect", int(kAspects.size()), settings.Aspect);
    settings.EfbScale = std::max(1, Choice(values, L"efb_scale", 17, settings.EfbScale));
    if (auto follow = Value(values, L"internal_follow_window")) settings.MatchOutput = Trim(*follow) != L"0";
    if (auto vsync = Value(values, L"vsync_interval")) settings.VSync = Trim(*vsync) != L"0";
    double rate;
    if (auto fps = Value(values, L"max_fps"); fps && ParseDouble(*fps, rate) && (rate == 0 || (rate >= 1 && rate <= 1000)))
        settings.FrameRate = int(std::nearbyint(rate));
    settings.AntiAliasing = Choice(values, L"aa", int(kAntiAliasingModes.size()), settings.AntiAliasing);
    settings.Quality = Choice(values, L"smaa_quality", int(kAntiAliasingQualities.size()), settings.Quality);
    int upscaler = Choice(values, L"upscaler", 4, settings.Upscaler);
    settings.Upscaler = upscaler == 2 ? 1 : upscaler;
    settings.TemporalQuality = Choice(values, L"temporal_quality", kTemporalQualityCount, settings.TemporalQuality);
    settings.Sharpness = std::min(Choice(values, L"sharpness", 101, settings.Sharpness), 100);
    int anisotropy = Choice(values, L"anisotropic_filtering", 17, -1);
    if (anisotropy >= 1) {
        settings.TextureFilter = 0;
        for (int i = 0; i < 5; i++)
            if (kAnisotropy[i] <= anisotropy) settings.TextureFilter = i;
    }
    if (auto mipmaps = Value(values, L"enhanced_mipmaps")) settings.GeneratedMipmaps = Trim(*mipmaps) != L"0";
    if (auto lighting = Value(values, L"per_pixel_lighting")) settings.PerPixelLighting = Trim(*lighting) != L"0";
    if (auto hud = Value(values, L"hud_output_resolution")) settings.HudOutputResolution = Trim(*hud) != L"0";
    settings.read_ = MakeSnapshot(settings.Values());
    return settings;
}

KeyValues GraphicsSettings::Values() const {
    return {
        {L"display_mode", Num(DisplayMode)},
        {L"monitor", Monitor},
        {L"window_width", Num(WindowWidth)},
        {L"window_height", Num(WindowHeight)},
        {L"fullscreen_width", Num(FullscreenWidth)},
        {L"fullscreen_height", Num(FullscreenHeight)},
        {L"fullscreen_refresh_hz", Num(FullscreenRefresh)},
        {L"aspect", Num(Aspect)},
        {L"efb_scale", Num(EfbScale)},
        {L"internal_follow_window", MatchOutput ? L"1" : L"0"},
        {L"vsync_interval", VSync ? L"1" : L"0"},
        {L"max_fps", Num(FrameRate)},
        {L"aa", Num(AntiAliasing)},
        {L"smaa_quality", Num(Quality)},
        {L"upscaler", Num(Upscaler)},
        {L"temporal_quality", Num(TemporalQuality)},
        {L"sharpness", Num(Sharpness)},
        {L"anisotropic_filtering", Num(kAnisotropy[std::max(0, std::min(TextureFilter, 4))])},
        {L"enhanced_mipmaps", GeneratedMipmaps ? L"1" : L"0"},
        {L"per_pixel_lighting", PerPixelLighting ? L"1" : L"0"},
        {L"hud_output_resolution", HudOutputResolution ? L"1" : L"0"},
    };
}

void GraphicsSettings::Save() {
    KeyValues values = Values();
    SaveSettingsFile(ConfigPath(), values, read_ ? &*read_ : nullptr);
    read_ = MakeSnapshot(values);
}

// ---- audio and general -------------------------------------------------------------------------------------------

const std::vector<wstring> AudioSettings::kNames = {L"Master", L"Music", L"Sound effects", L"Voices", L"Ambience", L"Cutscenes"};
static const wchar_t* const kAudioKeys[] = {L"audio_master_gain", L"audio_music_gain", L"audio_sfx_gain",
                                            L"audio_voice_gain", L"audio_ambience_gain", L"audio_cutscene_gain"};

AudioSettings AudioSettings::Load() {
    AudioSettings settings;
    bool failed = false;
    auto lines = ReadLinesOrEmpty(RuntimeSettingsPath(), failed);
    if (failed) return settings;
    for (const auto& line : lines) {
        size_t equals = line.find(L'=');
        if (equals == wstring::npos || equals == 0) continue;
        wstring key = Trim(line.substr(0, equals));
        for (int index = 0; index < 6; index++) {
            if (key != kAudioKeys[index]) continue;
            float gain;
            if (ParseFloat(line.substr(equals + 1), gain) && std::isfinite(gain))
                settings.Percent[index] = int(std::nearbyint(std::max(0.0f, std::min(gain, 2.0f)) * 100.0f));
        }
    }
    settings.read_ = MakeSnapshot(settings.Values());
    return settings;
}

KeyValues AudioSettings::Values() const {
    KeyValues values;
    for (int k = 0; k < 6; k++) values.emplace_back(kAudioKeys[k], FloatR(float(Percent[k]) / 100.0f));
    return values;
}

void AudioSettings::Save() {
    KeyValues values = Values();
    SaveSettingsFile(RuntimeSettingsPath(), values, read_ ? &*read_ : nullptr);
    read_ = MakeSnapshot(values);
}

const std::vector<wstring> GeneralSettings::kTextSpeeds = {L"1x (original)", L"2x", L"3x"};
const std::vector<wstring> GeneralSettings::kLanguages = {L"English", L"Français", L"Español"};
static const wchar_t* const kLanguageKeys[] = {L"english", L"french", L"spanish"};

GeneralSettings GeneralSettings::Load() {
    GeneralSettings settings;
    bool failed = false;
    auto lines = ReadLinesOrEmpty(RuntimeSettingsPath(), failed);
    if (failed) return settings;
    for (const auto& line : lines) {
        size_t equals = line.find(L'=');
        if (equals == wstring::npos || equals == 0) continue;
        wstring key = Trim(line.substr(0, equals));
        wstring value = Trim(line.substr(equals + 1));
        long long speed;
        if (key == L"text_speed" && ParseInt(value, speed) && speed >= 1 && speed <= 3) settings.TextSpeed = int(speed);
        if (key == L"language")
            for (int i = 0; i < 3; i++)
                if (IEquals(kLanguageKeys[i], value)) { settings.Language = i; break; }
    }
    settings.read_ = MakeSnapshot(settings.Values());
    return settings;
}

KeyValues GeneralSettings::Values() const {
    return {{L"text_speed", Num(std::max(1, std::min(TextSpeed, 3)))}, {L"language", kLanguageKeys[std::max(0, std::min(Language, 2))]}};
}

void GeneralSettings::Save() {
    KeyValues values = Values();
    SaveSettingsFile(RuntimeSettingsPath(), values, read_ ? &*read_ : nullptr);
    read_ = MakeSnapshot(values);
}

// ---- controls ----------------------------------------------------------------------------------------------------

using CS = ControlsSettings;
const std::vector<wstring> CS::kSchemeIds = {L"keyboard", L"xbox", L"switch_pro", L"switch_pro2"};
const std::vector<wstring> CS::kSchemeLabels = {L"Keyboard and mouse", L"Xbox controller (XInput)", L"Switch Pro Controller",
                                               L"Switch 2 Pro Controller"};
const std::vector<wstring> CS::kTargetIds = {L"a", L"b", L"one", L"two", L"plus", L"minus", L"home", L"dpad_up", L"dpad_down",
                                            L"dpad_left", L"dpad_right", L"stick_up", L"stick_down", L"stick_left", L"stick_right",
                                            L"c", L"z", L"shake", L"nunchuk_shake", L"pointer_up", L"pointer_down", L"pointer_left",
                                            L"pointer_right", L"pointer_recenter", L"jump_back"};
const std::vector<wstring> CS::kTargetLabels = {
    L"A", L"B", L"1", L"2", L"Plus", L"Minus", L"Home", L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
    L"Nunchuk stick up", L"Nunchuk stick down", L"Nunchuk stick left", L"Nunchuk stick right", L"C", L"Z",
    L"Shake (Wii Remote)", L"Shake (Nunchuk)", L"Pointer up", L"Pointer down", L"Pointer left", L"Pointer right",
    L"Pointer recenter", L"A in play, B in menus"};
const std::vector<wstring> CS::kMotionNames = {L"off", L"remote", L"nunchuk"};
const std::vector<wstring> CS::kMotionLabels = {L"Off", L"Wii Remote shake", L"Nunchuk shake"};
const std::vector<int> CS::kRumbleSteps = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
const std::vector<wstring> CS::kRumbleLabels = {L"10%", L"20%", L"30%", L"40%", L"50%", L"60%", L"70%", L"80%", L"90%", L"100%"};
static const float kVersion3PointerHideSeconds = 4.0f, kVersion3PointerHideRadius = 0.03f;
static const float kVersion4PointerHideSeconds = 2.0f;
static const int kVersion4Rumble = 100;

static const wchar_t* const kXboxButtons[] = {L"DpadUp", L"DpadDown", L"DpadLeft", L"DpadRight", L"Menu", L"View", L"LS", L"RS", L"LB",
                                              L"RB", L"Guide", L"Share", L"A", L"B", L"X", L"Y", L"GL", L"GR", L"C"};
static const wchar_t* const kSwitchButtons[] = {L"DpadUp", L"DpadDown", L"DpadLeft", L"DpadRight", L"Plus", L"Minus", L"LS", L"RS", L"L",
                                                L"R", L"Home", L"Capture", L"B", L"A", L"Y", L"X", L"GL", L"GR", L"C"};
static const wchar_t* const kStickAxes[] = {L"LX", L"LY", L"RX", L"RY"};
static const wchar_t* const kMouseNames[] = {L"MouseLeft", L"MouseRight", L"MouseMiddle", L"Mouse4", L"Mouse5"};
static const std::pair<int, const wchar_t*> kNamedKeys[] = {
    {0x20, L"Space"}, {0x0D, L"Enter"}, {0x09, L"Tab"}, {0x08, L"Backspace"}, {0x1B, L"Escape"}, {0xA0, L"LShift"}, {0xA1, L"RShift"},
    {0x10, L"Shift"}, {0xA2, L"LCtrl"}, {0xA3, L"RCtrl"}, {0x11, L"Ctrl"}, {0xA4, L"LAlt"}, {0xA5, L"RAlt"}, {0x12, L"Alt"},
    {0x26, L"Up"}, {0x28, L"Down"}, {0x25, L"Left"}, {0x27, L"Right"}, {0x2D, L"Insert"}, {0x2E, L"Delete"}, {0x24, L"Home"},
    {0x23, L"End"}, {0x21, L"PageUp"}, {0x22, L"PageDown"}, {0x14, L"CapsLock"}, {0xBD, L"Minus"}, {0xBB, L"Equals"},
    {0xDB, L"LBracket"}, {0xDD, L"RBracket"}, {0xDC, L"Backslash"}, {0xBA, L"Semicolon"}, {0xDE, L"Quote"}, {0xBC, L"Comma"},
    {0xBE, L"Period"}, {0xBF, L"Slash"}, {0xC0, L"Grave"}, {0x6B, L"NumAdd"}, {0x6D, L"NumSub"}, {0x6A, L"NumMul"},
    {0x6F, L"NumDiv"}, {0x6E, L"NumDecimal"}};

static InputSource Key(int vk) { return {SourceKind::Key, vk}; }
static InputSource Mouse(int b) { return {SourceKind::Mouse, b}; }
static InputSource Btn(int b) { return {SourceKind::Button, b}; }
static InputSource Pos(int a) { return {SourceKind::AxisPositive, a}; }
static InputSource Neg(int a) { return {SourceKind::AxisNegative, a}; }

CS::ControlsSettings() {
    for (int s = 0; s < kSchemeCount; s++) {
        Table[s] = DefaultTable(s);
        Tuning[s] = DefaultTuning(s);
    }
}

bool CS::IsAnalog(int target) { return (target >= 11 && target <= 14) || (target >= 19 && target <= 22); }
bool CS::HasMotion(int scheme) { return scheme == kSwitchPro || scheme == kSwitchPro2; }

int CS::SnapRumble(float percent) {
    int best = kRumbleSteps[0];
    for (int step : kRumbleSteps)
        if (std::fabs(percent - float(step)) < std::fabs(percent - float(best))) best = step;
    return best;
}

std::vector<Bindings> CS::LegacyTable(int scheme) {
    std::vector<Bindings> t(kTargetCount);
    if (scheme == kKeyboard) {
        t[0] = {Key(0x20), Key(0x0D), Mouse(0)};
        t[1] = {Key(0xA0), Key(0xA1), Mouse(1)};
        t[2] = {Key('1'), Key(0x61)};
        t[3] = {Key('2'), Key(0x62)};
        t[4] = {Key(0xBB), Key('P')};
        t[5] = {Key(0xBD), Key('M')};
        t[6] = {Key('H')};
        t[7] = {Key(0x26)}; t[8] = {Key(0x28)}; t[9] = {Key(0x25)}; t[10] = {Key(0x27)};
        t[11] = {Key('W')}; t[12] = {Key('S')}; t[13] = {Key('A')}; t[14] = {Key('D')};
        t[15] = {Key('C')};
        t[16] = {Key('Z'), Key(0xA2), Key(0xA3)};
        t[17] = {Key('Q'), Key('E')};
        t[19] = {Key('I')}; t[20] = {Key('K')}; t[21] = {Key('J')}; t[22] = {Key('L')};
        t[23] = {Key('R')};
        return t;
    }
    t[7] = {Btn(DpadUp)}; t[8] = {Btn(DpadDown)}; t[9] = {Btn(DpadLeft)}; t[10] = {Btn(DpadRight)};
    t[4] = {Btn(Start)}; t[5] = {Btn(Back)};
    t[11] = {Pos(LeftY)}; t[12] = {Neg(LeftY)}; t[13] = {Neg(LeftX)}; t[14] = {Pos(LeftX)};
    t[15] = {Btn(LeftStick)};
    t[16] = {Btn(LeftShoulder), Pos(LeftTrigger)};
    t[19] = {Pos(RightY)}; t[20] = {Neg(RightY)}; t[21] = {Neg(RightX)}; t[22] = {Pos(RightX)};
    t[23] = {Btn(RightStick)};
    t[17] = {Btn(West), Btn(North), Btn(RightShoulder)};
    if (scheme == kXbox) {
        t[0] = {Btn(South)};
        t[1] = {Btn(East), Pos(RightTrigger)};
    } else {
        t[0] = {Btn(East)};
        t[1] = {Btn(South), Pos(RightTrigger)};
        t[6] = {Btn(Home)};
    }
    return t;
}

std::vector<Bindings> CS::Version2Table(int scheme) {
    auto t = LegacyTable(scheme);
    if (scheme == kKeyboard) return t;
    bool xbox = scheme == kXbox;
    t[0] = {Btn(xbox ? South : East)};
    t[kJumpBack] = {Btn(xbox ? East : South)};
    t[1] = {Pos(RightTrigger)};
    t[16] = {Pos(LeftTrigger)};
    t[15] = {Btn(LeftShoulder), Btn(LeftStick)};
    if (xbox) {
        t[17] = {Btn(West), Btn(North), Btn(RightShoulder)};
        t[23] = {Btn(RightStick)};
    } else {
        t[17] = {Btn(West), Btn(North)};
        t[23] = {Btn(RightShoulder)};
        t[7] = {Btn(DpadUp), Pos(RightY)};
        t[8] = {Btn(DpadDown), Neg(RightY)};
        t[9] = {Btn(DpadLeft), Neg(RightX)};
        t[10] = {Btn(DpadRight), Pos(RightX)};
    }
    return t;
}

std::vector<Bindings> CS::Version3Table(int scheme) {
    auto t = Version2Table(scheme);
    if (scheme == kKeyboard) return t;
    t[1] = {Btn(LeftShoulder)};
    t[16] = {Pos(LeftTrigger), Pos(RightTrigger)};
    t[15] = {Btn(LeftStick)};
    return t;
}

std::vector<Bindings> CS::DefaultTable(int scheme) {
    auto t = Version3Table(scheme);
    if (scheme == kKeyboard) return t;
    t[16] = {Pos(LeftTrigger)};
    t[17] = scheme == kXbox ? Bindings{Btn(West), Btn(North), Btn(RightShoulder), Pos(RightTrigger)}
                            : Bindings{Btn(West), Btn(North), Pos(RightTrigger)};
    return t;
}

SchemeTuning CS::LegacyTuning(int scheme) {
    SchemeTuning t;
    if (scheme == kXbox) {
        t.StickDeadzone = 7849.0f / 32767.0f;
        t.PointerDeadzone = 8689.0f / 32767.0f;
        t.PointerSpeed = 2.4f;
        t.PointerHideSeconds = 4.0f;
    } else if (HasMotion(scheme)) {
        t.StickDeadzone = 0.12f;
        t.PointerDeadzone = 0.12f;
        t.PointerSpeed = 2.4f;
        t.Motion = 1;
        t.PointerHideSeconds = 4.0f;
    }
    return t;
}

SchemeTuning CS::DefaultTuning(int scheme) {
    SchemeTuning t = LegacyTuning(scheme);
    if (HasMotion(scheme)) t.GyroPointer = true;
    if (scheme != kKeyboard) {
        t.PointerHideSeconds = 4.0f;
        t.PointerHideRadius = 0.12f;
    }
    return t;
}

void CS::MigrateLegacy(int version) {
    for (int s = 0; s < kSchemeCount; s++) {
        auto legacy = LegacyTable(s), version2 = Version2Table(s), version3 = Version3Table(s), current = DefaultTable(s);
        for (int t = 0; t < kTargetCount; t++) {
            bool old = Table[s][t] == legacy[t] || Table[s][t] == version2[t] || Table[s][t] == version3[t];
            if (old && !(Table[s][t] == current[t])) Table[s][t] = current[t];
        }
        if (version < 2 && Tuning[s].GyroPointer == LegacyTuning(s).GyroPointer) Tuning[s].GyroPointer = DefaultTuning(s).GyroPointer;
        if (version < kControlsVersion && s != kKeyboard) {
            SchemeTuning defaults = DefaultTuning(s);
            bool oldSeconds = (version < 4 && Tuning[s].PointerHideSeconds == kVersion3PointerHideSeconds) ||
                              Tuning[s].PointerHideSeconds == kVersion4PointerHideSeconds;
            if (oldSeconds) Tuning[s].PointerHideSeconds = defaults.PointerHideSeconds;
            if (version < 4 && Tuning[s].PointerHideRadius == kVersion3PointerHideRadius) Tuning[s].PointerHideRadius = defaults.PointerHideRadius;
            if (Tuning[s].Rumble == kVersion4Rumble) Tuning[s].Rumble = defaults.Rumble;
        }
    }
    if (version < 2 && InputMode == kModeKeyboardMouse) InputMode = kModeAuto;
}

wstring CS::FormatKey(int vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return wstring(1, wchar_t(vk));
    if (vk >= 0x60 && vk <= 0x69) return L"Num" + Num(vk - 0x60);
    if (vk >= 0x70 && vk <= 0x87) return L"F" + Num(vk - 0x70 + 1);
    for (const auto& key : kNamedKeys)
        if (key.first == vk) return key.second;
    wchar_t buffer[16];
    swprintf_s(buffer, L"VK%02X", vk);
    return buffer;
}

wstring CS::FormatSource(int scheme, InputSource source) {
    switch (source.kind) {
        case SourceKind::Key: return FormatKey(source.code);
        case SourceKind::Mouse: return source.code < 5 ? kMouseNames[source.code] : L"None";
        case SourceKind::Button:
            return source.code < ButtonCount ? (scheme == kXbox ? kXboxButtons : kSwitchButtons)[source.code] : L"None";
        case SourceKind::AxisPositive:
        case SourceKind::AxisNegative:
            if (source.code == LeftTrigger) return scheme == kXbox ? L"LT" : L"ZL";
            if (source.code == RightTrigger) return scheme == kXbox ? L"RT" : L"ZR";
            if (source.code >= 4) return L"None";
            return wstring(kStickAxes[source.code]) + (source.kind == SourceKind::AxisPositive ? L"+" : L"-");
        default: return L"None";
    }
}

bool CS::TryParseSource(int scheme, wstring text, InputSource& source) {
    source = {};
    text = Trim(text);
    if (text.empty()) return false;
    if (scheme == kKeyboard) {
        for (int i = 0; i < 5; i++)
            if (IEquals(text, kMouseNames[i])) { source = Mouse(i); return true; }
        if (text.size() == 1) {
            wchar_t c = ToUpper(text)[0];
            if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')) { source = Key(c); return true; }
        }
        for (const auto& key : kNamedKeys)
            if (IEquals(text, key.second)) { source = Key(key.first); return true; }
        long long n;
        if (text.size() == 4 && StartsWith(text, L"Num", true) && ParseInt(text.substr(3), n)) { source = Key(int(0x60 + n)); return true; }
        if (text.size() >= 2 && text.size() <= 3 && (text[0] == L'F' || text[0] == L'f')) {
            wstring digits = text.substr(1);
            bool allDigits = std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
            if (allDigits && ParseInt(digits, n) && n >= 1 && n <= 24) { source = Key(int(0x70 + n - 1)); return true; }
        }
        if (text.size() == 4 && StartsWith(text, L"VK", true)) {
            wchar_t* end = nullptr;
            wstring hex = text.substr(2);
            bool isHex = std::all_of(hex.begin(), hex.end(), [](wchar_t c) { return iswxdigit(c) != 0; });
            long value = isHex ? wcstol(hex.c_str(), &end, 16) : 0;
            if (isHex && value > 0) { source = Key(int(value)); return true; }
        }
        return false;
    }
    const wchar_t* const* names = scheme == kXbox ? kXboxButtons : kSwitchButtons;
    for (int i = 0; i < ButtonCount; i++)
        if (IEquals(text, names[i])) { source = Btn(i); return true; }
    if (IEquals(text, scheme == kXbox ? L"LT" : L"ZL")) { source = Pos(LeftTrigger); return true; }
    if (IEquals(text, scheme == kXbox ? L"RT" : L"ZR")) { source = Pos(RightTrigger); return true; }
    if (text.size() == 3 && (text[2] == L'+' || text[2] == L'-'))
        for (int i = 0; i < 4; i++)
            if (IEquals(text.substr(0, 2), kStickAxes[i])) { source = text[2] == L'+' ? Pos(i) : Neg(i); return true; }
    return false;
}

wstring CS::FormatBindings(int scheme, const Bindings& bindings) {
    std::vector<wstring> parts;
    for (const auto& b : bindings)
        if (!b.Empty()) parts.push_back(FormatSource(scheme, b));
    return parts.empty() ? L"None" : Join(parts, L", ");
}

bool CS::Allowed(int scheme, int target, InputSource s) {
    switch (s.kind) {
        case SourceKind::Key: return scheme == kKeyboard && s.code > 0 && s.code < 256;
        case SourceKind::Mouse: return scheme == kKeyboard && s.code < 5 && !IsAnalog(target);
        case SourceKind::Button:
            if (scheme == kKeyboard || s.code >= ButtonCount) return false;
            if (scheme == kXbox && (s.code == Home || s.code == Capture || s.code >= GripLeft)) return false;
            if (scheme == kSwitchPro && s.code >= GripLeft) return false;
            return true;
        case SourceKind::AxisPositive: return scheme != kKeyboard && s.code <= RightTrigger;
        case SourceKind::AxisNegative: return scheme != kKeyboard && s.code <= RightY;
        default: return false;
    }
}

Bindings CS::AddBinding(const Bindings& bindings, InputSource source) {
    if (std::find(bindings.begin(), bindings.end(), source) != bindings.end()) return bindings;
    Bindings list = bindings;
    if (int(list.size()) >= kSlots) list.pop_back();
    list.push_back(source);
    return list;
}

ControlsSettings CS::Load() {
    ControlsSettings settings;
    bool failed = false;
    auto lines = ReadLinesOrEmpty(RuntimeSettingsPath(), failed);
    if (failed) return settings;
    int version = 0;
    for (const auto& line : lines) {
        size_t eq = line.find(L'=');
        if (eq == wstring::npos || eq == 0) continue;
        wstring key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (key == L"controls_version") {
            long long v;
            version = ParseInt(value, v) ? int(v) : 0;
            continue;
        }
        settings.Apply(key, value);
    }
    bool migrated = !lines.empty() && version < kControlsVersion;
    if (migrated) settings.MigrateLegacy(version);
    // A migrated file is written whole, so it never mixes keys of two controls versions.
    if (!migrated) settings.read_ = MakeSnapshot(settings.Lines());
    return settings;
}

void CS::Apply(const wstring& key, const wstring& value) {
    if (key == L"input_mode") {
        wstring v = ToLower(value);
        if (v == L"auto") InputMode = kModeAuto;
        else if (v == L"controller" || v == L"xinput" || v == L"gamepad" || v == L"switch" || v == L"switch_pro") InputMode = kModeController;
        else if (v.find(L"wiimote") != wstring::npos || v.find(L"wii_remote") != wstring::npos || v.find(L"wii-remote") != wstring::npos)
            InputMode = kModeRealWiimote;
        else if (StartsWith(v, L"keyboard") || v == L"kbm" || v == L"mouse") InputMode = kModeKeyboardMouse;
        return;
    }
    bool bind = StartsWith(key, L"bind."), tune = StartsWith(key, L"tune.");
    if (!bind && !tune) return;
    wstring rest = key.substr(5);
    size_t dot = rest.find(L'.');
    if (dot == wstring::npos) return;
    auto schemeIt = std::find(kSchemeIds.begin(), kSchemeIds.end(), rest.substr(0, dot));
    wstring name = rest.substr(dot + 1);
    if (schemeIt == kSchemeIds.end()) return;
    int scheme = int(schemeIt - kSchemeIds.begin());
    if (bind) {
        auto targetIt = std::find(kTargetIds.begin(), kTargetIds.end(), name);
        if (targetIt == kTargetIds.end()) return;
        int target = int(targetIt - kTargetIds.begin());
        Bindings list;
        if (!IEquals(value, L"None") && !value.empty()) {
            for (const auto& token : Split(value, L',')) {
                if (Trim(token).empty()) continue;
                InputSource source;
                if (!TryParseSource(scheme, token, source)) return;
                if (Allowed(scheme, target, source)) list = AddBinding(list, source);
            }
        }
        Table[scheme][target] = list;
        return;
    }
    SchemeTuning& t = Tuning[scheme];
    float f = 0;
    bool parsed = ParseFloat(value, f) && std::isfinite(f);
    wstring lower = ToLower(value);
    if (name == L"stick_deadzone") { if (parsed) t.StickDeadzone = std::max(0.0f, std::min(0.9f, f)); }
    else if (name == L"pointer_deadzone") { if (parsed) t.PointerDeadzone = std::max(0.0f, std::min(0.9f, f)); }
    else if (name == L"pointer_speed") { if (parsed) t.PointerSpeed = std::max(0.1f, std::min(10.0f, f)); }
    else if (name == L"invert_pointer_y") t.InvertPointerY = value == L"1" || lower == L"true";
    else if (name == L"gyro_pointer") t.GyroPointer = value == L"1" || lower == L"true";
    else if (name == L"gyro_sensitivity") { if (parsed) t.GyroSensitivity = std::max(0.2f, std::min(5.0f, f)); }
    else if (name == L"pointer_hide_seconds") { if (parsed) t.PointerHideSeconds = std::max(0.0f, std::min(60.0f, f)); }
    else if (name == L"pointer_hide_radius") { if (parsed) t.PointerHideRadius = std::max(0.0f, std::min(0.5f, f)); }
    else if (name == L"shake_sensitivity") { if (parsed) t.ShakeSensitivity = std::max(1, std::min(5, int(std::nearbyint(f)))); }
    else if (name == L"motion") {
        auto m = std::find(kMotionNames.begin(), kMotionNames.end(), lower);
        if (m != kMotionNames.end()) t.Motion = int(m - kMotionNames.begin());
    } else if (name == L"vibration") {
        wstring v = ToLower(Trim(value));
        if (v == L"1" || v == L"true" || v == L"on") t.Vibration = true;
        else if (v == L"0" || v == L"false" || v == L"off") t.Vibration = false;
    } else if (name == L"rumble") {
        // 0 was "off" before controls version 5.
        if (parsed) {
            if (f <= 0.0f) t.Vibration = false;
            else t.Rumble = SnapRumble(f);
        }
    }
}

KeyValues CS::Lines() const {
    KeyValues lines;
    if (InputMode != kModeRealWiimote)
        lines.emplace_back(L"input_mode", InputMode == kModeController ? L"controller" : InputMode == kModeKeyboardMouse ? L"keyboard_mouse" : L"auto");
    lines.emplace_back(L"controls_version", Num(kControlsVersion));
    for (int s = 0; s < kSchemeCount; s++) {
        wstring p = kSchemeIds[s] + L".";
        for (int t = 0; t < kTargetCount; t++) lines.emplace_back(L"bind." + p + kTargetIds[t], FormatBindings(s, Table[s][t]));
        const SchemeTuning& tuning = Tuning[s];
        if (s != kKeyboard) {
            lines.emplace_back(L"tune." + p + L"stick_deadzone", FloatR(tuning.StickDeadzone));
            lines.emplace_back(L"tune." + p + L"pointer_deadzone", FloatR(tuning.PointerDeadzone));
            lines.emplace_back(L"tune." + p + L"pointer_hide_seconds", FloatR(tuning.PointerHideSeconds));
            lines.emplace_back(L"tune." + p + L"pointer_hide_radius", FloatR(tuning.PointerHideRadius));
            lines.emplace_back(L"tune." + p + L"vibration", tuning.Vibration ? L"1" : L"0");
            lines.emplace_back(L"tune." + p + L"rumble", Num(SnapRumble(float(tuning.Rumble))));
        }
        lines.emplace_back(L"tune." + p + L"pointer_speed", FloatR(tuning.PointerSpeed));
        lines.emplace_back(L"tune." + p + L"invert_pointer_y", tuning.InvertPointerY ? L"1" : L"0");
        if (HasMotion(s)) {
            lines.emplace_back(L"tune." + p + L"motion", kMotionNames[size_t(tuning.Motion)]);
            lines.emplace_back(L"tune." + p + L"shake_sensitivity", Num(tuning.ShakeSensitivity));
            lines.emplace_back(L"tune." + p + L"gyro_pointer", tuning.GyroPointer ? L"1" : L"0");
            lines.emplace_back(L"tune." + p + L"gyro_sensitivity", FloatR(tuning.GyroSensitivity));
        }
    }
    return lines;
}

void CS::Save() {
    KeyValues values = Lines();
    SaveSettingsFile(RuntimeSettingsPath(), values, read_ ? &*read_ : nullptr);
    read_ = MakeSnapshot(values);
}

// ---- controller capture ------------------------------------------------------------------------------------------

struct PadCaptureReader::HidPad {
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool pro2 = false;
    int inputLength = 64, outputLength = 64;
    std::thread reader;
    Sample state;
};

PadCaptureReader::PadCaptureReader(bool switchPads) {
    InitializeCriticalSection(&lock_);
    if (switchPads) OpenSwitchPads();
}

PadCaptureReader::~PadCaptureReader() {
    stopping_ = true;
    EnterCriticalSection(&lock_);
    for (auto& pad : pads_) CancelIoEx(pad->handle, nullptr);
    LeaveCriticalSection(&lock_);
    for (auto& pad : pads_) {
        if (pad->reader.joinable()) pad->reader.join();
        CloseHandle(pad->handle);
    }
    DeleteCriticalSection(&lock_);
}

static float Norm(short value) { return std::max(-1.0f, float(value) / 32767.0f); }

PadCaptureReader::Sample PadCaptureReader::Read(int scheme) {
    Sample sample;
    if (scheme == CS::kXbox) {
        for (DWORD i = 0; i < 4; i++) {
            XINPUT_STATE state{};
            if (XInputGetState(i, &state) != ERROR_SUCCESS) continue;
            sample.Connected = true;
            sample.Buttons = state.Gamepad.wButtons;
            sample.Axes[0] = Norm(state.Gamepad.sThumbLX);
            sample.Axes[1] = Norm(state.Gamepad.sThumbLY);
            sample.Axes[2] = Norm(state.Gamepad.sThumbRX);
            sample.Axes[3] = Norm(state.Gamepad.sThumbRY);
            sample.Axes[4] = state.Gamepad.bLeftTrigger / 255.0f;
            sample.Axes[5] = state.Gamepad.bRightTrigger / 255.0f;
            return sample;
        }
        return sample;
    }
    EnterCriticalSection(&lock_);
    for (auto& pad : pads_) {
        if (pad->pro2 != (scheme == CS::kSwitchPro2) || !pad->state.Connected) continue;
        sample = pad->state;
        break;
    }
    LeaveCriticalSection(&lock_);
    return sample;
}

bool PadCaptureReader::Detect(const Sample& baseline, const Sample& now, InputSource& source) {
    source = {};
    if (!now.Connected) return false;
    int pressed = now.Buttons & ~(baseline.Connected ? baseline.Buttons : 0);
    for (int i = 0; i < CS::ButtonCount; i++)
        if (pressed & (1 << i)) { source = {SourceKind::Button, i}; return true; }
    for (int i = 0; i < 6; i++) {
        float before = baseline.Connected ? baseline.Axes[i] : 0.0f, value = now.Axes[i];
        if (i >= 4) {
            if (value >= 0.5f && before < 0.5f) { source = {SourceKind::AxisPositive, i}; return true; }
            continue;
        }
        if (value >= 0.6f && before < 0.6f) { source = {SourceKind::AxisPositive, i}; return true; }
        if (value <= -0.6f && before > -0.6f) { source = {SourceKind::AxisNegative, i}; return true; }
    }
    return false;
}

void PadCaptureReader::OpenSwitchPads() {
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA data{sizeof(data)};
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, index, &data); index++) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &required, nullptr);
        if (required == 0) continue;
        std::vector<uint8_t> buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &data, detail, required, &required, nullptr)) continue;
        wstring path = detail->DevicePath;
        if (IFind(path, L"057e") == wstring::npos) continue;
        TryOpen(path);
    }
    SetupDiDestroyDeviceInfoList(set);
}

static int U12Low(const uint8_t* d, int i) { return d[i] | ((d[i + 1] & 0x0F) << 8); }
static int U12High(const uint8_t* d, int i) { return (d[i] >> 4) | (d[i + 1] << 4); }
static float Stick(int raw) { return std::max(-1.0f, std::min(1.0f, float(raw - 2048) / 1200.0f)); }

// Same bit assignments as switch_controller.cpp.
static void Decode(const uint8_t* r, int size, PadCaptureReader::Sample& s) {
    int b = 0;
    if (size >= 13 && (r[0] == 0x30 || r[0] == 0x21)) {
        uint8_t right = r[3], shared = r[4], left = r[5];
        if (right & 0x01) b |= 1 << CS::West;
        if (right & 0x02) b |= 1 << CS::North;
        if (right & 0x04) b |= 1 << CS::South;
        if (right & 0x08) b |= 1 << CS::East;
        if (right & 0x40) b |= 1 << CS::RightShoulder;
        if (shared & 0x01) b |= 1 << CS::Back;
        if (shared & 0x02) b |= 1 << CS::Start;
        if (shared & 0x04) b |= 1 << CS::RightStick;
        if (shared & 0x08) b |= 1 << CS::LeftStick;
        if (shared & 0x10) b |= 1 << CS::Home;
        if (shared & 0x20) b |= 1 << CS::Capture;
        if (left & 0x01) b |= 1 << CS::DpadDown;
        if (left & 0x02) b |= 1 << CS::DpadUp;
        if (left & 0x04) b |= 1 << CS::DpadRight;
        if (left & 0x08) b |= 1 << CS::DpadLeft;
        if (left & 0x40) b |= 1 << CS::LeftShoulder;
        s.Axes[0] = Stick(U12Low(r, 6)); s.Axes[1] = Stick(U12High(r, 7));
        s.Axes[2] = Stick(U12Low(r, 9)); s.Axes[3] = Stick(U12High(r, 10));
        s.Axes[4] = (left & 0x80) ? 1.0f : 0.0f; s.Axes[5] = (right & 0x80) ? 1.0f : 0.0f;
    } else if (size >= 12 && r[0] == 0x3F) {
        uint8_t b0 = r[1], b1 = r[2];
        const int map0[] = {CS::South, CS::East, CS::West, CS::North, CS::LeftShoulder, CS::RightShoulder};
        for (int i = 0; i < 6; i++) if (b0 & (1 << i)) b |= 1 << map0[i];
        const int map1[] = {CS::Back, CS::Start, CS::LeftStick, CS::RightStick, CS::Home, CS::Capture};
        for (int i = 0; i < 6; i++) if (b1 & (1 << i)) b |= 1 << map1[i];
        const int hat[] = {0x1, 0x9, 0x8, 0xA, 0x2, 0x6, 0x4, 0x5};
        if (r[3] < 8) b |= hat[r[3]];
        for (int i = 0; i < 4; i++) {
            float v = std::max(-1.0f, std::min(1.0f, float((r[4 + i * 2] | (r[5 + i * 2] << 8)) - 0x8000) / 20000.0f));
            s.Axes[i] = (i % 2) == 1 ? -v : v;
        }
        s.Axes[4] = (b0 & 0x40) ? 1.0f : 0.0f; s.Axes[5] = (b0 & 0x80) ? 1.0f : 0.0f;
    } else if (size >= 64 && r[0] == 0x05) {
        uint8_t b5 = r[5], b6 = r[6], b7 = r[7], b8 = r[8];
        if (b5 & 0x01) b |= 1 << CS::West;
        if (b5 & 0x02) b |= 1 << CS::North;
        if (b5 & 0x04) b |= 1 << CS::South;
        if (b5 & 0x08) b |= 1 << CS::East;
        if (b5 & 0x40) b |= 1 << CS::RightShoulder;
        if (b6 & 0x01) b |= 1 << CS::Back;
        if (b6 & 0x02) b |= 1 << CS::Start;
        if (b6 & 0x04) b |= 1 << CS::RightStick;
        if (b6 & 0x08) b |= 1 << CS::LeftStick;
        if (b6 & 0x10) b |= 1 << CS::Home;
        if (b6 & 0x20) b |= 1 << CS::Capture;
        if (b6 & 0x40) b |= 1 << CS::Chat;
        if (b7 & 0x01) b |= 1 << CS::DpadDown;
        if (b7 & 0x02) b |= 1 << CS::DpadUp;
        if (b7 & 0x04) b |= 1 << CS::DpadRight;
        if (b7 & 0x08) b |= 1 << CS::DpadLeft;
        if (b7 & 0x40) b |= 1 << CS::LeftShoulder;
        if (b8 & 0x01) b |= 1 << CS::GripRight;
        if (b8 & 0x02) b |= 1 << CS::GripLeft;
        s.Axes[0] = Stick(U12Low(r, 11)); s.Axes[1] = Stick(U12High(r, 12));
        s.Axes[2] = Stick(U12Low(r, 14)); s.Axes[3] = Stick(U12High(r, 15));
        s.Axes[4] = (b7 & 0x80) ? 1.0f : 0.0f; s.Axes[5] = (b5 & 0x80) ? 1.0f : 0.0f;
    } else {
        return;
    }
    s.Buttons = b;
    s.Connected = true;
}

void PadCaptureReader::TryOpen(const wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    HIDD_ATTRIBUTES attributes{sizeof(attributes)};
    if (!HidD_GetAttributes(handle, &attributes) || attributes.VendorID != 0x057e ||
        (attributes.ProductID != 0x2009 && attributes.ProductID != 0x2069)) {
        CloseHandle(handle);
        return;
    }
    auto pad = std::make_unique<HidPad>();
    pad->handle = handle;
    pad->pro2 = attributes.ProductID == 0x2069;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(handle, &preparsed)) {
        HIDP_CAPS caps{};
        HidP_GetCaps(preparsed, &caps);
        HidD_FreePreparsedData(preparsed);
        pad->inputLength = std::max(64, int(caps.InputReportByteLength));
        pad->outputLength = std::max(2, int(caps.OutputReportByteLength));
    }
    auto write = [&](std::initializer_list<uint8_t> data) {
        std::vector<uint8_t> buffer(size_t(pad->outputLength), 0);
        std::copy(data.begin(), data.begin() + std::min(data.size(), buffer.size()), buffer.begin());
        DWORD written = 0;
        WriteFile(pad->handle, buffer.data(), DWORD(buffer.size()), &written, nullptr);
        Sleep(20);
    };
    if (!pad->pro2) {
        bool bluetooth = IFind(path, L"{00001124-0000-1000-8000-00805f9b34fb}") != wstring::npos;
        if (!bluetooth) { write({0x80, 0x02}); write({0x80, 0x04}); }
        // Subcommand 0x03: full report mode 0x30 (neutral rumble).
        write({0x01, 0x00, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x03, 0x30});
    }
    HidPad* raw = pad.get();
    EnterCriticalSection(&lock_);
    pads_.push_back(std::move(pad));
    LeaveCriticalSection(&lock_);
    raw->reader = std::thread([this, raw] {
        std::vector<uint8_t> buffer(size_t(raw->inputLength));
        while (!stopping_) {
            DWORD read = 0;
            if (!ReadFile(raw->handle, buffer.data(), DWORD(buffer.size()), &read, nullptr)) break;
            EnterCriticalSection(&lock_);
            Decode(buffer.data(), int(read), raw->state);
            LeaveCriticalSection(&lock_);
        }
    });
}

// ---- monitors ----------------------------------------------------------------------------------------------------

wstring MonitorEntry::Label(int index) const {
    wstring name = Name.empty() ? L"Display" : Name;
    return Num(index + 1) + L": " + name + L" (" + Num(Width) + L" × " + Num(Height) + L")" + (Primary ? L", main" : L"");
}

std::vector<Size> MonitorEntry::Resolutions() const {
    std::vector<Size> sizes;
    for (const auto& mode : Modes) {
        Size size{mode.Width, mode.Height};
        if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) sizes.push_back(size);
    }
    return sizes;
}

static std::map<wstring, wstring> MonitorNames() {
    std::map<wstring, wstring> names;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return names;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return names;
    for (UINT32 i = 0; i < pathCount; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS && DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
            target.monitorFriendlyDeviceName[0])
            names[ToLower(source.viewGdiDeviceName)] = target.monitorFriendlyDeviceName;
    }
    return names;
}

static BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        MonitorEntry entry;
        entry.Device = info.szDevice;
        entry.Primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        entry.Bounds = info.rcMonitor;
        entry.WorkArea = info.rcWork;
        entry.Width = info.rcMonitor.right - info.rcMonitor.left;
        entry.Height = info.rcMonitor.bottom - info.rcMonitor.top;
        reinterpret_cast<std::vector<MonitorEntry>*>(data)->push_back(entry);
    }
    return TRUE;
}

std::vector<MonitorEntry> EnumerateMonitors() {
    auto names = MonitorNames();
    std::vector<MonitorEntry> list;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&list));
    for (auto& entry : list) {
        auto name = names.find(ToLower(entry.Device));
        if (name != names.end()) entry.Name = name->second;
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (EnumDisplaySettingsExW(entry.Device.c_str(), ENUM_CURRENT_SETTINGS, &current, 0)) {
            entry.Width = int(current.dmPelsWidth);
            entry.Height = int(current.dmPelsHeight);
            entry.Refresh = std::max(1, int(current.dmDisplayFrequency));
        }
        for (DWORD index = 0;; index++) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsExW(entry.Device.c_str(), index, &mode, 0)) break;
            if (mode.dmBitsPerPel != 32 || (mode.dmDisplayFlags & DM_INTERLACED) || mode.dmPelsWidth < 640 || mode.dmPelsHeight < 480) continue;
            int refresh = std::max(1, int(mode.dmDisplayFrequency));
            bool known = std::any_of(entry.Modes.begin(), entry.Modes.end(), [&](const MonitorMode& m) {
                return m.Width == int(mode.dmPelsWidth) && m.Height == int(mode.dmPelsHeight) && m.Refresh == refresh;
            });
            if (!known) entry.Modes.push_back({int(mode.dmPelsWidth), int(mode.dmPelsHeight), refresh});
        }
        if (entry.Modes.empty()) entry.Modes.push_back({entry.Width, entry.Height, entry.Refresh});
        std::stable_sort(entry.Modes.begin(), entry.Modes.end(), [](const MonitorMode& a, const MonitorMode& b) {
            long long areaA = (long long)a.Width * a.Height, areaB = (long long)b.Width * b.Height;
            if (areaA != areaB) return areaA > areaB;
            if (a.Width != b.Width) return a.Width > b.Width;
            return a.Refresh > b.Refresh;
        });
    }
    std::stable_sort(list.begin(), list.end(), [](const MonitorEntry& a, const MonitorEntry& b) {
        if (a.Primary != b.Primary) return a.Primary;
        if (a.Bounds.left != b.Bounds.left) return a.Bounds.left < b.Bounds.left;
        return a.Bounds.top < b.Bounds.top;
    });
    return list;
}

const MonitorEntry* FindMonitor(const std::vector<MonitorEntry>& monitors, const wstring& device) {
    if (!device.empty())
        for (const auto& monitor : monitors)
            if (IEquals(monitor.Device, device)) return &monitor;
    for (const auto& monitor : monitors)
        if (monitor.Primary) return &monitor;
    return monitors.empty() ? nullptr : &monitors[0];
}

int ScaleCovering(int width, int height) {
    if (width <= 0 || height <= 0) return 16;
    double needed = std::max(width / 640.0, height / 456.0);
    return std::max(1, std::min(16, int(std::ceil(needed - 1e-6))));
}

}  // namespace nebula::launcher
