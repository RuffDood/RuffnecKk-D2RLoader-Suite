#include <RuffnecKk/native_stat_compat.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {
namespace Stat = RuffnecKk::NativeStatCompat;
struct Image final { std::uintptr_t base; std::array<std::byte, 1024> bytes{}; };
struct Images final { Image main{0x1000}; Image core{0x4000}; bool unwind{true}; };
auto ReadImage(const Image& image, std::uintptr_t address, std::byte* output, std::size_t size) noexcept -> bool {
    if (address < image.base || size > image.bytes.size() || address - image.base > image.bytes.size() - size) return false;
    for (std::size_t index = 0; index < size; ++index) output[index] = image.bytes[address - image.base + index];
    return true;
}
auto Read(void* userData, std::uintptr_t address, std::byte* output, std::size_t size) noexcept -> bool {
    const auto& images = *static_cast<const Images*>(userData);
    return ReadImage(images.main, address, output, size) || ReadImage(images.core, address, output, size);
}
auto ValidateUnwind(void* userData, std::uintptr_t, std::uint32_t, std::size_t, std::uint32_t, bool) noexcept -> bool { return static_cast<const Images*>(userData)->unwind; }
auto ValidateExecutable(void*, std::uintptr_t) noexcept -> bool { return true; }
auto ValidateNotExecutable(void*, std::uintptr_t) noexcept -> bool { return false; }
auto ValidateRedirect(void* userData, std::uintptr_t mainBase, std::uint32_t nativeRva,
        std::uintptr_t slot, std::uintptr_t actual, std::uintptr_t expected) noexcept -> bool {
    return *static_cast<bool*>(userData) && mainBase == 0x1000 && nativeRva == 0x10
        && slot == 0x1080 && actual == 0xBEEF && expected == 0x4100;
}
void Put64(Image& image, std::size_t offset, std::uint64_t value) { std::memcpy(image.bytes.data() + offset, &value, sizeof(value)); }
struct CanonicalCapture final { std::uintptr_t base{0x80000000}; std::vector<std::byte> bytes{0x340000}; };
auto ReadCapture(void* userData, std::uintptr_t address, std::byte* output, std::size_t size) noexcept -> bool {
    const auto& capture = *static_cast<const CanonicalCapture*>(userData);
    if (address < capture.base || size > capture.bytes.size() || address - capture.base > capture.bytes.size() - size) return false;
    std::memcpy(output, capture.bytes.data() + address - capture.base, size); return true;
}
auto Nibble(char c) noexcept -> std::uint8_t { return c <= '9' ? static_cast<std::uint8_t>(c - '0') : static_cast<std::uint8_t>((c | 0x20) - 'a' + 10); }
void PopulateHex(CanonicalCapture& capture, std::uint32_t rva, std::string_view hex) {
    for (std::size_t i = 0; i < hex.size() / 2; ++i) capture.bytes[rva + i] = std::byte{static_cast<std::uint8_t>((Nibble(hex[i * 2]) << 4) | Nibble(hex[i * 2 + 1]))};
}
std::uint16_t legacyLayer{};
std::uint32_t wideLayer{};
void __fastcall LegacySet(void*, std::int32_t, std::int32_t, std::uint16_t layer) noexcept { legacyLayer = layer; }
auto __fastcall WideSet(void*, std::int32_t, std::int32_t, std::uint32_t layer) noexcept -> std::int32_t { wideLayer = layer; return 0; }
auto __fastcall Alignment(void*) noexcept -> std::int32_t { return 73; }
struct DispatchReader final { std::uintptr_t main; std::uintptr_t core; };
auto ReadCanonicalDispatch(void*, std::uintptr_t, std::byte* output, std::size_t size) noexcept -> bool {
    if (size != 2) return false; output[0] = std::byte{0xAA}; output[1] = std::byte{0xBB}; return true;
}
auto ReadProviderDispatch(void* userData, std::uintptr_t address, std::byte* output, std::size_t size) noexcept -> bool {
    const auto& reader = *static_cast<const DispatchReader*>(userData);
    if (address == reader.main && (size == 2 || size == 6)) {
        output[0] = std::byte{0xFF}; output[1] = std::byte{0x25}; output[2] = std::byte{0x0A};
        for (std::size_t i = 3; i < size; ++i) output[i] = std::byte{0}; return true;
    }
    if (address == reader.main + 6 && size == 4) { for (std::size_t i = 0; i < size; ++i) output[i] = std::byte{0x90}; return true; }
    if ((address == reader.main + 6 || address == reader.main + 10) && size == 1) { output[0] = std::byte{0xCC}; return true; }
    if (address == reader.main + 16 && size == sizeof(std::uintptr_t)) { std::memcpy(output, &reader.core, size); return true; }
    if (address == reader.core + 0x20 && size == 24) {
        const std::array<std::uint64_t, 3> descriptor{{reader.main, 0, reader.main}};
        std::memcpy(output, descriptor.data(), size); return true;
    }
    return false;
}

constexpr std::array<Stat::HelperWitness, 7> CanonicalHelpers{{
    {"get", 0x10, "AABB", 6, "", "", 0, 0, {}, {}}, {"add", 0x20, "AABB", 6, "", "", 0, 0, {}, {}},
    {"merge", 0x30, "AABB", 6, "", "", 0, 0, {}, {}}, {"critical", 0x40, "AABB", 6, "", "", 0, 0, {}, {}},
    {"base", 0x50, "AABB", 6, "", "", 0, 0, {}, {}}, {"set", 0x60, "AABB", 6, "", "", 0, 0, {}, {}},
    {"alignment", 0x70, "AABB", 6, "", "", 0, 0, {}, {}},
}};
constexpr Stat::AdmissionContract CanonicalContract{"synthetic", CanonicalHelpers};
constexpr std::array<Stat::DirectCallWitness, 1> Direct{{{0x100, 0x110}}};
constexpr std::array<Stat::DirectCallWitness, 1> Jump32{{{0x130, 0x140}}};
constexpr std::array<Stat::DirectCallWitness, 1> Jump8{{{0x140, 0x170}}};
constexpr std::array<Stat::IndirectCallWitness, 1> Indirect{{{0x120, 0x180}}};
constexpr std::array<Stat::ReadOnlyWitness, 1> ReadOnly{{{0x160, "CAFE"}}};
constexpr std::array<Stat::FunctionWitness, 4> Functions{{
    {0x100, "E80B0000009090909090", {0x150, "0102"}, Direct, {}, ReadOnly, 5},
    {0x120, "FF155A000000", {0, ""}, {}, Indirect, {}},
    {0x130, "E90B000000", {0, ""}, Jump32, {}, {}},
    {0x140, "EB2E", {0, ""}, Jump8, {}, {}},
}};
constexpr std::array<Stat::FunctionWitness, 4> TrackedFunctions{{
    {0x100, "E80B000000", {0x150, "0102"}, {}, {}, ReadOnly},
    {0x120, "FF155A000000", {0, ""}, {}, Indirect, {}},
    {0x130, "E90B000000", {0, ""}, Jump32, {}, {}},
    {0x140, "EB2E", {0, ""}, Jump8, {}, {}},
}};
constexpr std::array<Stat::ImportWitness, 1> Imports{{{0x180, 0xC0, "D00D"}}};
constexpr std::array<Stat::HelperWitness, 7> ProviderHelpers{{
    {"get", 0x10, "AABB", 10, "90909090", "CC", 0x200, 0x100, Functions, Imports},
    {"add", 0x20, "AABB", 6, "", "", 0, 0, {}, {}}, {"merge", 0x30, "AABB", 6, "", "", 0, 0, {}, {}},
    {"critical", 0x40, "AABB", 6, "", "", 0, 0, {}, {}}, {"base", 0x50, "AABB", 6, "", "", 0, 0, {}, {}},
    {"set", 0x60, "AABB", 6, "", "", 0, 0, {}, {}}, {"alignment", 0x70, "AABB", 10, "90909090", "CC", 0x220, 0x100, Functions, Imports},
}};
constexpr Stat::AdmissionContract ProviderContract{"synthetic-provider", ProviderHelpers};
struct ObservationCapture final {
    RuffnecKk::TrackedNativeTransform::Observation observation{};
    bool returnObservation{true};
    std::uintptr_t observedMain{};
    std::uintptr_t observedTarget{};
    std::size_t observedExpectedSize{};
};
auto ObserveTransform(void* userData, std::uintptr_t mainImageBase,
        std::uintptr_t targetAddress, std::span<const std::byte> expected,
        RuffnecKk::TrackedNativeTransform::Observation& observation) noexcept -> bool {
    auto& capture = *static_cast<ObservationCapture*>(userData);
    capture.observedMain = mainImageBase;
    capture.observedTarget = targetAddress;
    capture.observedExpectedSize = expected.size();
    observation = capture.observation;
    return capture.returnObservation;
}
auto CompatibleObservation() noexcept -> RuffnecKk::TrackedNativeTransform::Observation {
    return {RuffnecKk::TrackedNativeTransform::State::Tracked,
        RuffnecKk::TrackedNativeTransform::Kind::InlineHook, 1,
        "celestialrayone.max-life-one", true, true};
}
void PopulateProvider(Images& images) {
    auto& main = images.main; auto& core = images.core;
    main.bytes[0x10] = std::byte{0xFF}; main.bytes[0x11] = std::byte{0x25}; main.bytes[0x12] = std::byte{0x6A};
    for (std::size_t offset = 0x16; offset != 0x1A; ++offset) main.bytes[offset] = std::byte{0x90};
    main.bytes[0x1A] = std::byte{0xCC}; main.bytes[0xC0] = std::byte{0xD0}; main.bytes[0xC1] = std::byte{0x0D};
    Put64(main, 0x80, core.base + 0x100); core.bytes[0x100] = std::byte{0xE8}; core.bytes[0x101] = std::byte{0x0B};
    for (std::size_t offset = 0x105; offset != 0x10A; ++offset) core.bytes[offset] = std::byte{0x90};
    main.bytes[0x70] = std::byte{0xFF}; main.bytes[0x71] = std::byte{0x25}; main.bytes[0x72] = std::byte{0x12};
    for (std::size_t offset = 0x76; offset != 0x7A; ++offset) main.bytes[offset] = std::byte{0x90};
    main.bytes[0x7A] = std::byte{0xCC}; Put64(main, 0x88, core.base + 0x100);
    core.bytes[0x120] = std::byte{0xFF}; core.bytes[0x121] = std::byte{0x15}; core.bytes[0x122] = std::byte{0x5A};
    core.bytes[0x130] = std::byte{0xE9}; core.bytes[0x131] = std::byte{0x0B};
    core.bytes[0x140] = std::byte{0xEB}; core.bytes[0x141] = std::byte{0x2E};
    core.bytes[0x150] = std::byte{0x01}; core.bytes[0x151] = std::byte{0x02}; core.bytes[0x160] = std::byte{0xCA}; core.bytes[0x161] = std::byte{0xFE};
    Put64(core, 0x180, main.base + 0xC0); Put64(core, 0x188, 0xC0); Put64(core, 0x190, main.base + 0xC0);
    Put64(core, 0x200, main.base + 0x10); Put64(core, 0x208, 0x10); Put64(core, 0x210, main.base + 0x10);
    Put64(core, 0x220, main.base + 0x70); Put64(core, 0x228, 0x70); Put64(core, 0x230, main.base + 0x70);
}
} // namespace

int main() {
    using namespace Stat;
    Images canonical{};
    for (const auto& helper : CanonicalHelpers) { canonical.main.bytes[helper.nativeRva] = std::byte{0xAA}; canonical.main.bytes[helper.nativeRva + 1] = std::byte{0xBB}; }
    const MemoryReader canonicalReader{&canonical, Read, ValidateUnwind}; const AddressRange main{canonical.main.base, canonical.main.bytes.size()}; Adapter adapter;
    if (!adapter.Bind(canonicalReader, main, {}, CanonicalContract) || !adapter.IsBound()) return 1;
    if (adapter.BindCurrentProcess(0, ToMask(Helper::GetUnitStat), CanonicalContract)
        || adapter.IsBound() || adapter.IsAdmitted(Helper::GetUnitStat)) return 6;
    CanonicalCapture capture; const auto& realContract = Loader130StatAdmissionContract();
    for (const auto& helper : realContract.helpers) PopulateHex(capture, helper.nativeRva, helper.canonicalExpectedHex);
    Adapter sixHelperCapture;
    if (!sixHelperCapture.Bind({&capture, ReadCapture, nullptr}, {capture.base, capture.bytes.size()}, {}, realContract)
        || !sixHelperCapture.IsBound()) return 8;
    auto overflowHelpers = CanonicalHelpers;
    overflowHelpers[0].nativeRva = 0;
    overflowHelpers[1].nativeRva = 0x20;
    const AdmissionContract overflowContract{"overflow", overflowHelpers};
    CanonicalCapture overflowCapture;
    overflowCapture.base = (std::numeric_limits<std::uintptr_t>::max)() - 0x10;
    overflowCapture.bytes[0] = std::byte{0xAA}; overflowCapture.bytes[1] = std::byte{0xBB};
    Adapter partiallyAdmitted;
    if (partiallyAdmitted.Bind({&overflowCapture, ReadCapture, nullptr}, {overflowCapture.base, 0x10}, {}, overflowContract,
            ToMask(Helper::GetUnitStat) | ToMask(Helper::AddUnitStat))
        || partiallyAdmitted.IsBound() || partiallyAdmitted.IsAdmitted(Helper::GetUnitStat)) return 9;
    auto canonicalForwardHelpers = CanonicalHelpers;
    canonicalForwardHelpers[5].nativeRva = 0;
    const AdmissionContract canonicalForwardContract{"canonical-forward", canonicalForwardHelpers};
    Adapter canonicalForward;
    const auto legacyEntry = reinterpret_cast<std::uintptr_t>(&LegacySet);
    if (!canonicalForward.Bind({nullptr, ReadCanonicalDispatch, nullptr}, {legacyEntry, 2}, {}, canonicalForwardContract,
            ToMask(Helper::SetUnitStat))) return 10;
    canonicalForward.SetUnitStatWide(nullptr, 0, 0, 0x12345678U);
    if (legacyLayer != 0x5678U) return 11;
    auto providerForwardHelpers = CanonicalHelpers;
    providerForwardHelpers[5] = {"set", 0, "AABB", 6, "", "CC", 0x20, 0, {}, {}};
    const AdmissionContract providerForwardContract{"provider-forward", providerForwardHelpers};
    const DispatchReader providerReader{reinterpret_cast<std::uintptr_t>(&WideSet), reinterpret_cast<std::uintptr_t>(&WideSet)};
    Adapter providerForward;
    if (!providerForward.Bind({const_cast<DispatchReader*>(&providerReader), ReadProviderDispatch, nullptr},
            {providerReader.main, 32}, {providerReader.core, 64}, providerForwardContract, ToMask(Helper::SetUnitStat))) return 12;
    providerForward.SetUnitStatWide(nullptr, 0, 0, 0x12345678U);
    if (wideLayer != 0x12345678U) return 13;
    auto canonicalAlignmentHelpers = CanonicalHelpers;
    canonicalAlignmentHelpers[6].nativeRva = 0;
    const AdmissionContract canonicalAlignmentContract{"canonical-alignment", canonicalAlignmentHelpers};
    Adapter canonicalAlignment;
    const auto alignmentEntry = reinterpret_cast<std::uintptr_t>(&Alignment);
    if (!canonicalAlignment.Bind({nullptr, ReadCanonicalDispatch, nullptr}, {alignmentEntry, 2}, {}, canonicalAlignmentContract,
            ToMask(Helper::GetUnitAlignment)) || canonicalAlignment.GetUnitAlignment(nullptr) != 73) return 16;
    auto providerAlignmentHelpers = ProviderHelpers;
    providerAlignmentHelpers[6].nativeRva = 0;
    providerAlignmentHelpers[6].descriptorRva = 0x20;
    providerAlignmentHelpers[6].exportRva = 0;
    providerAlignmentHelpers[6].functions = {};
    providerAlignmentHelpers[6].imports = {};
    const AdmissionContract providerAlignmentContract{"provider-alignment", providerAlignmentHelpers};
    const DispatchReader alignmentReader{alignmentEntry, alignmentEntry};
    Adapter providerAlignment;
    if (!providerAlignment.Bind({const_cast<DispatchReader*>(&alignmentReader), ReadProviderDispatch, nullptr},
            {alignmentReader.main, 32}, {alignmentReader.core, 64}, providerAlignmentContract, ToMask(Helper::GetUnitAlignment))
        || providerAlignment.GetUnitAlignment(nullptr) != 73) return 17;
    for (const auto& helper : CanonicalHelpers) { Images corrupted = canonical; corrupted.main.bytes[helper.nativeRva] = std::byte{0}; Adapter rejected; if (rejected.Bind({&corrupted, Read, ValidateUnwind}, main, {}, CanonicalContract)) return 2; }
    Images provider{}; PopulateProvider(provider); const AddressRange providerMain{provider.main.base, provider.main.bytes.size()}; const AddressRange core{provider.core.base, provider.core.bytes.size()}; Adapter accepted;
    if (!accepted.Bind({&provider, Read, ValidateUnwind}, providerMain, core, ProviderContract, ToMask(Helper::GetUnitStat)) || !accepted.IsBound() || accepted.IsBound(Helper::AddUnitStat)) return 3;
    if (accepted.Bind({&provider, Read, ValidateUnwind}, providerMain, {}, ProviderContract, ToMask(Helper::GetUnitStat))
        || accepted.IsBound() || accepted.IsAdmitted(Helper::GetUnitStat)) return 7;
    Adapter alignmentAccepted;
    if (!alignmentAccepted.Bind({&provider, Read, ValidateUnwind}, providerMain, core, ProviderContract, ToMask(Helper::GetUnitAlignment))
        || !alignmentAccepted.IsBound() || alignmentAccepted.IsBound(Helper::GetUnitStat)) return 14;
    Images redirected = provider;
    Put64(redirected.main, 0x80, 0xBEEF);
    bool redirectAllowed = true;
    const TransformDiagnostics redirectProof{&redirectAllowed, nullptr, ValidateRedirect};
    Adapter redirectRejected;
    if (redirectRejected.Bind({&redirected, Read, ValidateUnwind}, providerMain, core,
            ProviderContract, ToMask(Helper::GetUnitStat))) return 100;
    Adapter redirectAccepted;
    if (!redirectAccepted.Bind({&redirected, Read, ValidateUnwind}, providerMain, core,
            ProviderContract, ToMask(Helper::GetUnitStat), redirectProof)) return 101;
    redirectAllowed = false;
    if (redirectRejected.Bind({&redirected, Read, ValidateUnwind}, providerMain, core,
            ProviderContract, ToMask(Helper::GetUnitStat), redirectProof)) return 102;
    redirectAllowed = true;
    // A redirect proof never waives the independent Core code, calls, unwind,
    // read-only data, import graph or descriptor checks.
    for (const auto offset : std::array<std::size_t, 6>{0x102,0x120,0x150,0x160,0x188,0x208}) {
        Images brokenRedirect = redirected;
        brokenRedirect.core.bytes[offset] ^= std::byte{1};
        Adapter rejectedRedirect;
        if (rejectedRedirect.Bind({&brokenRedirect, Read, ValidateUnwind}, providerMain, core,
                ProviderContract, ToMask(Helper::GetUnitStat), redirectProof)) return 103;
    }
    Images badRedirectUnwind = redirected; badRedirectUnwind.unwind = false;
    if (redirectRejected.Bind({&badRedirectUnwind, Read, ValidateUnwind}, providerMain, core,
            ProviderContract, ToMask(Helper::GetUnitStat), redirectProof)) return 104;
    auto trackedHelpers = ProviderHelpers;
    trackedHelpers[0].functions = TrackedFunctions;
    const AdmissionContract trackedContract{"synthetic-tracked-provider", trackedHelpers};
    Images celestial = provider;
    celestial.core.bytes[0x100] = std::byte{0xE9};
    celestial.core.bytes[0x101] = std::byte{0};
    celestial.core.bytes[0x102] = std::byte{0};
    celestial.core.bytes[0x103] = std::byte{0};
    celestial.core.bytes[0x104] = std::byte{0};
    ObservationCapture observation{CompatibleObservation()};
    Adapter celestialAccepted;
    if (!celestialAccepted.Bind({&celestial, Read, ValidateUnwind, ValidateExecutable}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&observation, ObserveTransform})
        || observation.observedMain != providerMain.base
        || observation.observedTarget != core.base + 0x100
        || observation.observedExpectedSize != 5) return 18;
    ObservationCapture displacedPrefixObservation{CompatibleObservation()};
    Adapter displacedPrefixAccepted;
    if (!displacedPrefixAccepted.Bind(
            {&celestial, Read, ValidateUnwind, ValidateExecutable},
            providerMain,
            core,
            ProviderContract,
            ToMask(Helper::GetUnitStat),
            {&displacedPrefixObservation, ObserveTransform})) return 29;
    Images changedTrackedTail = celestial;
    changedTrackedTail.core.bytes[0x105] ^= std::byte{1};
    ObservationCapture changedTrackedTailObservation{CompatibleObservation()};
    Adapter changedTrackedTailRejected;
    if (changedTrackedTailRejected.Bind(
            {&changedTrackedTail, Read, ValidateUnwind, ValidateExecutable},
            providerMain,
            core,
            ProviderContract,
            ToMask(Helper::GetUnitStat),
            {&changedTrackedTailObservation, ObserveTransform})) return 30;
    ObservationCapture noExecutableValidatorObservation{CompatibleObservation()};
    Adapter noExecutableValidator;
    if (noExecutableValidator.Bind({&celestial, Read, ValidateUnwind}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&noExecutableValidatorObservation, ObserveTransform})) return 28;
    Adapter noDiagnostics;
    if (noDiagnostics.Bind({&celestial, Read, ValidateUnwind}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat))) return 19;
    auto rejectObservation = [&](RuffnecKk::TrackedNativeTransform::Observation candidate) {
        ObservationCapture rejectedCapture{candidate};
        Adapter rejected;
        return rejected.Bind({&celestial, Read, ValidateUnwind, ValidateExecutable}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&rejectedCapture, ObserveTransform});
    };
    auto untracked = CompatibleObservation();
    untracked.state = RuffnecKk::TrackedNativeTransform::State::Untracked;
    if (rejectObservation(untracked)) return 20;
    auto wrongOwner = CompatibleObservation();
    wrongOwner.ownerPluginId = "not-celestial";
    if (rejectObservation(wrongOwner)) return 21;
    auto multipleOwners = CompatibleObservation();
    multipleOwners.ownerCount = 2;
    if (rejectObservation(multipleOwners)) return 22;
    auto wrongKind = CompatibleObservation();
    wrongKind.kind = RuffnecKk::TrackedNativeTransform::Kind::BytePatch;
    if (rejectObservation(wrongKind)) return 23;
    ObservationCapture nonExecutableObservation{CompatibleObservation()};
    Adapter nonExecutableRejected;
    if (nonExecutableRejected.Bind({&celestial, Read, ValidateUnwind, ValidateNotExecutable}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&nonExecutableObservation, ObserveTransform})) return 24;
    Images brokenTail = celestial;
    brokenTail.main.bytes[0x1A] ^= std::byte{1};
    ObservationCapture brokenTailObservation{CompatibleObservation()};
    Adapter brokenTailRejected;
    if (brokenTailRejected.Bind({&brokenTail, Read, ValidateUnwind, ValidateExecutable}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&brokenTailObservation, ObserveTransform})) return 25;
    Images brokenIndependentWitness = celestial;
    brokenIndependentWitness.core.bytes[0x120] ^= std::byte{1};
    ObservationCapture brokenWitnessObservation{CompatibleObservation()};
    Adapter brokenWitnessRejected;
    if (brokenWitnessRejected.Bind({&brokenIndependentWitness, Read, ValidateUnwind, ValidateExecutable}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&brokenWitnessObservation, ObserveTransform})) return 26;
    ObservationCapture malformedObservation{CompatibleObservation(), false};
    Adapter malformedRejected;
    if (malformedRejected.Bind({&celestial, Read, ValidateUnwind, ValidateExecutable}, providerMain, core, trackedContract,
            ToMask(Helper::GetUnitStat), {&malformedObservation, ObserveTransform})) return 27;
    constexpr std::array<std::size_t, 4> alignmentOffsets{{0x70, 0x7A, 0x88, 0x220}};
    for (const auto offset : alignmentOffsets) { Images corrupted = provider; auto& image = offset < 0x100 ? corrupted.main : corrupted.core; image.bytes[offset] ^= std::byte{1}; Adapter rejected; if (rejected.Bind({&corrupted, Read, ValidateUnwind}, providerMain, core, ProviderContract, ToMask(Helper::GetUnitAlignment))) return 15; }
    constexpr std::array<std::size_t, 12> offsets{{0x10, 0x80, 0x16, 0x1A, 0x100, 0x120, 0x130, 0x140, 0x150, 0x160, 0x180, 0x200}};
    for (const auto offset : offsets) { Images corrupted = provider; auto& image = offset < 0x100 ? corrupted.main : corrupted.core; image.bytes[offset] ^= std::byte{1}; Adapter rejected; if (rejected.Bind({&corrupted, Read, ValidateUnwind}, providerMain, core, ProviderContract, ToMask(Helper::GetUnitStat))) return 4; }
    Images unwindCorrupt = provider; unwindCorrupt.unwind = false; Adapter unwindRejected;
    return unwindRejected.Bind({&unwindCorrupt, Read, ValidateUnwind}, providerMain, core, ProviderContract, ToMask(Helper::GetUnitStat)) ? 5 : 0;
}
