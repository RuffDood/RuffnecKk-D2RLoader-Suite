#include "tracked_native_transform_compat.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int Failures{};

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++Failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

} // namespace

int main() {
    using namespace RuffnecKk::TrackedNativeTransform;
    using RuffnecKk::MapSense::Detail::EvaluateClientUnitHashLookup;
    using namespace RuffnecKk::MapSense::Detail;

    constexpr std::array<std::uint8_t, 41> original{
        0x48,0x63,0xC2,0x48,0x8B,0x04,0xC1,0x48,0x85,0xC0,0x74,
        0x1A,0x44,0x39,0x40,0x08,0x75,0x05,0x44,0x39,0x08,0x74,
        0x11,0x48,0x8B,0x88,0x58,0x01,0x00,0x00,0x48,0x8B,0xC1,
        0x48,0x85,0xC9,0xEB,0xE4,0x33,0xC0,0xC3};
    // Exact process capture, 2026-09-26: seven-byte entry and 22-byte guard.
    auto entry = original;
    const std::array<std::uint8_t, 7> capturedEntry{
        0xE9,0xEB,0xE8,0xD8,0x03,0x90,0x90};
    std::copy(capturedEntry.begin(), capturedEntry.end(), entry.begin());
    std::array<std::uint8_t, 22> body{
        0x41,0x83,0xF8,0xFF,0x0F,0x84,0x2C,0x17,0x27,0xFC,0x48,
        0x63,0xC2,0x48,0x8B,0x04,0xC1,0xE9,0x01,0x17,0x27,0xFC};
    const auto check = [&](std::uintptr_t rva, const std::uint8_t* bytes,
            std::size_t size) noexcept {
        if (rva >= ClientUnitHashLookupRva
                && rva - ClientUnitHashLookupRva <= entry.size()
                && size <= entry.size() - (rva - ClientUnitHashLookupRva)) {
            return std::equal(bytes, bytes + size,
                entry.data() + (rva - ClientUnitHashLookupRva));
        }
        return rva == BuiltInClientUnitHashLookupRva && size == body.size()
            && std::equal(bytes, bytes + size, body.data());
    };
    CHECK(ValidateBuiltInClientUnitHashLookup(original, check, true, true, true));
    CHECK(!ValidateBuiltInClientUnitHashLookup(original, check, false, true, true));
    CHECK(!ValidateBuiltInClientUnitHashLookup(original, check, true, false, true));
    CHECK(!ValidateBuiltInClientUnitHashLookup(original, check, true, true, false));
    // Every entry, tail, instruction and branch displacement byte is required.
    for (auto& byte : entry) {
        byte ^= 1U;
        CHECK(!ValidateBuiltInClientUnitHashLookup(original, check, true, true, true));
        byte ^= 1U;
    }
    for (auto& byte : body) {
        byte ^= 1U;
        CHECK(!ValidateBuiltInClientUnitHashLookup(original, check, true, true, true));
        byte ^= 1U;
    }
    entry = original;
    CHECK(!ValidateBuiltInClientUnitHashLookup(original, check, true, true, true));

    CHECK(EvaluateClientUnitHashLookup(
        {State::Unchanged, Kind::Unknown, 0U, {}, true, true}, true, true)
        == Admission::Pristine);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", true, true}, false, true)
        == Admission::TrackedCompatible);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Untracked, Kind::Unknown, 0U, {}, true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "another-plugin", true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 2U,
            "celestialrayone.engine-stability", true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::BytePatch, 1U,
            "celestialrayone.engine-stability", true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", false, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", true, false}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", true, true}, false, false)
        == Admission::Rejected);
    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
