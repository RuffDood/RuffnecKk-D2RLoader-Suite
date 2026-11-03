#include <RuffnecKk/ui_input_compat_windows.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace RuffnecKk::UiInputCompat;
namespace {
unsigned assertions{};
void Require(bool value, const char* label) {
    ++assertions;
    if (!value) throw std::runtime_error(label);
}
struct Segment {
    std::uintptr_t base, owner;
    bool executable{true}, readable{true};
    std::vector<std::uint8_t> bytes;
};
struct Memory {
    std::vector<Segment> segments;
    const Segment* find(std::uintptr_t at, std::size_t n) const noexcept {
        for (const auto& s : segments)
            if (at >= s.base && at - s.base <= s.bytes.size()
                && n <= s.bytes.size() - (at - s.base)) return &s;
        return nullptr;
    }
    bool read(std::uintptr_t at, void* out, std::size_t n) const noexcept {
        const auto* s = find(at, n);
        if (!s || !s->readable) return false;
        std::memcpy(out, s->bytes.data() + at - s->base, n);
        return true;
    }
    bool code(std::uintptr_t at, std::size_t n) const noexcept {
        const auto* s = find(at, n);
        return s && s->readable && s->executable;
    }
    bool owned(std::uintptr_t at, std::size_t n, std::uintptr_t owner) const noexcept {
        const auto* s = find(at, n);
        return s && s->readable && s->owner == owner;
    }
};
constexpr std::uintptr_t Game = 0x140000000, Provider = 0x180000000;
constexpr std::uintptr_t Relay = 0x13FFE0AD5, Trampoline = 0x13FFF0000;
template<class T> void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
Memory Stock() {
    return {{{Game + NativeRva, Game, true, true, {NativeBytes.begin(), NativeBytes.end()}}}};
}
Memory Hooked() {
    auto memory = Stock();
    auto& native = memory.segments[0].bytes;
    native[0] = 0xE9;
    Put(native, 1, static_cast<std::int32_t>(static_cast<std::int64_t>(Relay) -
        static_cast<std::int64_t>(Game + NativeRva + 5)));
    std::vector<std::uint8_t> relay{0xFF,0x25,0,0,0,0,0,0,0,0,0,0,0,0};
    Put(relay, 6, Provider + 0x14960);
    memory.segments.push_back({Relay, Relay, true, true, relay});
    memory.segments.push_back({Provider + 0x14960, Provider, true, true,
        {QuickUpgradeHook.begin(), QuickUpgradeHook.end()}});
    std::vector<std::uint8_t> slot(8);
    Put(slot, 0, Trampoline);
    memory.segments.push_back({Provider + 0x7D5E0, Provider, false, true, slot});
    Put(slot, 0, Game);
    memory.segments.push_back({Provider + 0x7D868, Provider, false, true, slot});
    std::vector<std::uint8_t> trampoline(21);
    std::copy_n(NativeBytes.begin(), 7, trampoline.begin());
    Put(trampoline, 3, static_cast<std::int32_t>(static_cast<std::int64_t>(Game + 0x3440170) -
        static_cast<std::int64_t>(Trampoline + 7)));
    trampoline[7] = 0xFF; trampoline[8] = 0x25;
    Put(trampoline, 13, Game + NativeRva + 7);
    memory.segments.push_back({Trampoline, Trampoline, true, true, trampoline});
    return memory;
}
void Run() {
    Require(Validate(Game, 0, Stock()), "unmodified helper rejected");
    Require(Validate(Game, Provider, Stock()), "stock with optional provider rejected");
    Require(!Validate(Game, 0, Hooked()), "unknown hook admitted without provider");
    Require(Validate(Game, Provider, Hooked()), "known Quick Upgrade chain rejected");
    for (std::size_t i = 0; i < NativeBytes.size(); ++i) {
        auto stock = Stock(); stock.segments[0].bytes[i] ^= 1;
        Require(!Validate(Game, 0, stock), "damaged stock byte admitted");
    }
    // Every byte of every contract witness, including the provider pointers.
    const auto fixture = Hooked();
    for (std::size_t s = 0; s < fixture.segments.size(); ++s)
        for (std::size_t i = 0; i < fixture.segments[s].bytes.size(); ++i) {
            auto damaged = fixture; damaged.segments[s].bytes[i] ^= 1;
            Require(!Validate(Game, Provider, damaged), "damaged hook-chain byte admitted");
        }
    for (std::size_t s = 0; s < fixture.segments.size(); ++s) {
        auto unreadable = fixture; unreadable.segments[s].readable = false;
        Require(!Validate(Game, Provider, unreadable), "unreadable witness admitted");
        if (fixture.segments[s].executable) {
            auto noncode = fixture; noncode.segments[s].executable = false;
            Require(!Validate(Game, Provider, noncode), "nonexecutable chain admitted");
        }
    }
    for (const auto index : {0U, 2U, 3U, 4U}) {
        auto wrongOwner = fixture; wrongOwner.segments[index].owner += 0x1000;
        Require(!Validate(Game, Provider, wrongOwner), "wrong allocation owner admitted");
    }
    Require(!Validate(0, Provider, fixture), "null game admitted");
    Require(!Validate(UINTPTR_MAX, Provider, fixture), "overflowing game admitted");
    Require(!Validate(Game, UINTPTR_MAX, fixture), "overflowing provider admitted");
    // Exercise the production Windows memory adapter, including inaccessible pages.
    ProcessMemory process;
    std::array<std::uint8_t, 4> output{};
    Require(!process.read(1, output.data(), output.size()), "unmapped process read succeeded");
    auto* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Require(page != nullptr, "fixture allocation failed");
    const auto address = reinterpret_cast<std::uintptr_t>(page);
    Require(process.owned(address, 4, address), "allocation ownership not recognized");
    Require(!process.code(address, 4), "data page treated as executable");
    DWORD old{};
    Require(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old) != FALSE, "fixture protection failed");
    Require(process.code(address, 4), "code page not recognized");
    Require(!process.code(address + 4095, 2), "cross-region code read admitted");
    Require(VirtualProtect(page, 4096, PAGE_NOACCESS, &old) != FALSE, "fixture protection failed");
    Require(!process.read(address, output.data(), 4), "inaccessible page read succeeded");
    Require(!process.code(address, 4), "inaccessible code admitted");
    Require(!process.owned(address, 4, address), "inaccessible owner admitted");
    VirtualFree(page, 0, MEM_RELEASE);
}
}
int main() {
    try { Run(); std::cout << assertions << " native compatibility assertions passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
