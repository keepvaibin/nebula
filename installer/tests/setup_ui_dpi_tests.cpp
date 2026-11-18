// Nebula Setup's windows at a monitor's scale (installer/native/ui.cpp, P-115). A hidden window with a label, a list, a
// tab page and a moved button is told its monitor's scale changed from its own to half as much again (WM_DPICHANGED, as
// Windows sends it when the window is dragged onto such a monitor): the window takes the size Windows proposes, and
// every control's place, every font and the list's columns follow; a window opened afterwards opens at the new scale.
#include "ui.h"

#include <cstdio>

using namespace nebula;
using namespace nebula::ui;

namespace {

bool g_ok = true;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        g_ok = false;
    }
}

RECT PlaceIn(HWND control) {
    RECT rect;
    GetWindowRect(control, &rect);
    MapWindowPoints(nullptr, GetParent(control), reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

LONG FontHeight(HWND control) {
    LOGFONTW font{};
    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)), sizeof(font), &font);
    return font.lfHeight < 0 ? -font.lfHeight : font.lfHeight;
}

bool Near(long value, long wanted) { return value >= wanted - 1 && value <= wanted + 1; }

}  // namespace

int main() {
    Init();
    Form form(L"Scale test", 400, 300, Font(9.5));
    const UINT before = form.Dpi();
    HWND label = form.Label(form.Handle(), L"A label", 20, 30, 200, 20);
    HWND title = form.Label(form.Handle(), L"A title", 20, 60, 200, 30);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(Font(17, FW_SEMIBOLD)), TRUE);
    HWND list = form.List(form.Handle(), 20, 100, 300, 80);
    ListColumns(list, {{L"One", 100}, {L"Two", 150}});
    HWND tabs = form.Tabs(form.Handle(), 20, 190, 300, 90);
    HWND page = form.Page(tabs, L"Page");
    HWND onPage = form.Label(page, L"On the page", 10, 10, 100, 20);
    HWND button = form.Button(form.Handle(), L"Moved", 0, 0, 80, 24);
    Move(button, 300, 30, 80, 24);

    const RECT labelBefore = PlaceIn(label), onPageBefore = PlaceIn(onPage), buttonBefore = PlaceIn(button);
    const RECT pageBefore = PlaceIn(page);
    const LONG labelFont = FontHeight(label), titleFont = FontHeight(title);
    const int columnBefore = ListView_GetColumnWidth(list, 1);

    const UINT after = before * 3 / 2;
    RECT window;
    GetWindowRect(form.Handle(), &window);
    RECT suggested{window.left, window.top, window.left + MulDiv(window.right - window.left, 3, 2),
                   window.top + MulDiv(window.bottom - window.top, 3, 2)};
    SendMessageW(form.Handle(), WM_DPICHANGED, MAKEWPARAM(after, after), reinterpret_cast<LPARAM>(&suggested));

    std::printf("scale %u -> %u\n", before, after);
    expect(form.Dpi() == after, "the window takes the new scale");
    RECT now;
    GetWindowRect(form.Handle(), &now);
    expect(now.right - now.left == suggested.right - suggested.left && now.bottom - now.top == suggested.bottom - suggested.top,
           "the window takes the size Windows proposes");
    const RECT labelAfter = PlaceIn(label), onPageAfter = PlaceIn(onPage), buttonAfter = PlaceIn(button);
    std::printf("label %ld,%ld %ldx%ld -> %ld,%ld %ldx%ld\n", labelBefore.left, labelBefore.top,
                labelBefore.right - labelBefore.left, labelBefore.bottom - labelBefore.top, labelAfter.left, labelAfter.top,
                labelAfter.right - labelAfter.left, labelAfter.bottom - labelAfter.top);
    expect(Near(labelAfter.left, MulDiv(20, int(after), 96)) && Near(labelAfter.top, MulDiv(30, int(after), 96)) &&
               Near(labelAfter.right - labelAfter.left, MulDiv(200, int(after), 96)),
           "a control's place follows the scale");
    expect(Near(buttonAfter.left, MulDiv(300, int(after), 96)) && Near(buttonAfter.top, MulDiv(30, int(after), 96)),
           "a moved control keeps its new place at the new scale");
    expect(Near(onPageAfter.left, MulDiv(10, int(after), 96)), "a tab page's control follows the scale");
    const RECT pageAfter = PlaceIn(page);
    expect(pageAfter.right - pageAfter.left > pageBefore.right - pageBefore.left, "the tab page grows with its tabs");
    std::printf("fonts %ld, %ld -> %ld, %ld\n", labelFont, titleFont, FontHeight(label), FontHeight(title));
    expect(Near(FontHeight(label), MulDiv(labelFont, 3, 2)), "the window's font follows the scale");
    expect(Near(FontHeight(title), MulDiv(titleFont, 3, 2)), "a control's own font follows the scale");
    expect(Near(FontHeight(onPage), MulDiv(labelFont, 3, 2)), "a tab page's control's font follows the scale");
    expect(Near(ListView_GetColumnWidth(list, 1), MulDiv(columnBefore, 3, 2)), "the list's columns follow the scale");
    (void)buttonBefore;
    (void)onPageBefore;

    // Its owner is still on its real monitor (only told of another scale): a window it opens is at that monitor's
    // scale, with the font made at the new one taken back to it.
    Form later(L"Later", 200, 100, Font(9.5), &form);
    expect(later.Dpi() == before && Near(FontHeight(later.Label(later.Handle(), L"x", 0, 0, 50, 20)), labelFont),
           "a window opened afterwards is at its own monitor's scale, its font too");

    std::printf("%s\n", g_ok ? "setup ui scale: every case holds" : "setup ui scale: FAILED");
    return g_ok ? 0 : 1;
}
