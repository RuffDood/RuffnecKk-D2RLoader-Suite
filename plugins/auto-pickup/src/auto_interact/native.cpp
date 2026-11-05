#include "reimagined_stat_compat.hpp"
// Native object semantics reviewed from the user's read-only 93847 capture.
// No native object/player pointer survives a synchronous scan or observer call.
#include "native.hpp"
#include "native_contract.hpp"
#include <Windows.h>
#include "../../third_party/RuffnecKkObjectContract/native_object_interact_contract.hpp"
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <shellapi.h>

namespace AutoInteract {
namespace {
namespace C = NativeContract;
std::atomic<Native*> Observer{};
PlayerActionFn OriginalAction{};
auto __fastcall ObserveAction(void* game, void* player, void* skill, std::uint32_t mode,
        std::uint32_t type, std::uint32_t id, std::uint32_t a7, std::uint32_t a8) -> std::int32_t {
    // This observer never redirects an action or attempts an object interaction.
    // Forward every argument and the original result unchanged, exactly once.
    const auto result = OriginalAction(game, player, skill, mode, type, id, a7, a8);
    if (auto* native = Observer.load()) native->Observe(game, player);
    return result;
}
template<class F> auto At(std::uintptr_t base, std::uint32_t rva) noexcept -> F { return reinterpret_cast<F>(base + rva); }
auto SnapshotPort(void* self, std::uint32_t player, std::span<Object> out, std::size_t& count) noexcept -> bool {
    return static_cast<Native*>(self)->Snapshot(player, out, count);
}
auto RequestPort(void* self, Identity id, const Config& config) noexcept -> bool { return static_cast<Native*>(self)->Request(id, config); }
void SessionPort(void* self, const D2RL::Lifecycle::GameplayEvent& event) noexcept { static_cast<Native*>(self)->Session(event); }
}
auto Native::Port() noexcept -> NativePort { return {this, SnapshotPort, RequestPort, SessionPort}; }
auto Native::State() noexcept -> SessionState { std::lock_guard lock(mutex_); return state_; }
void Native::Remember(SessionState old, std::uintptr_t game) noexcept {
    std::lock_guard lock(mutex_);
    if (active_.load() && old.epoch == state_.epoch && old.player == state_.player) state_.game = game;
}
void Native::Session(const D2RL::Lifecycle::GameplayEvent& e) noexcept {
    std::lock_guard lock(mutex_);
    using K = D2RL::Lifecycle::GameplayEventKind;
    if (e.kind == K::LocalPlayerReady) {
        if (state_.generation == e.sessionGeneration && state_.player == e.playerId) return;
        state_ = {0, e.playerId, e.sessionGeneration, state_.epoch + 1};
    } else if (e.kind == K::GameJoined || e.kind == K::GameLeft) {
        state_ = {0, UINT32_MAX, e.sessionGeneration, state_.epoch + 1};
    }
}
void Native::Observe(void* game, void* player) noexcept {
    if (!active_.load() || !game || !player) return;
    const auto state = State();
    if (state.player == UINT32_MAX || state.game) return; // One actor capture per session, not native work on every action.
    __try {
        if (b_.type(player) != 0 || b_.id(player) != state.player || b_.game(player) != game
            || b_.lookup(game, 0, state.player) != player) return;
        Remember(state, reinterpret_cast<std::uintptr_t>(game));
        observations_.fetch_add(1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
auto Native::Resolve(std::uint32_t player) noexcept -> Actor {
    const auto state = State();
    if (!active_.load() || !state.game || player != state.player) return {};
    auto* game = reinterpret_cast<void*>(state.game);
    auto* unit = b_.lookup(game, 0, player);
    if (!unit || b_.type(unit) != 0 || b_.id(unit) != player || b_.game(unit) != game) return {};
    return {game, unit, player, state.epoch};
}
auto Native::PlayerCanInteract(void* player) noexcept -> bool {
    if (!player || !PlayerReady(b_.mode(player)) || b_.dead(player)) return false;
    auto* inventory = b_.inventory(player);
    return inventory && !b_.cursor(inventory);
}
auto Native::Quantity(void* self, void* item) noexcept -> std::int32_t {
    return static_cast<Native*>(self)->stats_.GetUnitStat(item, 70, 0);
}
auto Native::HasKeys(void* player) noexcept -> bool {
    // The native chest opener exempts Assassin class 6. Every other player
    // needs a carried stored key with positive quantity; never consume it here.
    if (*reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(player) + 4) == 6) return true;
    if (!b_.quantity) return false;
    auto* inventory = b_.inventory(player);
    if (!inventory || b_.cursor(inventory)) return false;
    std::size_t visited{};
    for (void* item = b_.firstItem(inventory); item; item = b_.nextItem(item)) {
        if (++visited > 4096) return false;
        if (b_.type(item) == 4 && b_.mode(item) == 0 && b_.itemType(item, 41) && b_.itemPage(item) == 0
            && b_.quantity(this, item) > 0) return true;
    }
    return false;
}
auto Native::ObjectKey(const std::uint8_t* record, std::uint32_t id, std::uint8_t bank, std::uint32_t count) noexcept -> std::string_view {
    const auto key = catalogs_[bank == 1 ? 0 : 1].Key(id, count, {record, 0x168});
    if (!key.empty()) return key;
    // Compatibility with actual ASCII rows. A numeric linker ID is not text.
    const auto* end = static_cast<const char*>(std::memchr(record, 0, 64));
    if (!end) return {};
    const std::string_view ascii(reinterpret_cast<const char*>(record), static_cast<std::size_t>(end - reinterpret_cast<const char*>(record)));
    return ascii.size() >= 3 && ObjectCatalog::SafeKey(ascii) ? ascii : std::string_view{};
}

void Native::LoadCatalogs() noexcept {
    catalogs_ = {};
    try {
        namespace fs = std::filesystem;
        int argc{}; auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc); bool preferTxt{};
        if (argv) { for (int i = 1; i < argc; ++i) if (_wcsicmp(argv[i], L"-txt") == 0) preferTxt = true; LocalFree(argv); }
        std::array<wchar_t, 32768> exe{}; const auto length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (!length || length >= exe.size()) return;
        const auto gameRoot = fs::path(exe.data()).parent_path();
        std::vector<fs::path> active, vanilla;
        if (context_->activeMod && *context_->activeMod) {
            const std::string token = context_->activeMod;
            if (!context_->modDirectory || token.size() > 128 || token.find_first_of("/\\:") != std::string::npos || token == "." || token == "..") return;
            const fs::path mod(context_->modDirectory);
            active.push_back(mod / L"data/global/excel");
            auto package = fs::path(std::u8string(reinterpret_cast<const char8_t*>(token.data()), token.size())); if (package.extension() != L".mpq") package += L".mpq";
            active.push_back(mod / package / L"data/global/excel");
        }
        std::string version = context_->buildVersion ? context_->buildVersion : "";
        bool safe = !version.empty() && version.size() <= 32;
        for (const char c : version) if ((c < '0' || c > '9') && c != '.') safe = false;
        if (safe) vanilla.push_back(gameRoot / L"d2rloader/data/compiler" / fs::path(version) / L"data/global/excel");
        for (std::size_t bank = 0; bank < catalogs_.size(); ++bank) {
            auto paths = active;
            if (bank == 0) for (auto& p : paths) p /= L"base";
            bool found{}; std::string bytes;
            auto read = [&](const std::vector<fs::path>& dirs, bool baseline) {
                for (const auto& dir : dirs) {
                    const auto folder = baseline && bank == 0 ? dir / L"base" : dir;
                    const auto txt = folder / L"objects.txt", bin = folder / L"objects.bin";
                    if (fs::exists(bin) && !preferTxt && !baseline) { found = true; bytes.clear(); return; }
                    if (!fs::exists(txt)) continue;
                    const auto size = fs::file_size(txt); if (size > 8 * 1024 * 1024) { found = true; bytes.clear(); return; }
                    std::ifstream file(txt, std::ios::binary); std::string text(static_cast<std::size_t>(size), '\0');
                    if (!file.read(text.data(), static_cast<std::streamsize>(size))) { found = true; bytes.clear(); return; }
                    if (found && text != bytes) { bytes.clear(); return; }
                    bytes = std::move(text); found = true;
                }
            };
            // Packed mod data may override vanilla even when no loose TXT is
            // visible. Do not infer shared-callback Class keys from vanilla.
            read(paths, false); if (!found && active.empty()) read(vanilla, true);
            std::string error;
            if (!found || bytes.empty() || !catalogs_[bank].Parse(bytes, error)) {
                context_->LogWarn("Auto Interact: source Class catalog unavailable; shared-callback objects require verified source keys.");
            }
        }
        char message[192]{};
        std::snprintf(message, sizeof(message), "Auto Interact: source Class catalogs classic=%zu; expansion=%zu; each key requires matching active row count and semantics.", catalogs_[0].Size(), catalogs_[1].Size());
        context_->LogInfo(message);
    } catch (...) { catalogs_ = {}; context_->LogWarn("Auto Interact: source Class catalog could not be loaded; unknown keys excluded."); }
}

auto Native::ReadObject(Actor actor, void* unit, Object& out, const Config& config) noexcept -> bool {
    if (!unit || b_.type(unit) != 2) return false;
    const auto klass = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(unit) + 4);
    const auto bank = *(static_cast<const std::uint8_t*>(actor.game) + 0x106);
    const auto count = b_.count(bank);
    if (count <= 0 || count > 1'000'000 || klass >= static_cast<std::uint32_t>(count)) return false;
    auto* record = static_cast<const std::uint8_t*>(b_.record(unit));
    if (!record || record != b_.row(bank, klass)) return false;
    const auto key = ObjectKey(record, klass, bank, static_cast<std::uint32_t>(count));
    // The actual compiler descriptors use separate BYTEs for InitFn,
    // PopulateFn, OperateFn, ClientFn. Do not read overlapping uint16 fields.
    const Semantics semantics{key, record[0x15C], record[0x15E], record[0x127],
        record[0x85] != 0, record[0xFA] != 0, true};
    const auto kind = Classify(semantics);
    if (kind == Kind::Excluded) return false;
    auto* objectData = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(unit) + 0x10);
    if (!objectData) return false;
    // Both native and the loader's expanded getter preserve this low byte.
    // Its layout is witnessed independently by the compiled chest handler.
    const auto interact = objectData[8];
    out = {{actor.id, b_.id(unit), klass}, kind, b_.mode(unit), -1, false, true,
        kind == Kind::Chest && semantics.operate == 4 && (interact & 0x80) != 0, false, (interact & 0x7F) < 10};
    if (out.mode != 0 || !Selected(kind, config)) return true; // Completion observations need no distance/reach/collision work.
    out.targetable = b_.selectable(unit) != 0;
    if (!out.targetable || !out.validTrap) return true;
    out.distance = b_.distance(actor.player, unit);
    const auto maximum = config.interactRange ? config.interactRange : 50U; // Native dispatcher's own backstop.
    if (out.distance < 0 || static_cast<std::uint32_t>(out.distance) > maximum) return true;
    // The normal barrel callback starts a kick action. Keep its native reach
    // guard so extended range never asks that skill path to move toward it.
    const bool needsReach = !config.interactRange || semantics.operate == 5;
    out.inReach = needsReach && b_.reach(actor.player, unit) != 0;
    if (needsReach && !out.inReach) return true;
    out.blocked = b_.collision(actor.player, unit, 0x804) != 0;
    return true;
}
auto Native::Snapshot(std::uint32_t player, std::span<Object> out, std::size_t& count) noexcept -> bool {
    count = 0;
    __try {
        const auto actor = Resolve(player);
        if (!actor.player || !PlayerCanInteract(actor.player)) return true;
        auto* room = b_.room(actor.player);
        if (!room) return true;
        void** rooms{}; std::uint32_t roomCount{};
        b_.neighbours(room, &rooms, &roomCount);
        if (!rooms || !roomCount || roomCount > 64) return false;
        bool checkedKeys{}, keys{};
        std::size_t visited{};
        for (std::uint32_t r = 0; r < roomCount; ++r) {
            if (!rooms[r]) continue;
            for (void* unit = b_.first(rooms[r]); unit; unit = b_.next(unit)) {
                if (++visited > 16384) return false;
                Object candidate;
                if (!ReadObject(actor, unit, candidate, config_)) continue;
                if (candidate.locked && config_.lockedChests && !candidate.blocked) {
                    if (!checkedKeys) { keys = HasKeys(actor.player); checkedKeys = true; }
                    candidate.keysAvailable = keys;
                }
                if (count == out.size()) return false;
                out[count++] = candidate;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { count = 0; return false; }
}
auto Native::Request(Identity id, const Config& config) noexcept -> bool {
    __try {
        const auto actor = Resolve(id.player);
        if (!actor.player || !PlayerCanInteract(actor.player)) return false;
        auto* unit = b_.lookup(actor.game, 2, id.object);
        Object current;
        if (!ReadObject(actor, unit, current, config) || current.id != id) return false;
        if (current.locked && config.lockedChests && !current.blocked) current.keysAvailable = HasKeys(actor.player);
        if (!Engine::Eligible(current, config)) return false;
        char key[64]{};
        const auto name = ObjectKey(static_cast<const std::uint8_t*>(b_.record(unit)), id.objectClass,
            *(static_cast<const std::uint8_t*>(actor.game) + 0x106), static_cast<std::uint32_t>(b_.count(*(static_cast<const std::uint8_t*>(actor.game) + 0x106))));
        if (!name.empty()) std::memcpy(key, name.data(), name.size());
        // Ordinary authoritative operation dispatcher: no cursor item (-1),
        // full collision check. It creates the native operation structure and
        // invokes the admitted callback; it has no auto-walking fallback.
        const auto result = b_.operate(actor.game, actor.player, id.object, UINT32_MAX, true);
        const auto n = loggedRequests_.fetch_add(1) + 1;
        if (context_ && config.diagnostics && (n <= 8 || n % 100 == 0)) {
            // Reacquire after dispatch before observing the resulting state.
            auto* after = b_.lookup(actor.game, 2, id.object);
            const auto mode = after && b_.type(after) == 2
                && *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(after) + 4) == id.objectClass
                ? b_.mode(after) : UINT32_MAX;
            char message[320]{};
            std::snprintf(message, sizeof(message), "Auto Interact: native attempt object=%u; class=%u; class-key=%s; kind=%u; distance=%d; result=%d; mode-after=%u.",
                id.object, id.objectClass, key, static_cast<unsigned>(current.kind), current.distance, result, mode);
            context_->LogInfo(message);
        }
        return result == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Native::Stop() noexcept {
    active_.store(false);
    Observer.store(nullptr);
    { std::lock_guard lock(mutex_); state_ = {0, UINT32_MAX, 0, state_.epoch + 1}; }
    // Leave the trampoline intact while the SDK restores the context-owned hook.
}
auto Native::Admit(const D2RL::PluginContext* context, const Config& config) noexcept -> bool {
    Stop(); context_ = context; config_ = config; base_ = context ? context->exeBase : 0;
    stats_.Reset();
    if (!base_ || !context_) return false;
    LoadCatalogs();
    for (const auto& witness : C::Witnesses) {
        if (!config.lockedChests && (witness.rva == C::ItemTypeRva || witness.rva == C::ItemPageRva
            || witness.rva == C::FirstInventoryItemRva || witness.rva == C::NextInventoryItemRva)) continue;
        if (!context_->CheckExpectedBytes(witness.rva, witness.bytes.data(), static_cast<std::uint32_t>(witness.bytes.size()))) {
            char message[160]{};
            std::snprintf(message, sizeof(message), "Auto Interact: native code mismatch at RVA 0x%X; no observer installed.", witness.rva);
            context_->LogError(message); return false;
        }
    }
    for (auto operation : C::Operations) {
        const std::uint64_t expected = base_ + operation.rva;
        if (!context_->CheckExpectedBytes(0x23A51C0 + operation.index * 8, &expected, sizeof(expected))) {
            context_->LogError("Auto Interact: object-operation table changed; no observer installed."); return false;
        }
    }
    namespace ObjectContract = RuffnecKk::MapSense::Detail;
    const auto contract = ObjectContract::ValidateObjectInteractContract(base_, [](std::uintptr_t address, std::span<std::uint8_t> bytes) noexcept {
        return ObjectContract::ReadObjectInteractCode(GetCurrentProcess(), address, bytes);
    });
    if (contract == ObjectContract::ObjectInteractContract::Unsupported) {
        context_->LogError("Auto Interact: object lock/trap accessor contract unsupported; no observer installed."); return false;
    }
    if (config.lockedChests && !stats_.BindCurrentProcess(base_, RuffnecKk::NativeStatCompat::ToMask(RuffnecKk::NativeStatCompat::Helper::GetUnitStat),
            RuffnecKk::AutoPickup::Compat::StatRedirectProof())) {
        context_->LogError("Auto Interact: key quantity reader unsupported; no observer installed."); return false;
    }
    b_.room = At<NativeBindings::Unit>(base_, C::GetUnitRoomRva);
    b_.first = At<NativeBindings::Unit>(base_, C::RoomFirstUnitRva); b_.next = At<NativeBindings::Unit>(base_, C::NextUnitRva);
    b_.game = At<NativeBindings::Unit>(base_, C::GetGameFromUnitRva); b_.record = At<NativeBindings::Unit>(base_, C::ObjectRecordRva);
    b_.inventory = At<NativeBindings::Unit>(base_, C::InventoryRva); b_.cursor = At<NativeBindings::Unit>(base_, C::CursorItemRva);
    b_.firstItem = At<NativeBindings::Unit>(base_, C::FirstInventoryItemRva); b_.nextItem = At<NativeBindings::Unit>(base_, C::NextInventoryItemRva);
    b_.type = At<NativeBindings::Number>(base_, C::UnitTypeRva); b_.id = At<NativeBindings::Number>(base_, C::UnitIdRva);
    b_.mode = At<NativeBindings::Number>(base_, C::UnitModeRva); b_.dead = At<NativeBindings::Number>(base_, C::IsDeadRva);
    b_.selectable = At<NativeBindings::Number>(base_, C::ObjectSelectableRva); b_.lookup = At<NativeBindings::Lookup>(base_, C::ServerUnitRva);
    b_.neighbours = At<decltype(b_.neighbours)>(base_, C::RoomNeighboursRva);
    b_.count = At<decltype(b_.count)>(base_, C::ObjectsCountRva); b_.row = At<decltype(b_.row)>(base_, C::ObjectsRecordRva);
    b_.distance = At<NativeBindings::Pair>(base_, C::UnitDistanceRva); b_.reach = At<NativeBindings::Pair>(base_, C::ObjectReachRva);
    b_.collision = At<decltype(b_.collision)>(base_, C::UnitCollisionRva); b_.operate = At<decltype(b_.operate)>(base_, C::ObjectDispatcherRva);
    b_.itemType = At<decltype(b_.itemType)>(base_, C::ItemTypeRva); b_.itemPage = At<decltype(b_.itemPage)>(base_, C::ItemPageRva);
    b_.quantity = config.lockedChests ? Quantity : nullptr;
    return true;
}
auto Native::PrepareSettings(const Config& config) noexcept -> bool {
    if (config.lockedChests && !b_.quantity) {
        for (const auto& w : C::Witnesses) if (w.rva == C::ItemTypeRva || w.rva == C::ItemPageRva
            || w.rva == C::FirstInventoryItemRva || w.rva == C::NextInventoryItemRva) {
            if (!context_->CheckExpectedBytes(w.rva, w.bytes.data(), static_cast<std::uint32_t>(w.bytes.size()))) return false;
        }
        if (!stats_.BindCurrentProcess(base_, RuffnecKk::NativeStatCompat::ToMask(RuffnecKk::NativeStatCompat::Helper::GetUnitStat),
            RuffnecKk::AutoPickup::Compat::StatRedirectProof())) return false;
        b_.quantity = Quantity;
    }
    return true;
}

auto Native::Activate() noexcept -> bool {
    if (!context_ || config_.validateOnly || !config_.enabled) return false;
    Observer.store(this); active_.store(true);
    if (!context_->InstallInlineHook(D2RL::MakeInlineHook(C::PlayerActionRva,
            C::PlayerActionBytes.data(), static_cast<std::uint32_t>(C::PlayerActionBytes.size()),
            reinterpret_cast<void*>(&ObserveAction), reinterpret_cast<void**>(&OriginalAction))) || !OriginalAction) {
        Stop(); return false;
    }
    return true;
}
}
