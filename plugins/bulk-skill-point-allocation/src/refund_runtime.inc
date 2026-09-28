// Included inside the plugin implementation namespace. Native ABI witnesses
// are recorded in refund_native.hpp and the component development home.
Removal::Settings RefundSettings;
RuffnecKk::NativeStatCompat::Adapter RefundStats;
using ButtonInputFn = void(__fastcall*)(void*, const void*);
using ServerAllocateFn = int(__fastcall*)(void*, void*, const void*, int);
ButtonInputFn OriginalButtonInput{};
ServerAllocateFn OriginalServerAllocate{};

constexpr std::uint64_t InputHash(std::string_view text) noexcept {
    std::uint64_t value = 0xCBF29CE484222325;
    for (unsigned char c : text) value = (value ^ c) * 0x100000001B3;
    return value;
}
static_assert(InputHash("LeftMouseButtonDown") == 0xBDF8DAFF2E44666FULL);
template<class T> T ReadRefund(const void* object, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof(value));
    return value;
}

auto RefundClick(void* widget, const void* message, std::uint16_t& skill,
    std::uint16_t& action) noexcept -> bool {
    if (!widget || !message || !OriginalSendFiveBytePacket
        || ReadRefund<std::uint64_t>(message, 0) != InputHash("InputMessage")
        || ReadRefund<std::uint64_t>(widget, 0x5E0) != 0x290E587494892995ULL)
        return false;
    const auto command = ReadRefund<std::uint64_t>(message, 0x88);
    Removal::Button button{};
    if (command == InputHash("RightMouseButtonDown")) button = Removal::Button::Right;
    else if (command == InputHash("BindableMouseButtonDown")) {
        unsigned pressed = 0;
        for (const auto [key, candidate] : {
                std::pair{VK_MBUTTON, Removal::Button::Middle},
                std::pair{VK_XBUTTON1, Removal::Button::Mouse4},
                std::pair{VK_XBUTTON2, Removal::Button::Mouse5}}) {
            if (Win32KeyDown(key)) { ++pressed; button = candidate; }
        }
        if (pressed != 1) return false;
    } else return false;
    const unsigned modifiers = (Win32KeyDown(VK_CONTROL) ? 1U : 0U)
        | (Win32KeyDown(VK_SHIFT) ? 2U : 0U) | (Win32KeyDown(VK_MENU) ? 4U : 0U);
    const auto matched = Removal::Match(RefundSettings, button, modifiers);
    if (!matched) return false;
    if (At<bool(__fastcall*)()>(RefundNative::InputBlockedRva)()
        || !At<bool(__fastcall*)(void*)>(RefundNative::CurrentMouseWidgetRva)(widget))
        return false;
    const auto* pointer = ReadRefund<void*>(message, 0x110);
    const auto* payload = ReadRefund<void*>(widget, 0x668);
    if (!pointer || !payload
        || !At<bool(__fastcall*)(void*, std::uint64_t)>(RefundNative::HitTestRva)(
            widget, ReadRefund<std::uint64_t>(pointer, 4))) return false;
    const int id = ReadRefund<int>(payload, 0);
    if (id < 0 || id >= 0x8000) return false;
    skill = static_cast<std::uint16_t>(id);
    action = static_cast<std::uint16_t>(*matched);
    return true;
}

void __fastcall HookButtonInput(void* widget, const void* message) {
    std::uint16_t skill{}, action{};
    if (RefundClick(widget, message, skill, action)) {
        CancelPendingConfirmation();
        // The native server rejects ids outside its table. The marked id can
        // never become an ordinary allocation in an unmodified server.
        OriginalSendFiveBytePacket(0x3B, static_cast<std::uint16_t>(skill | 0x8000), action);
        At<void(__fastcall*)()>(RefundNative::ConsumeInputRva)();
        LogAllocation("refund-request", skill, action);
        return;
    }
    OriginalButtonInput(widget, message);
}

int InvestedRanks(void* player, int id) {
    auto* skill = At<void*(__fastcall*)(void*, int, int)>(RefundNative::SkillLookupRva)(player, id, -1);
    return skill ? ReadRefund<int>(skill, 0x40) : 0;
}

void ApplySkillRefund(void* game, void* player, int id, Removal::Action action) {
    if (!game || !player || ReadRefund<int>(player, 0) != 0) return;
    const int playerClass = ReadRefund<int>(player, 4);
    if (playerClass < 0 || playerClass > 7) return;
    const auto dataContext = ReadRefund<std::uint8_t>(game, 0x106);
    auto* tables = At<void*(__fastcall*)(std::uint8_t)>(RefundNative::DataTablesRva)(dataContext);
    // Reserve the upper bit only while it lies outside the actual skill table.
    if (!tables || ReadRefund<std::uint64_t>(tables, 0x11B8) >= 0x8000) return;
    const int count = At<int(__fastcall*)(std::uint8_t, int)>(RefundNative::ClassCountRva)(dataContext, playerClass);
    if (count <= 0 || count > 2048) return;
    std::vector<Removal::Skill> skills;
    skills.reserve(static_cast<std::size_t>(count));
    const void* selectedRow{};
    for (int index = 0; index < count; ++index) {
        const int current = At<int(__fastcall*)(std::uint8_t, int, int)>(RefundNative::ClassSkillRva)(dataContext, playerClass, index);
        if (current < 0 || static_cast<std::uint64_t>(current) >= ReadRefund<std::uint64_t>(tables, 0x11B8)) return;
        auto* row = At<void*(__fastcall*)(std::uint8_t, int)>(RefundNative::SkillRowRva)(dataContext, current);
        if (!row) return;
        if (current == id) selectedRow = row;
        skills.push_back({current, InvestedRanks(player, current), {
            ReadRefund<std::int16_t>(row, 0x21E), ReadRefund<std::int16_t>(row, 0x220),
            ReadRefund<std::int16_t>(row, 0x222)}});
    }
    const auto plan = Removal::MakePlan(skills, id, action, RefundSettings.batchPoints);
    if (!plan || !selectedRow || plan->expectedRanks > 255) return;
    // Native allocation's -1 sentinel explicitly means one point per rank.
    // An arbitrary mod expression is not a recoverable historical spend ledger.
    if (ReadRefund<std::int32_t>(selectedRow, 0x210) != -1) {
        Context->LogWarn("BulkSkillPointAllocation: refund refused for a custom skill cost; no ranks or points changed.");
        return;
    }
    const int before = RefundStats.GetUnitBaseStat(player, 5, 0);
    if (!Removal::CreditPool(before, plan->removedRanks)) return;
    auto* client = At<void*(__fastcall*)(void*)>(RefundNative::GetClientRva)(player);
    if (!client) return;
    // This runs synchronously on the native allocation packet's game thread;
    // the complete prerequisite snapshot is current before the first mutation.
    At<void(__fastcall*)(void*, int, int, int)>(RefundNative::SetSkillRva)(player, id, plan->remainingRanks, 1);
    if (InvestedRanks(player, id) != plan->remainingRanks) {
        Context->LogError("BulkSkillPointAllocation: native skill setter did not reach its requested rank; no refund credited.");
        return;
    }
    At<void(__fastcall*)(void*, void*, std::uint16_t, std::uint8_t, int)>(RefundNative::SendSkillRva)(
        client, player, static_cast<std::uint16_t>(id), static_cast<std::uint8_t>(plan->remainingRanks), 1);
    RefundStats.AddUnitStat(player, 5, plan->removedRanks, 0);
    At<void(__fastcall*)(void*, void*)>(RefundNative::RefreshStatsRva)(game, player);
    At<void(__fastcall*)(void*, void*, int)>(RefundNative::RefreshSkillsRva)(game, player, 1);
    if (ActiveSettings.diagnostics) {
        char log[240]{};
        std::snprintf(log, sizeof(log), "BulkSkillPointAllocation: refund skill=%d rank=%d->%d points=%d->%d.",
            id, plan->expectedRanks, plan->remainingRanks, before, RefundStats.GetUnitBaseStat(player, 5, 0));
        Context->LogInfo(log);
    }
}

int __fastcall HookServerAllocate(void* game, void* player, const void* packet, int size) {
    if (!packet || size != 5 || ReadRefund<std::uint8_t>(packet, 0) != 0x3B)
        return OriginalServerAllocate(game, player, packet, size);
    const auto encoded = ReadRefund<std::uint16_t>(packet, 1);
    if ((encoded & 0x8000) == 0) return OriginalServerAllocate(game, player, packet, size);
    const auto action = ReadRefund<std::uint16_t>(packet, 3);
    if (action > static_cast<std::uint16_t>(Removal::Action::All)) return 0;
    try { ApplySkillRefund(game, player, encoded & 0x7FFF, static_cast<Removal::Action>(action)); }
    catch (...) { Context->LogError("BulkSkillPointAllocation: refund processing failed."); }
    return 0;
}

bool ValidateRefundNative() {
    if (!RefundNative::Validate([](auto rva, const auto& bytes, const char* label) {
            return CheckBytes(rva, bytes, label);
        })) return false;
    if (ReadRefund<std::uintptr_t>(Base, 0x1D2A968) != reinterpret_cast<std::uintptr_t>(Base + RefundNative::ServerAllocateRva)) {
        Context->LogError("BulkSkillPointAllocation: server skill packet ownership differs.");
        return false;
    }
    using namespace RuffnecKk::NativeStatCompat;
    if (!RefundStats.BindCurrentProcess(reinterpret_cast<std::uintptr_t>(Base),
            ToMask(Helper::GetUnitBaseStat) | ToMask(Helper::AddUnitStat))) {
        Context->LogError("BulkSkillPointAllocation: skill point stat helpers could not be admitted.");
        return false;
    }
    return true;
}

bool InstallRefundHooks() {
    return Context->InstallInlineHook(RefundNative::ServerAllocateRva, RefundNative::ServerAllocateBytes.data(),
        static_cast<std::uint32_t>(RefundNative::ServerAllocateBytes.size()), HookServerAllocate, &OriginalServerAllocate)
        && Context->InstallInlineHook(RefundNative::ButtonInputRva, RefundNative::ButtonInputBytes.data(),
            static_cast<std::uint32_t>(RefundNative::ButtonInputBytes.size()), HookButtonInput, &OriginalButtonInput);
}
