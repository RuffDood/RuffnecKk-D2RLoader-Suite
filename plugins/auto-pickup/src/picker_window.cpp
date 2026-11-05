#include "picker_window.hpp"
#include <CommCtrl.h>
#include <windowsx.h>
#include <array>
#include <climits>

namespace RuffnecKk::AutoPickup::Picker {
namespace {
constexpr auto ClassName = L"AutoPickupAnything.ItemPicker.v1";
constexpr UINT ShowMessage = WM_APP + 71, RefreshMessage = WM_APP + 72, HideMessage = WM_APP + 73, StopMessage = WM_APP + 74;
constexpr int SearchId = 101, ListId = 102, SaveId = 104, FilterId = 105, CloseId = 106, PickId = 108, LeaveId = 109, ResetId = 110;
auto Wide(std::string_view value) -> std::wstring {
    if (value.empty() || value.size() > INT_MAX) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (length) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}
auto Utf8(std::wstring_view value) -> std::string {
    const auto length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    if (length) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}
auto Instance() noexcept -> HINSTANCE {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Instance), &module);
    return module;
}
struct GameWindow { HWND handle{}; long long area{}; };
BOOL CALLBACK FindGame(HWND hwnd, LPARAM data) {
    DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(hwnd) || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    RECT rect{}; GetClientRect(hwnd, &rect);
    const auto area = static_cast<long long>(rect.right) * rect.bottom;
    auto& candidate = *reinterpret_cast<GameWindow*>(data);
    if (area > candidate.area) candidate = {hwnd, area};
    return TRUE;
}
}
auto Window::Start(Handler callback, void* data) noexcept -> bool {
    stopping.store(false);
    handler.store(callback); userData = data;
    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return false;
    thread = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
    if (!thread) { CloseHandle(ready); ready = nullptr; return false; }
    if (WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0 || !window.load()) { Stop(); return false; }
    return true;
}
void Window::Stop() noexcept {
    stopping.store(true);
    handler.store(nullptr);
    if (const auto hwnd = window.load()) PostMessageW(hwnd, StopMessage, 0, 0);
    if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); thread = nullptr; }
    if (ready) { CloseHandle(ready); ready = nullptr; }
    visible.store(false); userData = nullptr;
    std::lock_guard lock(mutex); query.clear(); rows.clear(); pendingRows.clear();
}
auto Window::Open() noexcept -> bool {
    const auto hwnd = window.load();
    if (!hwnd) return false;
    visible.store(true);
    if (!PostMessageW(hwnd, ShowMessage, 0, 0)) { visible.store(false); return false; }
    return true;
}
void Window::Close() noexcept {
    visible.store(false);
    if (const auto hwnd = window.load()) PostMessageW(hwnd, HideMessage, 0, 0);
}
auto Window::SearchText() const -> std::string {
    // This member is UI-thread/worker shared. Keep one bounded copied value.
    std::lock_guard lock(mutex); return query;
}
void Window::Update(const std::vector<Entry>& entries, const Config& config, const std::string& source,
        const std::string& status, bool dirty, bool filtered, bool delegated, const Reimagined::InteractSettings::View& interact, bool updateItems) {
    std::vector<WindowRow> next;
    if (updateItems) next.reserve(entries.size());
    if (updateItems) for (const auto& item : entries) {
        const auto state = GetChoice(config, item.code);
        const auto policy = state == Choice::Include ? L"Include" : state == Choice::Exclude ? L"Exclude"
            : config.Selects(item.code) ? L"Default: pickup" : L"Default: leave";
        next.push_back({item.code, Wide(item.name), Wide(CodeText(item.code)), policy, state, config.Selects(item.code)});
    }
    {
        std::lock_guard lock(mutex);
        if (updateItems) { pendingRows = std::move(next); ++pendingRowsRevision; }
        pendingConfig = config;
        pendingSource = Wide(source); pendingStatus = Wide(status);
        pendingDirty = dirty; pendingFiltered = filtered; pendingDelegated = delegated;
        pendingInteract = interact;
    }
    if (const auto hwnd = window.load()) PostMessageW(hwnd, RefreshMessage, 0, 0);
}
void Window::Emit(WindowAction action) noexcept {
    if (const auto callback = handler.load()) callback(userData, std::move(action));
}
DWORD WINAPI Window::ThreadMain(void* data) noexcept { return static_cast<Window*>(data)->Run(); }
auto Window::Run() noexcept -> DWORD {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = Instance(); cls.lpfnWndProc = Procedure;
    cls.lpszClassName = ClassName; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { SetEvent(ready); return 1; }
    const auto hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, ClassName, L"Auto Pickup Settings",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 820, 660,
        nullptr, nullptr, Instance(), this);
    // A cross-thread owner implicitly joins input queues. Keep this top-level
    // window unowned so the game's cursor hiding and input stalls stay isolated.
    // Balance only our own increment on this thread; never touch the game queue.
    ShowCursor(TRUE);
    window.store(hwnd); SetEvent(ready);
    if (!hwnd) { ShowCursor(FALSE); UnregisterClassW(ClassName, Instance()); return 1; }
    if (stopping.load()) PostMessageW(hwnd, StopMessage, 0, 0);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && (message.wParam == VK_ESCAPE
            || (message.wParam == 'P' && (GetKeyState(VK_CONTROL) & 0x8000)))) { Hide(); Emit({WindowActionKind::Close}); continue; }
        if (!IsDialogMessageW(hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (font) { DeleteObject(font); font = nullptr; }
    ShowCursor(FALSE);
    window.store(nullptr); visible.store(false);
    UnregisterClassW(ClassName, Instance());
    return 0;
}
LRESULT CALLBACK Window::Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window.store(hwnd);
    }
    if (self) { try { return self->Message(hwnd, message, wp, lp); } catch (...) { return 0; } }
    return DefWindowProcW(hwnd, message, wp, lp);
}
auto Window::CursorFor(HWND target, int hit) const noexcept -> HCURSOR {
    LPCWSTR id = IDC_ARROW;
    if (hit == HTLEFT || hit == HTRIGHT) id = IDC_SIZEWE;
    else if (hit == HTTOP || hit == HTBOTTOM) id = IDC_SIZENS;
    else if (hit == HTTOPLEFT || hit == HTBOTTOMRIGHT) id = IDC_SIZENWSE;
    else if (hit == HTTOPRIGHT || hit == HTBOTTOMLEFT) id = IDC_SIZENESW;
    else if (target == search && hit == HTCLIENT) id = IDC_IBEAM;
    return LoadCursorW(nullptr, id);
}
void Window::Repaint() {
    // Include child controls: merely invalidating the parent misses combo
    // selection fields, which otherwise may remain blank until mouse hover.
    RedrawWindow(window.load(), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}
void Window::Layout() {
    const auto hwnd = window.load();
    RECT r{}; GetClientRect(hwnd, &r);
    auto px = [&](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int margin = px(16), width = r.right - 2 * margin, h = px(28);
    MoveWindow(sourceLabel, margin, px(12), width, px(36), TRUE);
    // Use the tab as a header. A full-page themed tab is a sibling of these
    // controls and can paint over their selection fields during activation.
    MoveWindow(tabs, margin, px(54), width, px(34), TRUE);
    MoveWindow(search, margin, px(100), (std::max)(px(100), width - px(205)), h, TRUE);
    MoveWindow(filter, r.right - margin - px(190), px(100), px(190), h, TRUE);
    MoveWindow(countLabel, margin, px(136), width, px(22), TRUE);
    const int bulkWidth = (width - px(16)) / 3;
    MoveWindow(pickResults, margin, px(164), bulkWidth, h, TRUE);
    MoveWindow(leaveResults, margin + bulkWidth + px(8), px(164), bulkWidth, h, TRUE);
    MoveWindow(resetResults, margin + 2 * (bulkWidth + px(8)), px(164), bulkWidth, h, TRUE);
    MoveWindow(list, margin, px(204), width, (std::max)(px(100), static_cast<int>(r.bottom) - px(343)), TRUE);
    MoveWindow(save, r.right - margin - px(265), r.bottom - px(126), px(155), px(32), TRUE);
    MoveWindow(close, r.right - margin - px(100), r.bottom - px(126), px(100), px(32), TRUE);
    MoveWindow(statusLabel, margin, r.bottom - px(79), width, px(62), TRUE);
    for (std::size_t i = 0; i < toggles.size(); ++i)
        MoveWindow(toggles[i], margin + px(16) + static_cast<int>(i / 7) * width / 2,
            px(100 + static_cast<int>(i % 7) * 25), width / 2 - px(24), px(24), TRUE);
    for (int i = 0; i < 2; ++i) {
        const auto x = margin + px(16) + i * width / 2;
        MoveWindow(rangeLabels[i], x, px(290), width / 2 - px(24), px(24), TRUE);
        MoveWindow(ranges[i], x, px(318), px(100), px(200), TRUE);
    }
    for(std::size_t family=0;family<3;++family) {
        MoveWindow(potionLabels[family],margin+px(16),px(100+static_cast<int>(family)*90),width-px(32),px(24),TRUE);
        for(std::size_t choice=0;choice<5;++choice)
            MoveWindow(potionToggles[family*5+choice],margin+px(16)+static_cast<int>(choice)*width/5,px(128+static_cast<int>(family)*90),width/5-px(12),px(26),TRUE);
    }
    MoveWindow(potionHelp,margin+px(16),px(385),width-px(32),px(65),TRUE);
    MoveWindow(ownership, margin + px(16), px(360), width - px(32), px(85), TRUE);
    for (std::size_t i = 0; i < interactToggles.size(); ++i)
        MoveWindow(interactToggles[i], margin + px(16) + static_cast<int>(i / 4) * width / 2,
            px(100 + static_cast<int>(i % 4) * 29), width / 2 - px(24), px(26), TRUE);
    for (int i = 0; i < 3; ++i) {
        const auto x = margin + px(16) + i * width / 3;
        MoveWindow(interactLabels[i], x, px(240), width / 3 - px(24), px(24), TRUE);
        MoveWindow(interactNumbers[i], x, px(270), width / 3 - px(24), px(240), TRUE);
    }
    MoveWindow(interactHelp, margin + px(16), px(322), width - px(32), px(135), TRUE);
    ListView_SetColumnWidth(list, 0, (std::max)(px(120), width - px(245)));
    ListView_SetColumnWidth(list, 1, px(75)); ListView_SetColumnWidth(list, 2, px(145));
}
void Window::CheckRow(int index, UINT state) {
    if (updating || index < 0 || static_cast<std::size_t>(index) >= rows.size()) return;
    const auto image = (state & LVIS_STATEIMAGEMASK) >> 12;
    if (image != 1 && image != 2) return;
    auto& row = rows[static_cast<std::size_t>(index)];
    const bool checked = image == 2;
    if (row.checked == checked) return;
    row.checked = checked; row.choice = checked ? Choice::Include : Choice::Exclude;
    row.policy = checked ? L"Include" : L"Exclude";
    Emit({WindowActionKind::Choice, row.code, row.choice});
    ListView_RedrawItems(list, index, index);
}
LRESULT CALLBACK Window::ListProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) noexcept {
    auto* self = reinterpret_cast<Window*>(data);
    try {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ListProcedure, id);
        if (message == LVM_SETITEMSTATE && lp) {
            const auto* item = reinterpret_cast<LVITEMW*>(lp);
            if (item->stateMask & LVIS_STATEIMAGEMASK) self->CheckRow(static_cast<int>(wp), item->state);
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) {
            LVHITTESTINFO hit{}; hit.pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const auto index = ListView_HitTest(hwnd, &hit);
            if (index >= 0 && (hit.flags & LVHT_ONITEMSTATEICON) && static_cast<std::size_t>(index) < self->rows.size()) {
                if (message != WM_LBUTTONUP) {
                    SetFocus(hwnd); ListView_SetItemState(hwnd, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    self->CheckRow(index, INDEXTOSTATEIMAGEMASK(self->rows[static_cast<std::size_t>(index)].checked ? 1 : 2));
                }
                return 0;
            }
        }
        if (message == WM_KEYDOWN && wp == VK_SPACE) {
            const auto index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
            if (!(lp & (LPARAM{1} << 30)) && index >= 0 && static_cast<std::size_t>(index) < self->rows.size())
                self->CheckRow(index, INDEXTOSTATEIMAGEMASK(self->rows[static_cast<std::size_t>(index)].checked ? 1 : 2));
            return 0;
        }
    } catch (...) {}
    return DefSubclassProc(hwnd, message, wp, lp);
}
void Window::Bulk(Choice choiceValue) {
    auto targets = std::make_shared<std::vector<std::uint32_t>>();
    targets->reserve(rows.size());
    for (const auto& row : rows) targets->push_back(row.code);
    Emit({WindowActionKind::BulkChoice, 0, choiceValue, 0, std::move(targets)});
}
void Window::Refresh() {
    const auto index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const auto selected = index >= 0 && static_cast<std::size_t>(index) < rows.size() ? rows[static_cast<std::size_t>(index)].code : 0;
    std::wstring sourceText, statusText; bool dirty{}, filtered{}, delegated{}, changedRows{}; Config config;
    {
        std::lock_guard lock(mutex);
        changedRows = rowsRevision != pendingRowsRevision;
        if (changedRows) { rows = pendingRows; rowsRevision = pendingRowsRevision; }
        sourceText = pendingSource; statusText = pendingStatus;
        dirty = pendingDirty; filtered = pendingFiltered; config = pendingConfig; delegated = pendingDelegated;
        activeInteract = pendingInteract;
    }
    updating = true;
    for (std::size_t i = 0; i < toggles.size(); ++i) {
        SendMessageW(toggles[i], BM_SETCHECK, ToggleValue(config, i) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(toggles[i], !delegated || i < 11);
    }
    for(std::size_t i=0;i<potionToggles.size();++i)
        SendMessageW(potionToggles[i],BM_SETCHECK,PotionRouting::ToggleValue(config.potionRouting,i)?BST_CHECKED:BST_UNCHECKED,0);
    for(std::size_t i=0;i<3;++i) {
        const auto title=std::string(PotionRouting::FamilyNames[i])+" - column priority "+PotionRouting::ColumnsText(config.potionRouting.families[i]);
        SetWindowTextW(potionLabels[i],Wide(title).c_str());
    }
    SendMessageW(ranges[0], CB_SETCURSEL, config.walkingRange - 1, 0);
    SendMessageW(ranges[1], CB_SETCURSEL, config.teleportRange - 1, 0);
    EnableWindow(ranges[1], !delegated);
    for (std::size_t i = 0; i < interactToggles.size(); ++i) {
        SendMessageW(interactToggles[i], BM_SETCHECK, activeInteract.values.flags & (1U << i) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(interactToggles[i], activeInteract.available);
    }
    for (std::size_t i = 0; i < interactNumbers.size(); ++i) {
        const auto value = i == 0 ? activeInteract.values.range : i == 1 ? activeInteract.values.intervalMs : activeInteract.values.retryMs;
        const auto text = i == 0 && !value ? std::wstring(L"Normal reach (default)") : std::to_wstring(value);
        auto comboIndex = SendMessageW(interactNumbers[i], CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
        if (comboIndex == CB_ERR) {
            comboIndex = SendMessageW(interactNumbers[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            SendMessageW(interactNumbers[i], CB_SETITEMDATA, comboIndex, value);
        }
        SendMessageW(interactNumbers[i], CB_SETCURSEL, comboIndex, 0); EnableWindow(interactNumbers[i], activeInteract.available);
    }
    SetWindowTextW(interactHelp, activeInteract.available
        ? L"Normal reach uses the game's usual interaction distance. Choose 1-20 to open eligible objects without moving.\r\n\r\nWalls still block interaction. Normal barrels require close range. Personal stashes, Evil Urns, exploding barrels, and quest objects stay manual."
        : L"Auto Interact settings are unavailable. Check the Pickit log.");
    SetWindowTextW(ownership, delegated
        ? L"Post-cast distance and options are disabled because an older Teleport Auto Pickup is installed. Update it to version 0.3.5 or later, or uninstall it, then restart the game."
        : L"Item rules take priority over category settings.");
    if (changedRows) {
        ListView_SetItemCountEx(list, static_cast<int>(rows.size()), LVSICF_NOSCROLL);
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        for (std::size_t i = 0; i < rows.size(); ++i) if (rows[i].code == selected)
            ListView_SetItemState(list, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    currentSource = sourceText; currentStatus = statusText; pickitDirty = dirty;
    const auto checkedCount = std::count_if(rows.begin(), rows.end(), [](const auto& row) { return row.checked; });
    SetWindowTextW(countLabel, (std::to_wstring(rows.size()) + L" matches | " + std::to_wstring(checkedCount) + L" checked for pickup").c_str());
    SetWindowTextW(pickResults, (L"Pick up results (" + std::to_wstring(rows.size()) + L")").c_str());
    SetWindowTextW(leaveResults, (L"Leave results (" + std::to_wstring(rows.size()) + L")").c_str());
    for (const auto child : {pickResults, leaveResults, resetResults}) EnableWindow(child, !rows.empty());
    SendMessageW(filter, BM_SETCHECK, filtered ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(list, nullptr, FALSE);
    updating = false;
    ShowTab();
}
void Window::ShowTab() {
    const bool potions = TabCtrl_GetCurSel(tabs) == 3;
    for(auto child:potionToggles)ShowWindow(child,potions?SW_SHOW:SW_HIDE);
    for(auto child:potionLabels)ShowWindow(child,potions?SW_SHOW:SW_HIDE);
    ShowWindow(potionHelp,potions?SW_SHOW:SW_HIDE);
    const bool general = TabCtrl_GetCurSel(tabs) == 0;
    const bool items = TabCtrl_GetCurSel(tabs) == 1;
    const bool interact = TabCtrl_GetCurSel(tabs) == 2;
    for (auto hwnd : toggles) ShowWindow(hwnd, general ? SW_SHOW : SW_HIDE);
    for (auto hwnd : rangeLabels) ShowWindow(hwnd, general ? SW_SHOW : SW_HIDE);
    for (auto hwnd : ranges) ShowWindow(hwnd, general ? SW_SHOW : SW_HIDE);
    ShowWindow(ownership, general ? SW_SHOW : SW_HIDE);
    for (auto hwnd : {search, list, countLabel, filter, pickResults, leaveResults, resetResults}) ShowWindow(hwnd, items ? SW_SHOW : SW_HIDE);
    for (auto child : interactToggles) ShowWindow(child, interact ? SW_SHOW : SW_HIDE);
    for (auto child : interactLabels) ShowWindow(child, interact ? SW_SHOW : SW_HIDE);
    for (auto child : interactNumbers) ShowWindow(child, interact ? SW_SHOW : SW_HIDE);
    ShowWindow(interactHelp, interact ? SW_SHOW : SW_HIDE);
    SetWindowTextW(sourceLabel, potions ? L"Potion routing" : interact ? L"Auto Interact" : currentSource.c_str());
    SetWindowTextW(statusLabel, interact ? Wide(activeInteract.status).c_str()
        : items && currentStatus.empty() ? L"Check items to pick them up. Uncheck items to leave them. The buttons below affect all search results."
        : currentStatus.c_str());
    EnableWindow(save, interact ? activeInteract.available && activeInteract.dirty : pickitDirty);
    Repaint();
}
void Window::Hide() {
    visible.store(false); ShowWindow(window.load(), SW_HIDE);
    if (gameWindow && IsWindow(gameWindow)) SetForegroundWindow(gameWindow);
}
auto Window::Message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) -> LRESULT {
    switch (message) {
    case WM_CREATE: {
        dpi = GetDpiForWindow(hwnd); if (!dpi) dpi = 96;
        font = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto control = [&](LPCWSTR type, LPCWSTR text, DWORD style, int id) {
            const auto child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 10, 10, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), Instance(), nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return child;
        };
        tabs = control(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, 107);
        TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = const_cast<LPWSTR>(L"General"); TabCtrl_InsertItem(tabs, 0, &tab);
        tab.pszText = const_cast<LPWSTR>(L"Item picker"); TabCtrl_InsertItem(tabs, 1, &tab);
        tab.pszText = const_cast<LPWSTR>(L"Auto Interact"); TabCtrl_InsertItem(tabs, 2, &tab);
        tab.pszText = const_cast<LPWSTR>(L"Potion routing"); TabCtrl_InsertItem(tabs, 3, &tab);
        for(std::size_t i=0;i<15;++i) {
            const auto label=i%5==4?std::wstring(L"Inventory overflow"):L"Column "+std::to_wstring(i%5+1);
            potionToggles[i]=control(L"BUTTON",label.c_str(),WS_TABSTOP|BS_AUTOCHECKBOX,400+static_cast<int>(i));
        }
        for(std::size_t i=0;i<3;++i)potionLabels[i]=control(L"STATIC",L"",0,420+static_cast<int>(i));
        potionHelp=control(L"STATIC",L"Choose potions in General or Item picker. Columns are numbered left to right. Newly checked columns are tried last. With no columns, overflow sends potions to inventory; otherwise they stay on the ground.",0,424);
        for (std::size_t i = 0; i < interactToggles.size(); ++i)
            interactToggles[i] = control(L"BUTTON", Wide(Reimagined::InteractSettings::ToggleLabels[i]).c_str(), WS_TABSTOP | BS_AUTOCHECKBOX, 300 + static_cast<int>(i));
        const std::array<const wchar_t*, 3> interactText{L"Auto Interact distance", L"Scan interval (ms)", L"Retry cooldown (ms)"};
        for (std::size_t i = 0; i < interactNumbers.size(); ++i) {
            interactLabels[i] = control(L"STATIC", interactText[i], 0, 0);
            interactNumbers[i] = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 320 + static_cast<int>(i));
            const auto step = i == 0 ? 1U : i == 1 ? 50U : 1000U;
            const auto last = i == 0 ? 20U : i == 1 ? 1000U : 60000U;
            for (auto value = i == 0 ? 0U : step; value <= last; value += step) {
                const auto text = i == 0 && !value ? std::wstring(L"Normal reach (default)") : std::to_wstring(value);
                const auto index = SendMessageW(interactNumbers[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                SendMessageW(interactNumbers[i], CB_SETITEMDATA, index, value);
            }
        }
        interactHelp = control(L"STATIC", L"", 0, 0);
        for (std::size_t i = 0; i < toggles.size(); ++i)
            toggles[i] = control(L"BUTTON", Wide(ToggleLabels[i]).c_str(), WS_TABSTOP | BS_AUTOCHECKBOX, 200 + static_cast<int>(i));
        rangeLabels[0] = control(L"STATIC", L"Pickup distance while moving", 0, 0);
        rangeLabels[1] = control(L"STATIC", L"Pickup distance after casting", 0, 0);
        for (int i = 0; i < 2; ++i) {
            ranges[i] = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 220 + i);
            for (int radius = 1; radius <= 20; ++radius)
                SendMessageW(ranges[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(radius).c_str()));
        }
        ownership = control(L"STATIC", L"", 0, 0);
        sourceLabel = control(L"STATIC", L"Reading loaded game item definitions...", 0, 0);
        search = control(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, SearchId);
        SendMessageW(search, EM_SETLIMITTEXT, 120, 0); SendMessageW(search, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Search item name or code"));
        filter = control(L"BUTTON", L"Only custom item rules", WS_TABSTOP | BS_AUTOCHECKBOX, FilterId);
        pickResults = control(L"BUTTON", L"Pick up results", WS_TABSTOP | BS_PUSHBUTTON, PickId);
        leaveResults = control(L"BUTTON", L"Leave results", WS_TABSTOP | BS_PUSHBUTTON, LeaveId);
        resetResults = control(L"BUTTON", L"Use category defaults", WS_TABSTOP | BS_PUSHBUTTON, ResetId);
        countLabel = control(L"STATIC", L"", 0, 0);
        list = control(WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS, ListId);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
        ListView_SetCallbackMask(list, LVIS_STATEIMAGEMASK);
        if (!SetWindowSubclass(list, ListProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) return -1;
        std::array<LPCWSTR, 3> names{L"Item", L"Code", L"Pickup rule"};
        for (int i = 0; i < 3; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<LPWSTR>(names[i]); column.cx = 200; ListView_InsertColumn(list, i, &column); }
        save = control(L"BUTTON", L"Apply && Save", WS_TABSTOP | BS_PUSHBUTTON, SaveId);
        close = control(L"BUTTON", L"Close", WS_TABSTOP | BS_PUSHBUTTON, CloseId);
        statusLabel = control(L"STATIC", L"", 0, 0);
        if (!sourceLabel || !search || !filter || !countLabel || !list || !pickResults || !leaveResults || !resetResults || !save || !close || !statusLabel || !tabs || !ownership
            || std::any_of(toggles.begin(), toggles.end(), [](HWND child) { return !child; }) || !ranges[0] || !ranges[1]
            || !interactHelp || std::any_of(interactToggles.begin(), interactToggles.end(), [](HWND child) { return !child; })
            || std::any_of(interactNumbers.begin(), interactNumbers.end(), [](HWND child) { return !child; })) return -1;
        EnableWindow(save, FALSE); Layout(); ShowTab(); return 0;
    }
    case ShowMessage: {
        GameWindow game; EnumWindows(FindGame, reinterpret_cast<LPARAM>(&game)); gameWindow = game.handle;
        if (gameWindow) {
            RECT rect{}; GetWindowRect(gameWindow, &rect);
            const int width = MulDiv(820, static_cast<int>(dpi), 96), height = MulDiv(660, static_cast<int>(dpi), 96);
            SetWindowPos(hwnd, HWND_TOP, rect.left + (rect.right - rect.left - width) / 2,
                rect.top + (rect.bottom - rect.top - height) / 2, width, height, SWP_NOACTIVATE);
        }
        ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
        SetFocus(TabCtrl_GetCurSel(tabs) == 0 ? toggles[0] : TabCtrl_GetCurSel(tabs) == 2 ? interactToggles[0] : TabCtrl_GetCurSel(tabs) == 3 ? potionToggles[0] : search);
        Repaint(); return 0;
    }
    case RefreshMessage: Refresh(); return 0;
    case HideMessage: Hide(); return 0;
    case StopMessage: DestroyWindow(hwnd); return 0;
    case WM_CLOSE: Hide(); Emit({WindowActionKind::Close}); return 0;
    case WM_DESTROY: visible.store(false); PostQuitMessage(0); return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) Repaint();
        break;
    case WM_SETCURSOR:
        SetCursor(CursorFor(reinterpret_cast<HWND>(wp), LOWORD(lp)));
        return TRUE;
    case WM_SIZE: if (list) { Layout(); Repaint(); } return 0;
    case WM_GETMINMAXINFO: {
        auto* bounds = reinterpret_cast<MINMAXINFO*>(lp); bounds->ptMinTrackSize = {MulDiv(760, static_cast<int>(dpi), 96), MulDiv(640, static_cast<int>(dpi), 96)}; return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lp);
        if (header->hwndFrom == tabs && header->code == TCN_SELCHANGE) { ShowTab(); Emit({WindowActionKind::Tab, static_cast<std::uint32_t>(TabCtrl_GetCurSel(tabs))}); return 0; }
        if (header->hwndFrom != list) break;
        if (header->code == LVN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
            if (info->item.iItem >= 0 && static_cast<std::size_t>(info->item.iItem) < rows.size()) {
                auto& row = rows[static_cast<std::size_t>(info->item.iItem)];
                if (info->item.mask & LVIF_TEXT)
                    info->item.pszText = info->item.iSubItem == 0 ? row.name.data() : info->item.iSubItem == 1 ? row.codeText.data() : row.policy.data();
                if (info->item.mask & LVIF_STATE) {
                    info->item.stateMask |= LVIS_STATEIMAGEMASK;
                    info->item.state = (info->item.state & ~LVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(row.checked ? 2 : 1);
                }
            }
            return 0;
        }
        if (header->code == LVN_SETDISPINFOW) {
            const auto* info = reinterpret_cast<NMLVDISPINFOW*>(lp);
            if ((info->item.mask & LVIF_STATE) && (info->item.stateMask & LVIS_STATEIMAGEMASK)) CheckRow(info->item.iItem, info->item.state);
        }
        if (header->code == LVN_ITEMCHANGED) {
            const auto* info = reinterpret_cast<NMLISTVIEW*>(lp);
            if ((info->uChanged & LVIF_STATE) && ((info->uOldState ^ info->uNewState) & LVIS_STATEIMAGEMASK)) CheckRow(info->iItem, info->uNewState);
        }
        return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)>=400 && LOWORD(wp)<415 && HIWORD(wp)==BN_CLICKED && !updating) {
            const auto i=static_cast<std::uint32_t>(LOWORD(wp)-400);
            Emit({WindowActionKind::PotionToggle,i,{},SendMessageW(potionToggles[i],BM_GETCHECK,0,0)==BST_CHECKED?1:0});return 0;
        }
        if (LOWORD(wp) >= 300 && LOWORD(wp) < 300 + interactToggles.size() && HIWORD(wp) == BN_CLICKED && !updating && activeInteract.available) {
            const auto index = static_cast<std::uint32_t>(LOWORD(wp) - 300);
            Emit({WindowActionKind::InteractToggle, index, {}, SendMessageW(interactToggles[index], BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0}); return 0;
        }
        if (LOWORD(wp) >= 320 && LOWORD(wp) <= 322 && HIWORD(wp) == CBN_SELCHANGE && !updating && activeInteract.available) {
            const auto index = static_cast<std::uint32_t>(LOWORD(wp) - 320);
            const auto selected = SendMessageW(interactNumbers[index], CB_GETCURSEL, 0, 0);
            if (selected != CB_ERR) Emit({WindowActionKind::InteractNumber, index, {}, static_cast<int>(SendMessageW(interactNumbers[index], CB_GETITEMDATA, selected, 0))});
            return 0;
        }
        if (HIWORD(wp) == BN_CLICKED) {
            if (LOWORD(wp) == PickId) { Bulk(Choice::Include); return 0; }
            if (LOWORD(wp) == LeaveId) { Bulk(Choice::Exclude); return 0; }
            if (LOWORD(wp) == ResetId) { Bulk(Choice::Default); return 0; }
        }
        if (LOWORD(wp) >= 200 && LOWORD(wp) < 200 + toggles.size() && HIWORD(wp) == BN_CLICKED && !updating) {
            const auto index = static_cast<std::uint32_t>(LOWORD(wp) - 200);
            Emit({WindowActionKind::Toggle, index, {}, SendMessageW(toggles[index], BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0}); return 0;
        }
        if ((LOWORD(wp) == 220 || LOWORD(wp) == 221) && HIWORD(wp) == CBN_SELCHANGE && !updating) {
            const auto index = static_cast<std::uint32_t>(LOWORD(wp) - 220);
            const auto value = SendMessageW(ranges[index], CB_GETCURSEL, 0, 0);
            if (value >= 0 && value < 20) Emit({WindowActionKind::Range, index, {}, static_cast<int>(value + 1)});
            return 0;
        }
        if (LOWORD(wp) == SearchId && HIWORD(wp) == EN_CHANGE && !updating) {
            std::array<wchar_t, 256> text{}; GetWindowTextW(search, text.data(), static_cast<int>(text.size()));
            { std::lock_guard lock(mutex); query = Utf8(text.data()); }
            Emit({WindowActionKind::Search}); return 0;
        }
        if (LOWORD(wp) == FilterId && HIWORD(wp) == BN_CLICKED) { Emit({WindowActionKind::Filter}); return 0; }
        if (LOWORD(wp) == SaveId && HIWORD(wp) == BN_CLICKED) { Emit({WindowActionKind::Save}); return 0; }
        if (LOWORD(wp) == CloseId && HIWORD(wp) == BN_CLICKED) { Hide(); Emit({WindowActionKind::Close}); return 0; }
        break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
