#pragma once
#include "item_picker.hpp"
#include "interact_editor.hpp"
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <memory>

namespace RuffnecKk::AutoPickup::Picker {
enum class WindowActionKind { Search, Filter, Choice, Save, Close, Toggle, Range, BulkChoice, Tab, InteractToggle, InteractNumber, PotionToggle };
struct WindowAction { WindowActionKind kind{}; std::uint32_t code{}; Choice choice{}; int value{};
    std::shared_ptr<const std::vector<std::uint32_t>> targets;
};
struct WindowRow { std::uint32_t code{}; std::wstring name, codeText, policy; Choice choice{}; bool checked{}; };
// Independent Win32 settings window. Its message thread owns every HWND; SDK/config work is
// handed back to the loader's UI thread by the action callback.
class Window {
public:
    using Handler = void(*)(void*, WindowAction) noexcept;
    auto Start(Handler callback, void* data) noexcept -> bool;
    void Stop() noexcept;
    auto Open() noexcept -> bool;
    void Close() noexcept;
    auto IsOpen() const noexcept -> bool { return visible.load(); }
    auto SearchText() const -> std::string;
    void Update(const std::vector<Entry>& entries, const Config& config, const std::string& source,
        const std::string& status, bool dirty, bool filtered, bool delegated = false,
        const Reimagined::InteractSettings::View& interact = {}, bool updateItems = true);
    auto Handle() const noexcept -> HWND { return window.load(); }
private:
    static DWORD WINAPI ThreadMain(void* data) noexcept;
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK ListProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) noexcept;
    auto Run() noexcept -> DWORD;
    auto Message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) -> LRESULT;
    void Emit(WindowAction action) noexcept;
    void Layout();
    void Refresh();
    void CheckRow(int index, UINT state);
    void Bulk(Choice choice);
    void Hide();
    void ShowTab();
    void Repaint();
    auto CursorFor(HWND target, int hit) const noexcept -> HCURSOR;
    HANDLE thread{}, ready{};
    std::atomic<HWND> window{};
    std::atomic<bool> visible{}, stopping{};
    std::atomic<Handler> handler{};
    void* userData{};
    HWND gameWindow{}, search{}, list{}, sourceLabel{}, countLabel{}, statusLabel{}, save{}, filter{}, close{};
    HWND pickResults{}, leaveResults{}, resetResults{};
    HWND tabs{}, ownership{};
    std::array<HWND, ToggleLabels.size()> toggles{};
    std::array<HWND, 2> rangeLabels{}, ranges{};
    std::array<HWND, Reimagined::InteractSettings::ToggleLabels.size()> interactToggles{};
    std::array<HWND, 3> interactLabels{}, interactNumbers{};
    HWND interactHelp{};
    std::array<HWND,15> potionToggles{};
    std::array<HWND,3> potionLabels{};
    HWND potionHelp{};
    Reimagined::InteractSettings::View pendingInteract, activeInteract;
    std::wstring currentSource, currentStatus;
    bool pickitDirty{};
    HFONT font{};
    UINT dpi{96};
    mutable std::mutex mutex;
    std::vector<WindowRow> pendingRows, rows;
    std::uint64_t pendingRowsRevision{}, rowsRevision{};
    std::wstring pendingSource, pendingStatus;
    std::string query;
    bool pendingDirty{}, pendingFiltered{}, updating{};
    bool pendingDelegated{};
    Config pendingConfig;
};
}
