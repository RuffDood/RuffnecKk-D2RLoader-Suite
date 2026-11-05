#include <Windows.h>
#include <atomic>
#include <iostream>

// Exercise the real Open/Close path without moving the user's mouse or taking
// foreground focus. Ownership, queues, native controls and cursor APIs are real.
BOOL WINAPI FixtureForeground(HWND) { return TRUE; }
HWND WINAPI FixtureFocus(HWND) { return nullptr; }
BOOL WINAPI FixtureShow(HWND hwnd, int command) {
    return ShowWindow(hwnd, command == SW_SHOW ? SW_SHOWNOACTIVATE : command);
}
#define SetForegroundWindow FixtureForeground
#define SetFocus FixtureFocus
#define ShowWindow FixtureShow
#include "../src/picker_window.cpp"
#undef ShowWindow
#undef SetFocus
#undef SetForegroundWindow

using namespace RuffnecKk::AutoPickup::Picker;
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
std::atomic<HCURSOR> settingsCursor{};
std::atomic<int> settingsCounter{-100};
LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        const auto& message = *reinterpret_cast<CWPRETSTRUCT*>(lp);
        if (message.message == WM_SETCURSOR || message.message == WM_NULL) {
            settingsCursor.store(GetCursor());
            const auto incremented = ShowCursor(TRUE);
            ShowCursor(FALSE);
            settingsCounter.store(incremented - 1);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
bool AwaitVisible(HWND hwnd, bool visible) {
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        if (!!IsWindowVisible(hwnd) == visible) return true;
        Sleep(1);
    }
    return false;
}
void Probe(HWND root, HWND target, int hit, LPCWSTR expected) {
    DWORD_PTR result{};
    // The fake game's thread deliberately does not pump. The settings thread
    // must still process mouse messages without synchronizing with that queue.
    CHECK(SendMessageTimeoutW(root, WM_SETCURSOR, reinterpret_cast<WPARAM>(target),
        MAKELPARAM(hit, WM_MOUSEMOVE), SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result));
    CHECK(result == TRUE);
    CHECK(settingsCursor.load() == LoadCursorW(nullptr, expected));
    CHECK(settingsCounter.load() >= 0);
    CHECK(GetCursor() == nullptr); // Settings must not change the game cursor.
}
}
int main() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = instance;
    cls.lpszClassName = L"Pickit.CursorIsolation.FakeGame";
    CHECK(RegisterClassW(&cls));
    const auto game = CreateWindowExW(0, cls.lpszClassName, L"Fake game", WS_OVERLAPPEDWINDOW,
        -30000, -30000, 1000, 800, nullptr, nullptr, instance, nullptr);
    CHECK(game);
    ShowWindow(game, SW_SHOWNOACTIVATE);
    const auto originalCursor = SetCursor(nullptr);
    int hiddenIncrements{};
    do { ++hiddenIncrements; } while (ShowCursor(FALSE) >= 0);
    Window window;
    CHECK(window.Start(nullptr, nullptr));
    const auto root = window.Handle();
    CHECK(root && GetWindowThreadProcessId(root, nullptr) != GetCurrentThreadId());
    // A thread-specific test observer reads the worker's cursor state. This is
    // neither a global input hook nor production code.
    const auto hook = SetWindowsHookExW(WH_CALLWNDPROCRET, Observe, nullptr, GetWindowThreadProcessId(root, nullptr));
    CHECK(hook);
    for (int cycle = 0; cycle < 3; ++cycle) {
        CHECK(window.Open());
        CHECK(AwaitVisible(root, true));
        CHECK(GetWindow(root, GW_OWNER) == nullptr); // After Open, not just creation.
        RECT gameRect{}, rootRect{};
        GetWindowRect(game, &gameRect); GetWindowRect(root, &rootRect);
        CHECK(rootRect.left >= gameRect.left && rootRect.right <= gameRect.right);
        Probe(root, root, HTCLIENT, IDC_ARROW);
        Probe(root, GetDlgItem(root, 101), HTCLIENT, IDC_IBEAM);
        Probe(root, GetDlgItem(root, 220), HTCLIENT, IDC_ARROW);
        Probe(root, root, HTBOTTOMRIGHT, IDC_SIZENWSE);
        Probe(root, root, HTCLIENT, IDC_ARROW);
        Sleep(50); // Idle, no mouse movement or cursor timer.
        SendMessageW(root, WM_NULL, 0, 0);
        CHECK(settingsCursor.load() == LoadCursorW(nullptr, IDC_ARROW));
        CHECK(settingsCounter.load() >= 0 && GetCursor() == nullptr);
        window.Close();
        CHECK(AwaitVisible(root, false));
        CHECK(GetWindow(root, GW_OWNER) == nullptr);
    }
    if (hook) UnhookWindowsHookEx(hook);
    window.Stop();
    CHECK(!IsWindow(root));
    CHECK(GetCursor() == nullptr);
    while (hiddenIncrements--) ShowCursor(TRUE);
    SetCursor(originalCursor);
    DestroyWindow(game); UnregisterClassW(cls.lpszClassName, instance);
    return failures ? 1 : 0;
}
