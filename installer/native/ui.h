// A small Win32 window toolkit for Nebula's three programs: fixed-layout forms in 96-DPI units scaled to the
// display, the common controls they use, events as callbacks, modal windows and calls from worker threads.
#pragma once

#include "common.h"

#include <commctrl.h>

namespace nebula::ui {

void Init();                                  // DPI awareness, visual styles, common controls, COM
int Px(int value);                            // 96-DPI units to pixels
HFONT Font(double points, int weight = FW_NORMAL, const wchar_t* face = L"Segoe UI");

using Handler = std::function<void()>;

class Form;

// A NumericUpDown: an edit box with arrows, any value from min to max, steps of `step`, `decimals` shown.
struct NumberBox {
    HWND edit = nullptr;
    HWND arrows = nullptr;
    double minimum = 0, maximum = 100, step = 1, value = 0;
    int decimals = 0;
    Handler changed;
    void Set(double v, bool notify = false);
    double Get();                             // parses what was typed
    void Enable(bool enabled);
};

class Form {
public:
    Form(const wstring& title, int width, int height, HFONT font, Form* owner = nullptr, bool minimizeBox = true);
    virtual ~Form();
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;

    HWND Handle() const { return hwnd_; }
    HFONT DefaultFont() const { return font_; }

    // Controls; x, y, width and height in 96-DPI units relative to `parent` (the form or a tab page).
    HWND Label(HWND parent, const wstring& text, int x, int y, int w, int h, DWORD style = 0);
    HWND Button(HWND parent, const wstring& text, int x, int y, int w, int h, Handler click = nullptr);
    HWND Check(HWND parent, const wstring& text, int x, int y, int w, int h, Handler changed = nullptr);
    HWND Radio(HWND parent, const wstring& text, int x, int y, int w, int h, bool firstInGroup, Handler changed = nullptr);
    HWND Combo(HWND parent, int x, int y, int w, Handler changed = nullptr);
    HWND Edit(HWND parent, const wstring& text, int x, int y, int w, int h, bool multiline = false, bool readOnly = false,
              Handler changed = nullptr);
    HWND Track(HWND parent, int x, int y, int w, int h, int minimum, int maximum, int tick, int smallStep, int largeStep, Handler changed);
    HWND Progress(HWND parent, int x, int y, int w, int h);
    HWND List(HWND parent, int x, int y, int w, int h);
    HWND Tabs(HWND parent, int x, int y, int w, int h);
    HWND Page(HWND tabs, const wstring& title);      // a tab page; controls go on it
    HWND Link(HWND parent, const wstring& text, int x, int y, int w, int h, Handler click);
    std::unique_ptr<NumberBox> Number(HWND parent, int x, int y, int w, int h, double minimum, double maximum, double step,
                                      int decimals);

    void OnClick(HWND control, Handler handler);     // also selection and text changes
    void OnDoubleClick(HWND list, Handler handler);
    void SetAccept(HWND button);                     // Enter
    void SetCancel(HWND button);                     // Esc
    void SetTextColor(HWND control, COLORREF color);
    void ResetTextColor(HWND control);
    void SetWhiteBackground(HWND control);

    void Show();
    void Close();                                    // as the close box: OnClosing may refuse
    void Destroy();
    bool Closed() const { return closed_; }
    // Runs until this window is gone, with its owner disabled meanwhile (ShowDialog). Returns the result set.
    int ShowModal();
    void SetResult(int result) { result_ = result; }
    int Result() const { return result_; }
    void SetEnabled(bool enabled);
    void SelectTab(HWND tabs, int index);
    void Activate();

    // From any thread: runs on the window's thread.
    void Post(Handler handler);
    void Invoke(const Handler& handler);              // waits
    void StartTimer(UINT id, UINT ms, Handler handler);
    void StopTimer(UINT id);

    Handler onShown;
    std::function<bool()> onClosing;                 // false keeps the window open
    Handler onClosed;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND Make(HWND parent, const wchar_t* cls, const wstring& text, DWORD style, int x, int y, int w, int h, DWORD exStyle = 0);
    int NextId();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    Form* owner_ = nullptr;
    bool closed_ = false;
    bool shown_ = false;
    int result_ = 0;
    int accept_ = 0, cancel_ = 0;
    std::map<int, Handler> handlers_;
    std::map<int, Handler> doubleClicks_;
    std::map<UINT, Handler> timers_;
    std::map<HWND, COLORREF> colors_;
    std::map<HWND, bool> whiteBackground_;
    std::map<HWND, NumberBox*> numbers_;
    struct TabSet { HWND tabs; std::vector<HWND> pages; };
    std::vector<TabSet> tabSets_;
};

#ifdef NEBULA_UI_SNAPSHOTS
// Test builds only (NEBULA_UI_SNAPSHOT_DIR): windows open off screen without taking focus, and are saved as pictures.
std::optional<wstring> SnapshotDirectory();
void Pump(int milliseconds);
void SaveSnapshot(HWND window, const wstring& path);
#endif

// The message loop of the main window; returns when it closes. A filter sees every message first (true: consumed).
int Run(Form& main);
void SetMessageFilter(std::function<bool(MSG&)> filter);

int Message(Form* owner, const wstring& text, const wstring& caption, UINT flags = MB_OK);
std::optional<wstring> OpenFileDialog(Form* owner, const wstring& title, const std::vector<std::pair<wstring, wstring>>& filters);
std::optional<wstring> FolderDialog(Form* owner, const wstring& title);

// Text and lists.
wstring GetText(HWND control);
void SetText(HWND control, const wstring& text);
void Enable(HWND control, bool enabled);
void Visible(HWND control, bool visible);
bool Checked(HWND control);
void SetChecked(HWND control, bool checked);
void ComboClear(HWND combo);
void ComboAdd(HWND combo, const wstring& text);
void ComboSetItem(HWND combo, int index, const wstring& text);
int ComboCount(HWND combo);
int ComboIndex(HWND combo);
void ComboSelect(HWND combo, int index);
int TrackValue(HWND track);
void SetTrackValue(HWND track, int value);
void SetProgress(HWND bar, int value, int maximum = 1000);
void SetMarquee(HWND bar, bool marquee);
void ListColumns(HWND list, const std::vector<std::pair<wstring, int>>& columns);
void ListAdd(HWND list, const std::vector<wstring>& cells);
void ListSetCell(HWND list, int row, int column, const wstring& text);
int ListSelected(HWND list);
void Move(HWND control, int x, int y, int w, int h);  // 96-DPI units
int TopOf(HWND control);                               // 96-DPI units, in its parent
void AppendText(HWND edit, const wstring& text);

}  // namespace nebula::ui
