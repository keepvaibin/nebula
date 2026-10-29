// What Nebula.exe and NebulaSettings.exe read and write: the runtime's settings files, monitors, controller capture,
// the saves archive and how the game is started.
#pragma once

#include "archive.h"

namespace nebula::launcher {

struct Size {
    int w = 0, h = 0;
    bool operator==(const Size& o) const { return w == o.w && h == o.h; }
};

using KeyValues = std::vector<std::pair<wstring, wstring>>;
using Snapshot = std::map<wstring, wstring>;

// config.ini and runtime_settings.ini: only keys whose values changed since they were read are written, every other
// line stays as the file has it now (the game may have saved meanwhile). A null `read` writes every key.
Snapshot MakeSnapshot(const KeyValues& values);
void SaveSettingsFile(const wstring& path, const KeyValues& values, const Snapshot* read);

wstring RuntimeSettingsPath();                // runtime_settings.ini

// ---- graphics (config.ini, render_config.cpp) --------------------------------------------------------------------

struct GraphicsSettings {
    static const std::vector<wstring> kDisplayModes, kAspects, kAntiAliasingModes, kAntiAliasingQualities, kUpscalers,
        kDlssQualities, kTextureFilters, kLightingModes;
    static std::vector<int> UpscalerChoices();
    static bool DlssAdapter();
    static bool PackagePrefersIntegratedGpu();
    static wstring UpscalerName(int value);
    static std::vector<int> FrameRates(int refreshHz);
    static std::vector<Size> AspectWindowSizes(int aspect, int maxWidth, int maxHeight);
    static int SnapWindowSize(const std::vector<Size>& sizes, Size want);
    static wstring ConfigPath();
    static GraphicsSettings Load();

    int DisplayMode = 1;
    wstring Monitor;
    int WindowWidth = 1280, WindowHeight = 720;
    int FullscreenWidth = 0, FullscreenHeight = 0, FullscreenRefresh = 0;
    int Aspect = 0;
    int EfbScale = 2;
    bool MatchOutput = true;
    bool VSync = false;
    int FrameRate = 60;
    int AntiAliasing = 0, Quality = 2, Upscaler = 0, Sharpness = 0, TextureFilter = 0;
    int TemporalQuality = 1;
    bool GeneratedMipmaps = false, PerPixelLighting = false, HudOutputResolution = false;

    void Save();

private:
    KeyValues Values() const;
    std::optional<Snapshot> read_;
};

// ---- audio and general (runtime_settings.ini) --------------------------------------------------------------------

struct AudioSettings {
    static const std::vector<wstring> kNames;
    int Percent[6] = {100, 100, 100, 100, 100, 100};
    static AudioSettings Load();
    void Save();

private:
    KeyValues Values() const;
    std::optional<Snapshot> read_;
};

struct GeneralSettings {
    static const std::vector<wstring> kTextSpeeds, kLanguages;
    int TextSpeed = 1;
    int Language = 0;
    static GeneralSettings Load();
    void Save();

private:
    KeyValues Values() const;
    std::optional<Snapshot> read_;
};

// ---- controls (runtime_settings.ini, input_bindings.cpp) ---------------------------------------------------------

enum class SourceKind { None = 0, Key = 1, Mouse = 2, Button = 3, AxisPositive = 4, AxisNegative = 5 };

struct InputSource {
    SourceKind kind = SourceKind::None;
    int code = 0;
    bool operator==(const InputSource& o) const { return kind == o.kind && code == o.code; }
    bool Empty() const { return kind == SourceKind::None; }
};
using Bindings = std::vector<InputSource>;

struct SchemeTuning {
    float StickDeadzone = 0, PointerDeadzone = 0, PointerSpeed = 1.6f;
    bool InvertPointerY = false, GyroPointer = false;
    float GyroSensitivity = 1.0f;
    float PointerHideSeconds = 0, PointerHideRadius = 0.12f;
    int Motion = 0;
    int ShakeSensitivity = 3;
    bool Vibration = true;
    int Rumble = 50;
};

struct ControlsSettings {
    static const int kSchemeCount = 4, kTargetCount = 25, kSlots = 4, kJumpBack = 24, kControlsVersion = 5;
    static const int kModeAuto = 0, kModeKeyboardMouse = 1, kModeController = 2, kModeRealWiimote = 3;
    static const int kKeyboard = 0, kXbox = 1, kSwitchPro = 2, kSwitchPro2 = 3;
    static const std::vector<wstring> kSchemeIds, kSchemeLabels, kTargetIds, kTargetLabels, kMotionNames, kMotionLabels, kRumbleLabels;
    static const std::vector<int> kRumbleSteps;
    // Unified pad bits (input_bindings.h pad::).
    static const int DpadUp = 0, DpadDown = 1, DpadLeft = 2, DpadRight = 3, Start = 4, Back = 5, LeftStick = 6, RightStick = 7,
                     LeftShoulder = 8, RightShoulder = 9, Home = 10, Capture = 11, South = 12, East = 13, West = 14, North = 15,
                     GripLeft = 16, GripRight = 17, Chat = 18, ButtonCount = 19;
    static const int LeftX = 0, LeftY = 1, RightX = 2, RightY = 3, LeftTrigger = 4, RightTrigger = 5;

    int InputMode = kModeAuto;
    std::vector<Bindings> Table[kSchemeCount];
    SchemeTuning Tuning[kSchemeCount];

    ControlsSettings();
    static bool IsAnalog(int target);
    static bool HasMotion(int scheme);
    static int SnapRumble(float percent);
    static std::vector<Bindings> DefaultTable(int scheme);
    static SchemeTuning DefaultTuning(int scheme);
    static wstring FormatSource(int scheme, InputSource source);
    static wstring FormatBindings(int scheme, const Bindings& bindings);
    static bool Allowed(int scheme, int target, InputSource source);
    static Bindings AddBinding(const Bindings& bindings, InputSource source);
    static ControlsSettings Load();
    void Save();

private:
    static std::vector<Bindings> LegacyTable(int scheme);
    static std::vector<Bindings> Version2Table(int scheme);
    static std::vector<Bindings> Version3Table(int scheme);
    static SchemeTuning LegacyTuning(int scheme);
    static wstring FormatKey(int vk);
    static bool TryParseSource(int scheme, wstring text, InputSource& source);
    void MigrateLegacy(int version);
    void Apply(const wstring& key, const wstring& value);
    KeyValues Lines() const;
    std::optional<Snapshot> read_;
};

// Controller state for binding capture: XInput, and Switch Pro / Pro Controller 2 HID reports read on threads.
class PadCaptureReader {
public:
    struct Sample {
        bool Connected = false;
        int Buttons = 0;
        float Axes[6] = {};
    };
    explicit PadCaptureReader(bool switchPads);
    ~PadCaptureReader();
    Sample Read(int scheme);
    static bool Detect(const Sample& baseline, const Sample& now, InputSource& source);

private:
    struct HidPad;
    void OpenSwitchPads();
    void TryOpen(const wstring& path);
    std::vector<std::unique_ptr<HidPad>> pads_;
    CRITICAL_SECTION lock_;
    std::atomic<bool> stopping_{false};
};

// ---- monitors ----------------------------------------------------------------------------------------------------

struct MonitorMode { int Width, Height, Refresh; };

struct MonitorEntry {
    wstring Device, Name;
    bool Primary = false;
    RECT Bounds{}, WorkArea{};
    int Width = 0, Height = 0, Refresh = 60;
    std::vector<MonitorMode> Modes;
    wstring Label(int index) const;
    std::vector<Size> Resolutions() const;
};

std::vector<MonitorEntry> EnumerateMonitors();
const MonitorEntry* FindMonitor(const std::vector<MonitorEntry>& monitors, const wstring& device);
int ScaleCovering(int width, int height);

// ---- saves -------------------------------------------------------------------------------------------------------

namespace saves {
// Writes Nebula-Saves-yyyyMMdd-HHmmss.zip into the folder and returns its path.
wstring Export(const wstring& folder);
// Replaces the saves with an exported zip, another zip of a saves folder, or a saves folder. The confirmation is
// asked before current saves are replaced; nullopt when declined.
std::optional<wstring> Import(const wstring& source, const std::function<bool()>& confirmReplace);
}  // namespace saves

// ---- the game ----------------------------------------------------------------------------------------------------

enum class RecordingMode { None = 0, Lightweight = 1, Detailed = 2 };

struct LaunchSession {
    wstring App;
    bool Portable = false;
    wstring Root;
    wstring Content;
    bool SetupComplete = false;
    RecordingMode Recording = RecordingMode::None;
    HANDLE RecordingThread = nullptr;     // keeps the program alive until a recorded game exits

    LaunchSession();
    wstring SettingsFile() const;
    void Save();
    // Starts the game; returns an error message instead of throwing.
    std::optional<wstring> TryLaunch(void* ownerWindow);

private:
    bool EndRunningGame(void* ownerWindow);
    void StartRecorded(Environment environment, const wstring& commandLine);
};

}  // namespace nebula::launcher
