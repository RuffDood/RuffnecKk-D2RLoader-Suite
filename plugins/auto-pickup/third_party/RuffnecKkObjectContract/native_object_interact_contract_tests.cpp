#include "native_object_interact_contract.hpp"

#include <Psapi.h>

#include <charconv>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

using namespace RuffnecKk::MapSense::Detail;

namespace {
std::size_t Checks{};
std::size_t Failures{};

void Check(bool value, const char* expression, int line) {
    ++Checks;
    if (value) return;
    ++Failures;
    std::cerr << "FAIL " << line << ": " << expression << '\n';
}
#define CHECK(value) Check((value), #value, __LINE__)

void SetJump(std::vector<std::uint8_t>& bytes, std::size_t offset,
        std::uintptr_t from, std::uintptr_t to) {
    const auto distance = static_cast<std::int64_t>(to)
        - static_cast<std::int64_t>(from) - 5;
    CHECK(distance >= INT32_MIN && distance <= INT32_MAX);
    const auto encoded = static_cast<std::uint32_t>(distance);
    bytes[offset] = 0xE9U;
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + 1U + index] = static_cast<std::uint8_t>(encoded >> (index * 8U));
    }
}

struct Fixture {
    std::uintptr_t base{0x140000000ULL};
    std::map<std::uintptr_t, std::vector<std::uint8_t>> code;

    explicit Fixture(std::uintptr_t image = 0x140000000ULL) : base(image) {
        code[base + ObjectInteractGetterRva] = {
            ObjectInteractGetterBytes.begin(), ObjectInteractGetterBytes.end()};
        code[base + ObjectInteractSetterRva] = {
            ObjectInteractSetterBytes.begin(), ObjectInteractSetterBytes.end()};
    }
    void Extend(std::uintptr_t relay) {
        const auto install = [&](std::uintptr_t rva, std::size_t tail,
                auto prefix, std::uintptr_t target) {
            auto& body = code.at(base + rva);
            SetJump(body, tail, base + rva + tail, target);
            std::fill_n(body.begin() + tail + 5U, 3U, std::uint8_t{0x90U});
            std::vector<std::uint8_t> stub(prefix.begin(), prefix.end());
            stub.resize(prefix.size() + 5U);
            SetJump(stub, prefix.size(), target + prefix.size(), base + rva + tail + 8U);
            code[target] = std::move(stub);
        };
        install(ObjectInteractGetterRva, ObjectInteractGetterTail,
            ObjectInteractGetterRelay, relay);
        install(ObjectInteractSetterRva, ObjectInteractSetterTail,
            ObjectInteractSetterRelay, relay + 0x20U);
    }
    auto Validate() const -> ObjectInteractContract {
        return ValidateObjectInteractContract(base,
            [&](std::uintptr_t address, std::span<std::uint8_t> output) noexcept {
                const auto found = code.find(address);
                if (found == code.end() || output.size() != found->second.size()) return false;
                std::copy(found->second.begin(), found->second.end(), output.begin());
                return true;
            });
    }
};

void RejectEveryBitChange(Fixture fixture) {
    for (auto& [address, bytes] : fixture.code) {
        (void)address;
        for (auto& byte : bytes) {
            for (unsigned bit = 0; bit < 8U; ++bit) {
                const auto mask = static_cast<std::uint8_t>(1U << bit);
                byte ^= mask;
                CHECK(fixture.Validate() == ObjectInteractContract::Unsupported);
                byte ^= mask;
            }
        }
    }
}

void CheckExecutableReads() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto pageSize = static_cast<std::size_t>(system.dwPageSize);
    auto* pages = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,
        pageSize * 2U, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(pages != nullptr);
    if (pages == nullptr) return;
    std::copy(ObjectInteractGetterBytes.begin(), ObjectInteractGetterBytes.end(), pages);
    std::array<std::uint8_t, 72U> output{};
    const auto address = reinterpret_cast<std::uintptr_t>(pages);
    CHECK(!ReadObjectInteractCode(GetCurrentProcess(), address, output));
    DWORD previous{};
    CHECK(VirtualProtect(pages, pageSize, PAGE_EXECUTE_READ, &previous) != FALSE);
    CHECK(ReadObjectInteractCode(GetCurrentProcess(), address, output));
    CHECK(output == ObjectInteractGetterBytes);
    CHECK(!ReadObjectInteractCode(GetCurrentProcess(), address + pageSize - 1U, output));
    CHECK(VirtualProtect(pages, pageSize, PAGE_EXECUTE_READ | PAGE_GUARD, &previous) != FALSE);
    CHECK(!ReadObjectInteractCode(GetCurrentProcess(), address, output));
    CHECK(!ReadObjectInteractCode(GetCurrentProcess(), 0U, output));
    CHECK(!ReadObjectInteractCode(GetCurrentProcess(), address, {}));
    CHECK(!ReadObjectInteractCode(GetCurrentProcess(), UINTPTR_MAX - 10U, output));
    CHECK(VirtualFree(pages, 0, MEM_RELEASE) != FALSE);
}

// Explicit diagnostic mode: same compiled predicate and executable-page reader
// as the DLL, four bounded reads, no native invocation or process control.
auto ObserveProcess(std::string_view argument) -> int {
    DWORD processId{};
    const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), processId);
    if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size()
            || processId == 0U) return 2;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (process == nullptr) return 3;
    HMODULE module{};
    DWORD needed{};
    wchar_t name[MAX_PATH]{};
    if (!K32EnumProcessModules(process, &module, sizeof(module), &needed)
            || needed < sizeof(module) || module == nullptr
            || K32GetModuleBaseNameW(process, module, name, MAX_PATH) == 0U
            || (std::wstring_view(name) != L"D2RLoader.exe"
                && std::wstring_view(name) != L"D2R.exe")) {
        CloseHandle(process);
        return 4;
    }
    std::size_t reads{};
    std::size_t bytes{};
    const auto contract = ValidateObjectInteractContract(
        reinterpret_cast<std::uintptr_t>(module),
        [&](std::uintptr_t address, std::span<std::uint8_t> output) noexcept {
            ++reads;
            bytes += output.size();
            return ReadObjectInteractCode(process, address, output);
        });
    CloseHandle(process);
    std::cout << "pid=" << processId << " contract="
        << (contract == ObjectInteractContract::SplitByte16 ? "split-byte-16"
            : contract == ObjectInteractContract::NativeByte ? "native-byte" : "unsupported")
        << " reads=" << reads << " bytes=" << bytes << " writes=0 native-calls=0\n";
    return contract == ObjectInteractContract::Unsupported ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--observe-process") {
        return ObserveProcess(argv[2]);
    }
    Fixture vanilla;
    CHECK(vanilla.Validate() == ObjectInteractContract::NativeByte);
    RejectEveryBitChange(vanilla);
    Fixture extended;
    extended.Extend(0x143E2C5D0ULL);
    CHECK(extended.Validate() == ObjectInteractContract::SplitByte16);
    RejectEveryBitChange(extended);
    for (const auto base : {0x140000000ULL, 0x180000000ULL}) {
        Fixture relocated(base);
        relocated.Extend(base + 0x20000U); // Negative outward / positive return rel32.
        CHECK(relocated.Validate() == ObjectInteractContract::SplitByte16);
    }
    for (const auto rva : {ObjectInteractGetterRva, ObjectInteractSetterRva}) {
        auto partial = extended;
        partial.code[partial.base + rva] = vanilla.code.at(vanilla.base + rva);
        CHECK(partial.Validate() == ObjectInteractContract::Unsupported);
    }
    for (const auto relay : {0x143E2C5D0ULL, 0x143E2C5F0ULL}) {
        auto missing = extended;
        missing.code.erase(relay);
        CHECK(missing.Validate() == ObjectInteractContract::Unsupported);
        auto truncated = extended;
        truncated.code.at(relay).pop_back();
        CHECK(truncated.Validate() == ObjectInteractContract::Unsupported);
    }
    auto wrongReturn = extended;
    auto& getterRelay = wrongReturn.code.at(0x143E2C5D0ULL);
    SetJump(getterRelay, ObjectInteractGetterRelay.size(),
        0x143E2C5D0ULL + ObjectInteractGetterRelay.size(),
        extended.base + ObjectInteractGetterRva); // Valid address, wrong continuation.
    CHECK(wrongReturn.Validate() == ObjectInteractContract::Unsupported);
    std::size_t unexpectedReads{};
    CHECK(ValidateObjectInteractContract(UINTPTR_MAX,
        [&](std::uintptr_t, std::span<std::uint8_t>) noexcept {
            ++unexpectedReads;
            return true;
        }) == ObjectInteractContract::Unsupported);
    CHECK(unexpectedReads == 0U);
    CheckExecutableReads();
    std::cout << "Object interaction contract: checks=" << Checks
        << " failures=" << Failures << '\n';
    return Failures == 0U ? 0 : 1;
}
