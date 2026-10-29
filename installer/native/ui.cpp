#include "ui.h"

#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <cmath>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace nebula::ui {

namespace {
const wchar_t* const kFormClass = L"NebulaForm";
const wchar_t* const kPageClass = L"NebulaPage";
const UINT kPostMessage = WM_APP + 1;
const UINT kInvokeMessage = WM_APP + 2;
UINT g_dpi = 96;
int g_nextId = 1000;
std::function<bool(MSG&)> g_filter;

LRESULT CALLBACK PageProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* form = reinterpret_cast<Form*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_HSCROLL:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (form) return form->Handle(hwnd, message, wParam, lParam);
            break;
        case WM_ERASEBKGND: {
            RECT r;
            GetClientRect(hwnd, &r);
            FillRect(reinterpret_cast<HDC>(wParam), &r, GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}  // namespace

void Init() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = GetDpiForSystem();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS |
                                                        ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_LINK_CLASS};
    InitCommonControlsEx(&controls);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW form{sizeof(form)};
    form.lpfnWndProc = Form::WindowProc;
    form.hInstance = instance;
    form.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    form.hIconSm = form.hIcon;
    form.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    form.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    form.lpszClassName = kFormClass;
    RegisterClassExW(&form);
    WNDCLASSEXW page{sizeof(page)};
    page.lpfnWndProc = PageProc;
    page.hInstance = instance;
    page.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    page.lpszClassName = kPageClass;
    RegisterClassExW(&page);
}

int Px(int value) { return MulDiv(value, int(g_dpi), 96); }

HFONT Font(double points, int weight, const wchar_t* face) {
    LOGFONTW font{};
    font.lfHeight = -LONG(std::lround(points * g_dpi / 72.0));
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(font.lfFaceName, face);
    return CreateFontIndirectW(&font);
}

// ---- NumberBox ---------------------------------------------------------------------------------------------------

void NumberBox::Set(double v, bool notify) {
    double rounded = std::round(v * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
    value = std::max(minimum, std::min(maximum, rounded));
    SetText(edit, Fixed(value, decimals));
    if (notify && changed) changed();
}

double NumberBox::Get() {
    double typed;
    if (ParseDouble(GetText(edit), typed) && std::isfinite(typed)) {
        double before = value;
        Set(typed);
        if (value != before && changed) changed();
    } else {
        Set(value);
    }
    return value;
}

void NumberBox::Enable(bool enabled) {
    EnableWindow(edit, enabled);
    EnableWindow(arrows, enabled);
}

// ---- Form --------------------------------------------------------------------------------------------------------

Form::Form(const wstring& title, int width, int height, HFONT font, Form* owner, bool minimizeBox) : font_(font), owner_(owner) {
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | (minimizeBox ? WS_MINIMIZEBOX : 0);
    DWORD exStyle = WS_EX_CONTROLPARENT | (owner ? WS_EX_DLGMODALFRAME : 0);
    RECT rect{0, 0, Px(width), Px(height)};
    AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, g_dpi);
    int w = rect.right - rect.left, h = rect.bottom - rect.top;
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    int x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - w) / 2;
    int y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - h) / 2;
    hwnd_ = CreateWindowExW(exStyle, kFormClass, title.c_str(), style, x, std::max<int>(monitor.rcWork.top, y), w, h,
                            owner ? owner->Handle() : nullptr, nullptr, GetModuleHandleW(nullptr), this);
}

Form::~Form() {
    if (hwnd_ && IsWindow(hwnd_)) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

int Form::NextId() { return g_nextId++; }

HWND Form::Make(HWND parent, const wchar_t* cls, const wstring& text, DWORD style, int x, int y, int w, int h, DWORD exStyle) {
    int id = NextId();
    HWND control = CreateWindowExW(exStyle, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, Px(x), Px(y), Px(w), Px(h),
                                   parent ? parent : hwnd_, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return control;
}

HWND Form::Label(HWND parent, const wstring& text, int x, int y, int w, int h, DWORD style) {
    return Make(parent, WC_STATICW, text, SS_LEFT | SS_NOPREFIX | style, x, y, w, h);
}

HWND Form::Button(HWND parent, const wstring& text, int x, int y, int w, int h, Handler click) {
    HWND button = Make(parent, WC_BUTTONW, text, BS_PUSHBUTTON | WS_TABSTOP, x, y, w, h);
    if (click) OnClick(button, std::move(click));
    return button;
}

HWND Form::Check(HWND parent, const wstring& text, int x, int y, int w, int h, Handler changed) {
    HWND check = Make(parent, WC_BUTTONW, text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, h);
    if (changed) OnClick(check, std::move(changed));
    return check;
}

HWND Form::Radio(HWND parent, const wstring& text, int x, int y, int w, int h, bool firstInGroup, Handler changed) {
    HWND radio = Make(parent, WC_BUTTONW, text, BS_AUTORADIOBUTTON | (firstInGroup ? WS_GROUP | WS_TABSTOP : 0), x, y, w, h);
    if (changed) OnClick(radio, std::move(changed));
    return radio;
}

HWND Form::Combo(HWND parent, int x, int y, int w, Handler changed) {
    HWND combo = Make(parent, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 400);
    if (changed) OnClick(combo, std::move(changed));
    return combo;
}

HWND Form::Edit(HWND parent, const wstring& text, int x, int y, int w, int h, bool multiline, bool readOnly, Handler changed) {
    DWORD style = WS_TABSTOP | ES_AUTOHSCROLL;
    if (multiline) style = WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
    if (readOnly) style |= ES_READONLY;
    HWND edit = Make(parent, WC_EDITW, text, style, x, y, w, h, WS_EX_CLIENTEDGE);
    if (changed) OnClick(edit, std::move(changed));
    return edit;
}

HWND Form::Track(HWND parent, int x, int y, int w, int h, int minimum, int maximum, int tick, int smallStep, int largeStep, Handler changed) {
    HWND track = Make(parent, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, x, y, w, h);
    SendMessageW(track, TBM_SETRANGEMIN, FALSE, minimum);
    SendMessageW(track, TBM_SETRANGEMAX, TRUE, maximum);
    SendMessageW(track, TBM_SETTICFREQ, tick, 0);
    SendMessageW(track, TBM_SETLINESIZE, 0, smallStep);
    SendMessageW(track, TBM_SETPAGESIZE, 0, largeStep);
    if (changed) OnClick(track, std::move(changed));
    return track;
}

HWND Form::Progress(HWND parent, int x, int y, int w, int h) {
    HWND bar = Make(parent, PROGRESS_CLASSW, L"", 0, x, y, w, h);
    SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
    return bar;
}

HWND Form::List(HWND parent, int x, int y, int w, int h) {
    HWND list = Make(parent, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP, x, y, w,
                     h, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(list, L"Explorer", nullptr);
    HWND header = ListView_GetHeader(list);
    LONG_PTR style = GetWindowLongPtrW(header, GWL_STYLE);
    SetWindowLongPtrW(header, GWL_STYLE, style & ~HDS_BUTTONS);
    return list;
}

HWND Form::Tabs(HWND parent, int x, int y, int w, int h) {
    HWND tabs = Make(parent, WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, x, y, w, h);
    tabSets_.push_back({tabs, {}});
    return tabs;
}

HWND Form::Page(HWND tabs, const wstring& title) {
    TabSet* set = nullptr;
    for (auto& candidate : tabSets_)
        if (candidate.tabs == tabs) set = &candidate;
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(title.c_str());
    int index = int(set->pages.size());
    TabCtrl_InsertItem(tabs, index, &item);
    RECT area;
    GetClientRect(tabs, &area);
    TabCtrl_AdjustRect(tabs, FALSE, &area);
    MapWindowPoints(tabs, GetParent(tabs), reinterpret_cast<POINT*>(&area), 2);
    HWND page = CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"", WS_CHILD | WS_CLIPCHILDREN | (index == 0 ? WS_VISIBLE : 0),
                                area.left, area.top, area.right - area.left, area.bottom - area.top, GetParent(tabs), nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(page, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowPos(page, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    set->pages.push_back(page);
    return page;
}

HWND Form::Link(HWND parent, const wstring& text, int x, int y, int w, int h, Handler click) {
    HWND link = Make(parent, WC_LINK, L"<a>" + text + L"</a>", WS_TABSTOP, x, y, w, h);
    OnClick(link, std::move(click));
    return link;
}

std::unique_ptr<NumberBox> Form::Number(HWND parent, int x, int y, int w, int h, double minimum, double maximum, double step,
                                        int decimals) {
    auto box = std::make_unique<NumberBox>();
    box->minimum = minimum;
    box->maximum = maximum;
    box->step = step;
    box->decimals = decimals;
    box->value = minimum;
    int arrowWidth = 18;
    box->edit = Make(parent, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL, x, y, w - arrowWidth, h, WS_EX_CLIENTEDGE);
    box->arrows = Make(parent, UPDOWN_CLASSW, L"", UDS_ARROWKEYS | UDS_HOTTRACK, x + w - arrowWidth, y, arrowWidth, h);
    SendMessageW(box->arrows, UDM_SETRANGE32, 0, 100);
    SendMessageW(box->arrows, UDM_SETPOS32, 0, 50);
    box->Set(minimum);
    int id = GetDlgCtrlID(box->arrows);
    NumberBox* raw = box.get();
    numbers_[box->arrows] = raw;
    handlers_[GetDlgCtrlID(box->edit)] = [raw] {};
    (void)id;
    return box;
}

void Form::OnClick(HWND control, Handler handler) { handlers_[GetDlgCtrlID(control)] = std::move(handler); }
void Form::OnDoubleClick(HWND list, Handler handler) { doubleClicks_[GetDlgCtrlID(list)] = std::move(handler); }
void Form::SetAccept(HWND button) {
    accept_ = GetDlgCtrlID(button);
    SendMessageW(button, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
}
void Form::SetCancel(HWND button) { cancel_ = GetDlgCtrlID(button); }
void Form::SetTextColor(HWND control, COLORREF color) {
    colors_[control] = color;
    InvalidateRect(control, nullptr, TRUE);
}
void Form::ResetTextColor(HWND control) {
    colors_.erase(control);
    InvalidateRect(control, nullptr, TRUE);
}
void Form::SetWhiteBackground(HWND control) { whiteBackground_[control] = true; }

void Form::Show() {
#ifdef NEBULA_UI_SNAPSHOTS
    if (SnapshotDirectory()) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        SetWindowPos(hwnd_, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd_);
        if (!shown_) {
            shown_ = true;
            Post([this] { if (onShown) onShown(); });
        }
        return;
    }
#endif
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    if (!shown_) {
        shown_ = true;
        Post([this] { if (onShown) onShown(); });
    }
}

void Form::Close() {
    if (hwnd_ && !closed_) SendMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void Form::Destroy() {
    if (hwnd_ && !closed_) DestroyWindow(hwnd_);
}

int Form::ShowModal() {
    if (owner_) EnableWindow(owner_->Handle(), FALSE);
    Show();
    MSG message;
    bool quit = false;
    int quitCode = 0;
    while (!closed_) {
        if (GetMessageW(&message, nullptr, 0, 0) <= 0) {
            quit = true;
            quitCode = int(message.wParam);
            break;
        }
        if (g_filter && g_filter(message)) continue;
        HWND root = GetAncestor(message.hwnd, GA_ROOT);
        if (root && IsDialogMessageW(root, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (owner_) {
        EnableWindow(owner_->Handle(), TRUE);
        SetActiveWindow(owner_->Handle());
    }
    if (quit) PostQuitMessage(quitCode);
    return result_;
}

void Form::SetEnabled(bool enabled) { EnableWindow(hwnd_, enabled); }

void Form::SelectTab(HWND tabs, int index) {
    for (auto& set : tabSets_) {
        if (set.tabs != tabs) continue;
        TabCtrl_SetCurSel(tabs, index);
        for (size_t i = 0; i < set.pages.size(); i++) ShowWindow(set.pages[i], int(i) == index ? SW_SHOW : SW_HIDE);
    }
}

void Form::Activate() {
    SetForegroundWindow(hwnd_);
    SetActiveWindow(hwnd_);
}

void Form::Post(Handler handler) {
    if (!hwnd_ || closed_) return;
    auto* heap = new Handler(std::move(handler));
    if (!PostMessageW(hwnd_, kPostMessage, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
}

void Form::Invoke(const Handler& handler) {
    if (!hwnd_ || closed_) return;
    SendMessageW(hwnd_, kInvokeMessage, 0, reinterpret_cast<LPARAM>(&handler));
}

void Form::StartTimer(UINT id, UINT ms, Handler handler) {
    timers_[id] = std::move(handler);
    SetTimer(hwnd_, id, ms, nullptr);
}

void Form::StopTimer(UINT id) { KillTimer(hwnd_, id); }

LRESULT CALLBACK Form::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* form = reinterpret_cast<Form*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (form) return form->Handle(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Form::Handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            int id = LOWORD(wParam), code = HIWORD(wParam);
            if (id == IDCANCEL && lParam == 0) {
                if (cancel_) id = cancel_;
                else { Close(); return 0; }
                code = BN_CLICKED;
            }
            if (id == IDOK && lParam == 0) {
                if (!accept_) return 0;
                id = accept_;
                code = BN_CLICKED;
            }
            HWND control = lParam ? reinterpret_cast<HWND>(lParam) : GetDlgItem(hwnd, id);
            wchar_t cls[32] = L"";
            if (control) GetClassNameW(control, cls, 32);
            bool combo = _wcsicmp(cls, WC_COMBOBOXW) == 0;
            bool edit = _wcsicmp(cls, WC_EDITW) == 0;
            if (edit && code == EN_KILLFOCUS) {
                for (auto& pair : numbers_)
                    if (GetDlgCtrlID(pair.second->edit) == id) pair.second->Get();
                return 0;
            }
            bool fire = combo ? code == CBN_SELCHANGE : edit ? code == EN_CHANGE : code == BN_CLICKED;
            if (fire) {
                auto it = handlers_.find(id);
                if (it != handlers_.end() && it->second) {
                    Handler copy = it->second;
                    copy();
                }
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->code == NM_CLICK || header->code == NM_RETURN) {
                wchar_t cls[32] = L"";
                GetClassNameW(header->hwndFrom, cls, 32);
                if (_wcsicmp(cls, WC_LINK) == 0) {
                    auto it = handlers_.find(int(header->idFrom));
                    if (it != handlers_.end() && it->second) { Handler copy = it->second; copy(); }
                    return 0;
                }
            }
            if (header->code == NM_DBLCLK) {
                auto it = doubleClicks_.find(int(header->idFrom));
                if (it != doubleClicks_.end() && it->second) { Handler copy = it->second; copy(); }
                return 0;
            }
            if (header->code == UDN_DELTAPOS) {
                auto it = numbers_.find(header->hwndFrom);
                if (it != numbers_.end()) {
                    auto* change = reinterpret_cast<NMUPDOWN*>(lParam);
                    NumberBox* box = it->second;
                    box->Get();
                    double before = box->value;
                    box->Set(box->value + (change->iDelta > 0 ? box->step : -box->step));
                    if (box->value != before && box->changed) box->changed();
                    return 1;
                }
            }
            if (header->code == TCN_SELCHANGE) {
                for (auto& set : tabSets_) {
                    if (set.tabs != header->hwndFrom) continue;
                    int selected = TabCtrl_GetCurSel(set.tabs);
                    for (size_t i = 0; i < set.pages.size(); i++) ShowWindow(set.pages[i], int(i) == selected ? SW_SHOW : SW_HIDE);
                }
                return 0;
            }
            break;
        }
        case WM_HSCROLL: {
            HWND track = reinterpret_cast<HWND>(lParam);
            if (track) {
                auto it = handlers_.find(GetDlgCtrlID(track));
                if (it != handlers_.end() && it->second) { Handler copy = it->second; copy(); }
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            bool onPage = hwnd != hwnd_ || whiteBackground_.count(control);
            auto color = colors_.find(control);
            if (color != colors_.end()) ::SetTextColor(dc, color->second);
            else if (!IsWindowEnabled(control)) ::SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            int background = onPage ? COLOR_WINDOW : COLOR_BTNFACE;
            SetBkColor(dc, GetSysColor(background));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(background));
        }
        case DM_GETDEFID:
            if (accept_) return MAKELRESULT(accept_, DC_HASDEFID);
            return 0;
        case WM_TIMER: {
            auto it = timers_.find(UINT(wParam));
            if (it != timers_.end() && it->second) { Handler copy = it->second; copy(); }
            return 0;
        }
        case kPostMessage: {
            std::unique_ptr<Handler> handler(reinterpret_cast<Handler*>(lParam));
            if (*handler) (*handler)();
            return 0;
        }
        case kInvokeMessage: {
            auto* handler = reinterpret_cast<const Handler*>(lParam);
            if (*handler) (*handler)();
            return 0;
        }
        case WM_CLOSE:
            if (onClosing && !onClosing()) return 0;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            closed_ = true;
            if (onClosed) onClosed();
            return 0;
        case WM_NCDESTROY:
            hwnd_ = nullptr;
            closed_ = true;
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

#ifdef NEBULA_UI_SNAPSHOTS
std::optional<wstring> SnapshotDirectory() {
    auto directory = GetEnv(L"NEBULA_UI_SNAPSHOT_DIR");
    if (!directory || directory->empty()) return std::nullopt;
    return directory;
}

void Pump(int milliseconds) {
    ULONGLONG until = GetTickCount64() + ULONGLONG(milliseconds);
    MSG message;
    while (GetTickCount64() < until) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

void SaveSnapshot(HWND window, const wstring& path) {
    RECT rect;
    GetWindowRect(window, &rect);
    int w = rect.right - rect.left, h = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    PrintWindow(window, dc, PW_RENDERFULLCONTENT);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + DWORD(w * h * 4);
    std::vector<uint8_t> out(file.bfSize);
    memcpy(out.data(), &file, sizeof(file));
    memcpy(out.data() + sizeof(file), &info.bmiHeader, sizeof(BITMAPINFOHEADER));
    memcpy(out.data() + file.bfOffBits, bits, size_t(w) * h * 4);
    WriteAllBytes(path, out.data(), out.size());
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
#endif

// ---- loop and dialogs --------------------------------------------------------------------------------------------

void SetMessageFilter(std::function<bool(MSG&)> filter) { g_filter = std::move(filter); }

int Run(Form& main) {
    main.Show();
    MSG message;
    while (!main.Closed()) {
        int got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) return int(message.wParam);
        if (g_filter && g_filter(message)) continue;
        HWND root = GetAncestor(message.hwnd, GA_ROOT);
        if (root && IsDialogMessageW(root, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // Messages already posted (a worker's last update) are dropped with the window.
    return 0;
}

int Message(Form* owner, const wstring& text, const wstring& caption, UINT flags) {
    return MessageBoxW(owner && owner->Handle() ? owner->Handle() : nullptr, text.c_str(), caption.c_str(), flags);
}

std::optional<wstring> OpenFileDialog(Form* owner, const wstring& title, const std::vector<std::pair<wstring, wstring>>& filters) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const auto& filter : filters) specs.push_back({filter.first.c_str(), filter.second.c_str()});
    if (!specs.empty()) dialog->SetFileTypes(UINT(specs.size()), specs.data());
    dialog->SetTitle(title.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    std::optional<wstring> result;
    if (SUCCEEDED(dialog->Show(owner ? owner->Handle() : nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = wstring(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::optional<wstring> FolderDialog(Form* owner, const wstring& title) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title.c_str());
    std::optional<wstring> result;
    if (SUCCEEDED(dialog->Show(owner ? owner->Handle() : nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = wstring(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

// ---- control helpers ---------------------------------------------------------------------------------------------

wstring GetText(HWND control) {
    int length = GetWindowTextLengthW(control);
    wstring text(size_t(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(size_t(length));
    return text;
}

void SetText(HWND control, const wstring& text) { SetWindowTextW(control, text.c_str()); }
void Enable(HWND control, bool enabled) { EnableWindow(control, enabled); }
void Visible(HWND control, bool visible) { ShowWindow(control, visible ? SW_SHOW : SW_HIDE); }
bool Checked(HWND control) { return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void SetChecked(HWND control, bool checked) { SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0); }
void ComboClear(HWND combo) { SendMessageW(combo, CB_RESETCONTENT, 0, 0); }
void ComboAdd(HWND combo, const wstring& text) { SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())); }
void ComboSetItem(HWND combo, int index, const wstring& text) {
    int selected = ComboIndex(combo);
    SendMessageW(combo, CB_DELETESTRING, index, 0);
    SendMessageW(combo, CB_INSERTSTRING, index, reinterpret_cast<LPARAM>(text.c_str()));
    if (selected == index) ComboSelect(combo, index);
}
int ComboCount(HWND combo) { return int(SendMessageW(combo, CB_GETCOUNT, 0, 0)); }
int ComboIndex(HWND combo) { return int(SendMessageW(combo, CB_GETCURSEL, 0, 0)); }
void ComboSelect(HWND combo, int index) { SendMessageW(combo, CB_SETCURSEL, index, 0); }
int TrackValue(HWND track) { return int(SendMessageW(track, TBM_GETPOS, 0, 0)); }
void SetTrackValue(HWND track, int value) { SendMessageW(track, TBM_SETPOS, TRUE, value); }

void SetProgress(HWND bar, int value, int maximum) {
    SendMessageW(bar, PBM_SETRANGE32, 0, maximum);
    SendMessageW(bar, PBM_SETPOS, value, 0);
}

void SetMarquee(HWND bar, bool marquee) {
    LONG_PTR style = GetWindowLongPtrW(bar, GWL_STYLE);
    bool now = (style & PBS_MARQUEE) != 0;
    if (now == marquee) return;
    SetWindowLongPtrW(bar, GWL_STYLE, marquee ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
    SendMessageW(bar, PBM_SETMARQUEE, marquee ? TRUE : FALSE, 30);
}

void ListColumns(HWND list, const std::vector<std::pair<wstring, int>>& columns) {
    for (size_t i = 0; i < columns.size(); i++) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(columns[i].first.c_str());
        column.cx = Px(columns[i].second);
        ListView_InsertColumn(list, int(i), &column);
    }
}

void ListAdd(HWND list, const std::vector<wstring>& cells) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(list);
    item.pszText = const_cast<wchar_t*>(cells.empty() ? L"" : cells[0].c_str());
    int row = ListView_InsertItem(list, &item);
    for (size_t i = 1; i < cells.size(); i++) ListView_SetItemText(list, row, int(i), const_cast<wchar_t*>(cells[i].c_str()));
}

void ListSetCell(HWND list, int row, int column, const wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

int ListSelected(HWND list) {
    if (ListView_GetSelectedCount(list) != 1) return -1;
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

void Move(HWND control, int x, int y, int w, int h) { MoveWindow(control, Px(x), Px(y), Px(w), Px(h), TRUE); }

int TopOf(HWND control) {
    RECT rect;
    GetWindowRect(control, &rect);
    MapWindowPoints(nullptr, GetParent(control), reinterpret_cast<POINT*>(&rect), 2);
    return MulDiv(rect.top, 96, int(g_dpi));
}

void AppendText(HWND edit, const wstring& text) {
    int length = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, length, length);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

}  // namespace nebula::ui
