#include "explicit_preferences.hpp"
#include "picker_window.hpp"
#include <CommCtrl.h>
#include <Shlwapi.h>
#include <iostream>
#include <fstream>

using namespace RuffnecKk::AutoPickup;
using namespace RuffnecKk::AutoPickup::Picker;
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
std::mutex eventMutex;
std::vector<WindowAction> events;
void Event(void*, WindowAction value) noexcept {
    std::lock_guard lock(eventMutex); events.push_back(value);
}
auto Last() -> WindowAction { std::lock_guard lock(eventMutex); return events.empty() ? WindowAction{} : events.back(); }
template<class Predicate> bool Await(Predicate predicate) {
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) { if (predicate()) return true; Sleep(1); }
    return false;
}
bool AwaitText(HWND root, const wchar_t* expected) {
    struct Query { const wchar_t* expected{}; bool found{}; } query{expected};
    return Await([&] {
        query.found = false;
        EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
            auto& q = *reinterpret_cast<Query*>(data);
            wchar_t text[256]{}; GetWindowTextW(child, text, 256);
            if (std::wstring_view(text) == q.expected) { q.found = true; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&query));
        return query.found;
    });
}
bool AwaitCount(HWND list, int count) {
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        if (SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == count) return true;
        Sleep(1);
    }
    return false;
}
HWND CursorOf(HWND root) {
    struct Query { HWND root{}, cursor{}; } query{root};
    EnumThreadWindows(GetWindowThreadProcessId(root, nullptr), [](HWND candidate, LPARAM data) -> BOOL {
        auto& found = *reinterpret_cast<Query*>(data);
        wchar_t name[80]{}; GetClassNameW(candidate, name, 80);
        if (GetWindow(candidate, GW_OWNER) == found.root && std::wstring_view(name) == L"AutoPickupAnything.SettingsCursor.v1") {
            found.cursor = candidate; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.cursor;
}
void CheckInitialControls(HWND root) {
    RECT header{}, first{}, radius{};
    GetWindowRect(GetDlgItem(root, 107), &header);
    GetWindowRect(GetDlgItem(root, 200), &first);
    GetWindowRect(GetDlgItem(root, 220), &radius);
    CHECK(header.bottom <= first.top && header.bottom <= radius.top);
    CHECK(GetWindowLongPtrW(GetDlgItem(root, 220), GWL_STYLE) & WS_CLIPSIBLINGS);
    for (auto id : {220, 221}) {
        wchar_t text[8]{}; GetWindowTextW(GetDlgItem(root, id), text, 8);
        CHECK(std::wstring_view(text) == L"4"); // Value exists before any hover.
    }
    using VersionFn = HRESULT(CALLBACK*)(DLLVERSIONINFO*);
    const auto module = reinterpret_cast<HMODULE>(GetClassLongPtrW(GetDlgItem(root, 107), GCLP_HMODULE));
    const auto version = reinterpret_cast<VersionFn>(GetProcAddress(module, "DllGetVersion"));
    DLLVERSIONINFO info{}; info.cbSize = sizeof(info);
    CHECK(version && SUCCEEDED(version(&info)) && info.dwMajorVersion >= 6);
}
void CheckCursor(HWND root) {
    CHECK(!CursorOf(root)); // Native pointer: no compositor-driven popup.
    CHECK(GetWindow(root, GW_OWNER) == nullptr);
    CHECK(SendMessageW(root, WM_SETCURSOR, reinterpret_cast<WPARAM>(root), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE)) == TRUE);
}

}
void Capture(HWND hwnd, const char* path) {
    SetWindowPos(hwnd, nullptr, -30000, -30000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    RECT rect{}; GetWindowRect(hwnd, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    BITMAPINFO bitmap{}; bitmap.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* pixels{};
    const auto dc = CreateCompatibleDC(nullptr);
    const auto dib = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const auto previous = SelectObject(dc, dib);
    SendMessageW(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CHECKVISIBLE | PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    const DWORD bytes = static_cast<DWORD>(width * height * 4);
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER); header.bfSize = header.bfOffBits + bytes;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(&bitmap.bmiHeader), sizeof(BITMAPINFOHEADER));
    output.write(static_cast<const char*>(pixels), bytes);
    SelectObject(dc, previous); DeleteObject(dib); DeleteDC(dc);
    ShowWindow(hwnd, SW_HIDE);
}
int main(int argc, char** argv) {
    Window window;
    CHECK(window.Start(Event, nullptr));
    const auto hwnd = window.Handle();
    CHECK(hwnd && !IsWindowVisible(hwnd)); // Exercise real controls without opening a desktop window.
    const auto list = GetDlgItem(hwnd, 102), search = GetDlgItem(hwnd, 101);
    CHECK(list && search && !GetDlgItem(hwnd, 103));
    CHECK(GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA);
    std::vector<Entry> entries;
    for (unsigned i = 0; i < 6000; ++i) entries.push_back({i + 1, "Mod item " + std::to_string(i), true});
    entries.back() = {PackItemCode("gmo"), "Chaos Onyx", true};
    Config config=TestPreferences::Pickup();
    window.Update(entries, config, "ExampleMod", "Select an item", false, false);
    CHECK(AwaitCount(list, 6000));
    CHECK(AwaitText(hwnd, L"Select an item")); // Async refresh finished its text updates.
    CHECK(SendMessageW(GetDlgItem(hwnd, 220), CB_GETCOUNT, 0, 0) == 20);
    CHECK(SendMessageW(GetDlgItem(hwnd, 220), CB_GETCURSEL, 0, 0) == 3);
    CheckInitialControls(hwnd); CheckCursor(hwnd);
    // A status/tab-only update must retain the displayed catalog and scroll.
    CHECK(SendMessageW(list, LVM_ENSUREVISIBLE, 5999, FALSE));
    const auto top = SendMessageW(list, LVM_GETTOPINDEX, 0, 0);
    window.Update({}, config, "ExampleMod", "Status without rebuilding items", false, false, false, {}, false);
    CHECK(AwaitText(hwnd, L"Status without rebuilding items"));
    CHECK(AwaitCount(list, 6000) && SendMessageW(list, LVM_GETTOPINDEX, 0, 0) == top);
    if (argc > 1) Capture(hwnd, argv[1]);
    CHECK(SendMessageW(list, LVM_ENSUREVISIBLE, 5999, FALSE));
    CHECK(SendMessageW(list, LVM_GETTOPINDEX, 0, 0) > 5900); // Scroll reaches the end of a large catalog.
    LVITEMW selected{}; selected.stateMask = LVIS_SELECTED | LVIS_FOCUSED; selected.state = selected.stateMask;
    SendMessageW(list, LVM_SETITEMSTATE, 5999, reinterpret_cast<LPARAM>(&selected));
    CHECK(SendMessageW(list, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0) & LVS_EX_CHECKBOXES);
    LVITEMW checkbox{}; checkbox.stateMask = LVIS_STATEIMAGEMASK; checkbox.state = INDEXTOSTATEIMAGEMASK(1);
    SendMessageW(list, LVM_SETITEMSTATE, 5999, reinterpret_cast<LPARAM>(&checkbox));
    CHECK(Last().kind == WindowActionKind::Choice && Last().code == PackItemCode("gmo") && Last().choice == Choice::Exclude);
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    CHECK(Last().kind == WindowActionKind::Choice && Last().code == PackItemCode("gmo") && Last().choice == Choice::Include);
    RECT row{}; row.left = LVIR_BOUNDS;
    SendMessageW(list, LVM_GETITEMRECT, 5999, reinterpret_cast<LPARAM>(&row));
    const auto point = MAKELPARAM(6, (row.top + row.bottom) / 2);
    SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, point); SendMessageW(list, WM_LBUTTONUP, 0, point);
    CHECK(Last().kind == WindowActionKind::Choice && Last().choice == Choice::Exclude);
    SetWindowTextW(search, L"Chaos Onyx");
    CHECK(window.SearchText() == "Chaos Onyx" && Last().kind == WindowActionKind::Search);
    SetChoice(config, PackItemCode("gmo"), Choice::Exclude);
    window.Update({entries.back()}, config, "ExampleMod", "Unsaved changes", true, true);
    CHECK(AwaitCount(list, 1));
    CHECK(AwaitText(hwnd, L"Unsaved changes"));
    CHECK(Await([&] { return SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == 0; }));
    CHECK((SendMessageW(list, LVM_GETITEMSTATE, 0, LVIS_STATEIMAGEMASK) & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1));
    for (auto id : {108, 109, 110}) {
        SendMessageW(GetDlgItem(hwnd, id), BM_CLICK, 0, 0);
        const auto action = Last();
        CHECK(action.kind == WindowActionKind::BulkChoice && action.targets && action.targets->size() == 1 && action.targets->front() == PackItemCode("gmo"));
        CHECK(action.choice == (id == 108 ? Choice::Include : id == 109 ? Choice::Exclude : Choice::Default));
    }
    if (argc > 2) {
        const auto tabs = GetDlgItem(hwnd, 107);
        SendMessageW(tabs, TCM_SETCURSEL, 1, 0);
        NMHDR tab{tabs, 107, TCN_SELCHANGE}; SendMessageW(hwnd, WM_NOTIFY, 107, reinterpret_cast<LPARAM>(&tab));
        Capture(hwnd, argv[2]);
    }
    CHECK(Await([&] { return IsWindowEnabled(GetDlgItem(hwnd, 104)); }));
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(104, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 104)));
    CHECK(Last().kind == WindowActionKind::Save);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(105, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 105)));
    CHECK(Last().kind == WindowActionKind::Filter);
    SendMessageW(GetDlgItem(hwnd, 202), BM_CLICK, 0, 0);
    CHECK(Last().kind == WindowActionKind::Toggle && Last().code == 2 && Last().value == 0);
    SendMessageW(GetDlgItem(hwnd, 220), CB_SETCURSEL, 8, 0);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(220, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 220)));
    CHECK(Last().kind == WindowActionKind::Range && Last().code == 0 && Last().value == 9);
    window.Update({}, config, "ExampleMod", "Standalone owns post-cast pickup", false, false, true);
    CHECK(AwaitCount(list, 0));
    CHECK(!IsWindowEnabled(GetDlgItem(hwnd, 211)) && !IsWindowEnabled(GetDlgItem(hwnd, 221)));
    CHECK(IsWindowEnabled(GetDlgItem(hwnd, 210)) && IsWindowEnabled(GetDlgItem(hwnd, 220)));
    std::vector<Entry> seven;
    for (unsigned i = 1; i <= 7; ++i) seven.push_back({PackItemCode("k0" + std::to_string(i)), "Orb " + std::to_string(i), true});
    window.Update(seven, config, "ExampleMod", "Seven search matches", false, false);
    CHECK(AwaitCount(list, 7));
    SendMessageW(GetDlgItem(hwnd, 108), BM_CLICK, 0, 0);
    const auto snapshot = Last();
    CHECK(snapshot.targets && snapshot.targets->size() == 7);
    SetWindowTextW(search, L"another query");
    window.Update({}, config, "ExampleMod", "Different search", false, false);
    CHECK(AwaitCount(list, 0));
    CHECK(snapshot.targets && snapshot.targets->size() == 7 && snapshot.targets->front() == seven.front().code);
    Reimagined::InteractSettings::View interact; interact.available = true; interact.values.flags=63; interact.dirty = true; interact.status = "Independent Auto Interact changes";
    config.enabled = false;
    window.Update({}, config, "ExampleMod", "Pickit is off", false, false, false, interact);
    const auto tabs = GetDlgItem(hwnd, 107);
    CHECK(TabCtrl_GetItemCount(tabs) == 4);
    SendMessageW(tabs, TCM_SETCURSEL, 2, 0);
    NMHDR selectedTab{tabs, 107, TCN_SELCHANGE}; SendMessageW(hwnd, WM_NOTIFY, 107, reinterpret_cast<LPARAM>(&selectedTab));
    CHECK(AwaitText(hwnd, L"Independent Auto Interact changes"));
    CHECK((GetWindowLongPtrW(GetDlgItem(hwnd, 300), GWL_STYLE) & WS_VISIBLE)
        && !(GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) && !(GetWindowLongPtrW(GetDlgItem(hwnd, 200), GWL_STYLE) & WS_VISIBLE));
    for (int i = 300; i <= 306; ++i) CHECK(IsWindowEnabled(GetDlgItem(hwnd, i)));
    wchar_t rangeText[64]{}; GetWindowTextW(GetDlgItem(hwnd, 320), rangeText, 64);
    CHECK(std::wstring_view(rangeText) == L"Normal reach (default)");
    SendMessageW(GetDlgItem(hwnd, 302), BM_CLICK, 0, 0);
    CHECK(Last().kind == WindowActionKind::InteractToggle && Last().code == 2 && Last().value == 0);
    SendMessageW(GetDlgItem(hwnd, 320), CB_SETCURSEL, 20, 0);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(320, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 320)));
    CHECK(Last().kind == WindowActionKind::InteractNumber && Last().code == 0 && Last().value == 20);
    SendMessageW(GetDlgItem(hwnd, 320), CB_SETCURSEL, 0, 0);
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(320, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(hwnd, 320)));
    CHECK(Last().kind == WindowActionKind::InteractNumber && Last().value == 0);
    SendMessageW(tabs,TCM_SETCURSEL,3,0);
    SendMessageW(hwnd,WM_NOTIFY,107,reinterpret_cast<LPARAM>(&selectedTab));
    CHECK((GetWindowLongPtrW(GetDlgItem(hwnd,400),GWL_STYLE)&WS_VISIBLE)
        && !(GetWindowLongPtrW(GetDlgItem(hwnd,300),GWL_STYLE)&WS_VISIBLE));
    CHECK(SendMessageW(GetDlgItem(hwnd,400),BM_GETCHECK,0,0)==BST_CHECKED);
    CHECK(SendMessageW(GetDlgItem(hwnd,403),BM_GETCHECK,0,0)==BST_UNCHECKED);
    SendMessageW(GetDlgItem(hwnd,403),BM_CLICK,0,0);
    CHECK(Last().kind==WindowActionKind::PotionToggle && Last().code==3 && Last().value==1);
    config.potionRouting.families[0].columns={4,2,0,0};
    window.Update({},config,"ExampleMod","Potion routing draft",true,false,false,interact,false);
    CHECK(AwaitText(hwnd,L"Potion routing draft"));
    CHECK(SendMessageW(GetDlgItem(hwnd,403),BM_GETCHECK,0,0)==BST_CHECKED);
    Config empty;
    window.Update({},empty,"ExampleMod","No selections by default",false,false,false,{},false);
    CHECK(AwaitText(hwnd,L"No selections by default"));
    for(int id=400;id<415;++id)CHECK(SendMessageW(GetDlgItem(hwnd,id),BM_GETCHECK,0,0)==BST_UNCHECKED);
    CHECK(!IsWindowEnabled(GetDlgItem(hwnd,104)));
    if (argc > 3) Capture(hwnd, argv[3]);
    window.Update({}, config, "ExampleMod", "Pickit is off", false, false);
    CHECK(Await([&] { return !IsWindowEnabled(GetDlgItem(hwnd, 300))
        && !IsWindowEnabled(GetDlgItem(hwnd, 320)) && !IsWindowEnabled(GetDlgItem(hwnd, 104)); }));
    window.Stop(); CHECK(!window.Handle() && !IsWindow(hwnd));
    CHECK(window.Start(Event, nullptr)); window.Stop(); // Reload creates a fresh message thread and controls.
    std::cout << "Real themed Win32 picker: initial controls, native cursor handling, 6000 rows, scrolling, search, save and teardown; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
