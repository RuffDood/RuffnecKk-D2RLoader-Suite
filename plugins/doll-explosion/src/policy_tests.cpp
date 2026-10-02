#include "doll_explosion_policy.hpp"
#include "native_fingerprint.hpp"
#include "damage_cleanup_api.hpp"

#include <RuffnecKk/native_stat_compat.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <stdexcept>

using namespace ruffneckk::doll_explosion;
namespace native_stat = RuffnecKk::NativeStatCompat;

namespace {

int Failures{};

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": CHECK failed: " #expression << '\n';               \
            ++Failures;                                                        \
        }                                                                      \
    } while (false)

auto ReadFile(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

struct StatAdapterPolicyImage final {
    std::uintptr_t base{0x1000};
    std::array<std::byte, 16> bytes{};
};

auto ReadStatAdapterPolicyImage(
        void* userData,
        std::uintptr_t address,
        std::byte* output,
        std::size_t size) noexcept -> bool {
    const auto& image = *static_cast<const StatAdapterPolicyImage*>(userData);
    if (address < image.base || size > image.bytes.size()
            || address - image.base > image.bytes.size() - size) {
        return false;
    }
    std::memcpy(output, image.bytes.data() + address - image.base, size);
    return true;
}

constexpr std::array<native_stat::HelperWitness, 7> StatAdapterPolicyHelpers{{
    {"GetUnitStat", 0, "AA", 0, "", "", 0, 0, {}, {}},
    {"AddUnitStat", 1, "AA", 0, "", "", 0, 0, {}, {}},
    {"MergeStatLists", 2, "AA", 0, "", "", 0, 0, {}, {}},
    {"WeaponMastery", 3, "AA", 0, "", "", 0, 0, {}, {}},
    {"GetUnitBaseStat", 4, "AA", 0, "", "", 0, 0, {}, {}},
    {"SetUnitStat", 5, "AA", 0, "", "", 0, 0, {}, {}},
    {"GetUnitAlignment", 6, "AA", 0, "", "", 0, 0, {}, {}},
}};
constexpr native_stat::AdmissionContract StatAdapterPolicyContract{
    "policy", StatAdapterPolicyHelpers};

constexpr std::string_view ValidToml = R"toml(
config_version = 1

[targets]
monstats_ids = [212, 213, 214, 215, 216, 690, 691]

[explosion]
delay_frames = 25
radius = 4

[damage]
formula = "fixed"

[damage.fixed]
normal = [18, 30]
nightmare = [54, 96]
hell = [318, 540]

[damage.source_max_life_percent]
normal = [30, 50]
nightmare = [21, 35]
hell = [12, 20]

[diagnostics]
show_usage_counters = false
)toml";

auto Parses(std::string_view text) -> bool {
    Config config{};
    std::string error;
    return ParseToml(text, config, error);
}

auto Replace(
        std::string text,
        std::string_view from,
        std::string_view to) -> std::string {
    const auto position = text.find(from);
    if (position != std::string::npos) {
        text.replace(position, from.size(), to);
    }
    return text;
}

void TestDefaultsAndPolicy() {
    const Config defaults{};
    CHECK(defaults.schemaVersion == 1);
    CHECK(defaults.targetMonsterIds.size() == 7);
    for (const auto id : {212, 213, 214, 215, 216, 690, 691}) {
        CHECK(IsTargetMonster(defaults, id));
    }
    CHECK(!IsTargetMonster(defaults, 777));
    CHECK(defaults.delayFrames == 25);
    CHECK(defaults.radius == 4);
    CHECK(defaults.formula == DamageFormula::Fixed);
    CHECK((defaults.fixed.normal == InclusiveRange{18, 30}));
    CHECK((defaults.fixed.nightmare == InclusiveRange{54, 96}));
    CHECK((defaults.fixed.hell == InclusiveRange{318, 540}));

    CHECK(SelectRange(defaults.fixed, 0) == &defaults.fixed.normal);
    CHECK(SelectRange(defaults.fixed, 1) == &defaults.fixed.nightmare);
    CHECK(SelectRange(defaults.fixed, 2) == &defaults.fixed.hell);
    CHECK(SelectRange(defaults.fixed, 3) == nullptr);
    CHECK(InclusiveSpan({18, 30}) == 13);
    CHECK(ApplyInclusiveRoll({18, 30}, 0) == 18);
    CHECK(ApplyInclusiveRoll({18, 30}, 12) == 30);
    CHECK(ScaleFixedDamage(18) == 18 * 256);
    CHECK(ScaleFixedDamage(540) == 540 * 256);
    CHECK(!ScaleFixedDamage(MaximumFixedDamage + 1).has_value());
    CHECK(ScaleMaxLifePercent(100 * 256, 30) == 30 * 256);
    CHECK(ScaleMaxLifePercent(101 * 256, 35) == 9049);
    CHECK(!ScaleMaxLifePercent(0, 30).has_value());
    CHECK(!ScaleMaxLifePercent(256, 101).has_value());
}

void TestParsing() {
    Config config{};
    std::string error;
    CHECK(ParseToml(ValidToml, config, error));
    CHECK(error.empty());
    CHECK(config.targetMonsterIds.size() == 7);
    CHECK(config.delayFrames == 25);
    CHECK(config.radius == 4);
    CHECK(config.formula == DamageFormula::Fixed);
    CHECK(!config.diagnostics);

    auto percent = Replace(
        std::string(ValidToml),
        "formula = \"fixed\"",
        "formula = \"source_max_life_percent\"");
    CHECK(ParseToml(percent, config, error));
    CHECK(config.formula == DamageFormula::SourceMaxLifePercent);

    CHECK(!Parses(Replace(
        std::string(ValidToml), "config_version = 1", "config_version = 2")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "config_version = 1\n", "")));
    CHECK(!Parses(std::string(ValidToml) + "\nunknown = true\n"));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "monstats_ids = [212, 213, 214, 215, 216, 690, 691]",
        "monstats_ids = []")));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "monstats_ids = [212, 213, 214, 215, 216, 690, 691]",
        "monstats_ids = [212, 212]")));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "monstats_ids = [212, 213, 214, 215, 216, 690, 691]",
        "monstats_ids = [65536]")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "delay_frames = 25", "delay_frames = 32768")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "radius = 4", "radius = 0")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "radius = 4", "radius = 65")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "formula = \"fixed\"", "formula = \"script\"")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "normal = [18, 30]", "normal = [31, 30]")));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "normal = [18, 30]",
        "normal = [0, 8388608]")));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "normal = [30, 50]",
        "normal = [30, 101]")));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "show_usage_counters = false",
        "show_usage_counters = 0")));
    CHECK(!Parses(Replace(
        std::string(ValidToml), "[diagnostics]", "[other]")));
    CHECK(!Parses(Replace(
        std::string(ValidToml),
        "radius = 4",
        "radius = 4\nextra = true")));
}

void TestConfigCandidates() {
    const auto candidates = BuildConfigCandidates(
        L"C:/D2R/mods/BKVince/d2rloader/config",
        L"C:/D2R/mods/BKVince/d2rloader/config",
        L"C:/D2R/d2rloader/config",
        L"ruffneckk-doll-explosion.toml");
    CHECK(candidates.size() == 2);
    CHECK(candidates[0].generic_wstring().find(L"mods/BKVince")
        != std::wstring::npos);
    CHECK(candidates[1].generic_wstring().find(L"d2rloader/config")
        != std::wstring::npos);
}

void TestSourceContracts() {
    const auto plugin = ReadFile(DOLL_EXPLOSION_PLUGIN_SOURCE);
    const auto native = ReadFile(DOLL_EXPLOSION_NATIVE_SOURCE);
    const auto cmake = ReadFile(DOLL_EXPLOSION_CMAKE_SOURCE);
    const auto toml = ReadFile(DOLL_EXPLOSION_TOML_SOURCE);

    CHECK(plugin.find(".author = \"RuffnecKk\"") != std::string::npos);
    CHECK(plugin.find("PluginFlags::Shared | D2RL::PluginFlags::NativeHooks")
        != std::string::npos);
    CHECK(plugin.find("ModScopedOnly") == std::string::npos);
    CHECK(plugin.find("GetBuildName(context)") != std::string::npos);
    CHECK(plugin.find("diagnostic only; validating the complete native fingerprint")
        != std::string::npos);
    CHECK(plugin.find("std::strcmp(runtimeBuild") == std::string::npos);
    CHECK(plugin.find("\"92777\"") == std::string::npos);
    CHECK(plugin.find("\"93847\"") == std::string::npos);
    CHECK(plugin.find("DelayCarrierMissileId = 587") != std::string::npos);
    CHECK(plugin.find("CorpseExplosionMissileId = 117") != std::string::npos);
    CHECK(plugin.find("SetMissileTotalFrames(missile, delayFrames)")
        != std::string::npos);
    CHECK(plugin.find("SetMissileCurrentFrames(missile, delayFrames)")
        != std::string::npos);
    CHECK(plugin.find("GetMissileTotalFrames(missile) == delayFrames")
        != std::string::npos);
    CHECK(plugin.find("GetMissileCurrentFrames(missile) == delayFrames")
        != std::string::npos);
    CHECK(plugin.find("GameplayEventKind::GameJoined")
        != std::string::npos);
    CHECK(plugin.find("GameplayEventKind::GameLeft")
        != std::string::npos);
    CHECK(plugin.find("ClearCarriers();") != std::string::npos);
    CHECK(plugin.find("OriginalDeathMode(game, modeChange)")
        != std::string::npos);
    CHECK(plugin.find("CheckState(identity.unit, ReviveStateId)")
        != std::string::npos);
    CHECK(plugin.find("MonStatsDeathDamageMask") != std::string::npos);
    CHECK(plugin.find("armageddoncontrol") == std::string::npos);
    CHECK(plugin.find("PluginPack") == std::string::npos);
    CHECK(plugin.find("eezstreet") == std::string::npos);
    CHECK(plugin.find("GenerateUnitCastIdInfo") != std::string::npos);
    CHECK(plugin.find("native::GetUnitClassRva") == std::string::npos);
    CHECK(plugin.find("native::NextGameCounterRva") == std::string::npos);
    CHECK(plugin.find("native::DestroyDamageExpected") != std::string::npos);
    CHECK(plugin.find("ValidateDamageCleanup()") != std::string::npos);
    CHECK(plugin.find("binding.api.run(") != std::string::npos);
    CHECK(plugin.find("GetModuleHandleExW") != std::string::npos);
    CHECK(plugin.find("FreeLibrary(binding.module)") != std::string::npos);
    CHECK(plugin.find("return result == cleanup::Result::Completed;") != std::string::npos);
    CHECK(plugin.find("NativeStatAdapter.BindCurrentProcess(") != std::string::npos);
    CHECK(plugin.find("RequiredStatHelpers") != std::string::npos);
    CHECK(plugin.find("NativeStatAdapter.GetUnitStat(") != std::string::npos);
    CHECK(plugin.find("GetUnitStatFn") == std::string::npos);
    CHECK(plugin.find("GetUnitStat = At<") == std::string::npos);

    CHECK(native.find("DeathModeRva = 0x444F50") != std::string::npos);
    CHECK(plugin.find("HookServerDoOne") != std::string::npos);
    CHECK(plugin.find("RunTrackedServerDo(framesBefore,") != std::string::npos);
    CHECK(native.find("GenericMissileRva") == std::string::npos);
    CHECK(native.find("BasicMissileRva") == std::string::npos);
    CHECK(native.find("GetUnitStatRva") == std::string::npos);
    CHECK(native.find("GetUnitStatExpected") == std::string::npos);
    CHECK(native.find("ApplyAreaDamageRva = 0x44A120")
        != std::string::npos);
    CHECK(native.find("GetMissileCurrentFramesRva = 0x3BB1E0")
        != std::string::npos);
    CHECK(native.find("GetMissileTotalFramesRva = 0x3BC3E0")
        != std::string::npos);
    CHECK(native.find("SetMissileCurrentFramesRva = 0x3BD450")
        != std::string::npos);
    CHECK(native.find("SetMissileTotalFramesRva = 0x3BDBC0")
        != std::string::npos);
    CHECK(cmake.find("4933e2c42cb2592958cd0df3b6dc5003102252d1")
        != std::string::npos);
    CHECK(cmake.find("RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE FALSE")
        != std::string::npos);
    CHECK(cmake.find("RuffnecKk::NativeStatCompat") != std::string::npos);
    CHECK(cmake.find("../../common") != std::string::npos);
    CHECK(cmake.find("../../third_party/PluginSDK") != std::string::npos);
    CHECK(cmake.find("RUFFNECKK_SUITE_COMMON_DIR") == std::string::npos);

    Config shipped{};
    std::string error;
    CHECK(ParseToml(toml, shipped, error));
    CHECK(shipped.delayFrames == 25);
    CHECK(shipped.radius == 4);
    CHECK(shipped.formula == DamageFormula::Fixed);
    CHECK(IsTargetMonster(shipped, 691));
    CHECK(!IsTargetMonster(shipped, 777));
    CHECK(toml.find("enabled =") == std::string::npos);
}

void TestNativeStatAdapterPolicy() {
    constexpr auto required = native_stat::ToMask(
        native_stat::Helper::GetUnitStat);
    static_assert(required == 1U);
    StatAdapterPolicyImage image{};
    image.bytes[0] = std::byte{0xAA};
    const native_stat::MemoryReader reader{
        &image, ReadStatAdapterPolicyImage, nullptr};
    const native_stat::AddressRange main{image.base, image.bytes.size()};
    native_stat::Adapter adapter;
    CHECK(adapter.Bind(reader, main, {}, StatAdapterPolicyContract, required));
    CHECK(adapter.IsBound());
    CHECK(adapter.IsBound(native_stat::Helper::GetUnitStat));
    CHECK(!adapter.IsBound(native_stat::Helper::AddUnitStat));
    image.bytes[0] = std::byte{0};
    CHECK(!adapter.Bind(reader, main, {}, StatAdapterPolicyContract, required));
    CHECK(!adapter.IsBound());
    CHECK(!adapter.IsAdmitted(native_stat::Helper::GetUnitStat));
}

} // namespace

// Optional governed PE input: inspect the actual file bytes, not a mock copy
// of the expected signatures. Data-only/BSS addresses are not read from disk.
void TestNativeImage(const std::filesystem::path& path) {
    const auto image = ReadFile(path);
    const auto read = [&](std::size_t offset, std::size_t width) -> std::uint32_t {
        if (width > 4 || offset > image.size() || image.size() - offset < width)
            throw std::runtime_error("Truncated PE input");
        std::uint32_t value{};
        for (std::size_t i = 0; i < width; ++i)
            value |= std::uint32_t{static_cast<std::uint8_t>(image[offset + i])} << (8 * i);
        return value;
    };
    if (read(0, 2) != 0x5A4D) throw std::runtime_error("Not a PE image");
    const std::size_t pe = read(0x3C, 4);
    if (read(pe, 4) != 0x4550 || read(pe + 4, 2) != 0x8664
            || read(pe + 24, 2) != 0x20B)
        throw std::runtime_error("Not an AMD64 PE32+ image");
    const auto sectionCount = read(pe + 6, 2);
    const auto sections = pe + 24 + read(pe + 20, 2);
    const auto check = [&](std::uintptr_t rva, const auto& expected) {
        for (std::size_t i = 0; i < sectionCount; ++i) {
            const auto section = sections + i * 40;
            const auto start = read(section + 12, 4);
            const auto size = read(section + 16, 4);
            if (rva < start || rva - start > size
                    || expected.size() > size - (rva - start)) continue;
            const std::size_t offset = read(section + 20, 4) + (rva - start);
            if (offset > image.size() || expected.size() > image.size() - offset)
                throw std::runtime_error("Witness outside PE file");
            const auto* actual = reinterpret_cast<const std::uint8_t*>(image.data() + offset);
            CHECK(native::MatchesBytes(actual, expected.size(), expected));
            if (!native::MatchesBytes(actual, expected.size(), expected))
                std::cerr << "Native witness mismatch at RVA " << std::hex << rva << '\n';
            return;
        }
        throw std::runtime_error("Witness RVA has no file-backed section");
    };
#define CHECK_IMAGE(name) check(native::name##Rva, native::name##Expected)
    CHECK_IMAGE(DeathMode);
    CHECK_IMAGE(DeathModeDataGuard);
    CHECK_IMAGE(CreateSkillMissile);
    CHECK_IMAGE(DeathCallContext);
    CHECK_IMAGE(ServerDoOne);
    CHECK_IMAGE(ServerMissileStep);
    CHECK_IMAGE(ServerCountdown);
    CHECK_IMAGE(ServerHitResolve);
    CHECK_IMAGE(ServerHitReturn);
    CHECK_IMAGE(ServerRemoval);
    CHECK_IMAGE(ServerDoDispatch);
    CHECK_IMAGE(ServerDoTableWitness);
    CHECK_IMAGE(GameDataLayout);
    CHECK_IMAGE(DamageInitLayout);
    CHECK_IMAGE(DamageCallLayout);
    CHECK_IMAGE(MissileTimingLayout);
    CHECK_IMAGE(MissileTotalFrames);
    CHECK_IMAGE(GetMissileCurrentFrames);
    CHECK_IMAGE(GetMissileTotalFrames);
    CHECK_IMAGE(SetMissileCurrentFrames);
    CHECK_IMAGE(SetMissileTotalFrames);
    CHECK_IMAGE(ApplyAreaDamage);
    CHECK_IMAGE(GetMissilesRecord);
    CHECK_IMAGE(GetMonStatsRecord);
    CHECK_IMAGE(GetUnitType);
    CHECK_IMAGE(UnitIdentityLayout);
    CHECK_IMAGE(GetUnitId);
    CHECK_IMAGE(GetServerUnit);
    CHECK_IMAGE(GetPathX);
    CHECK_IMAGE(GetPathY);
    CHECK_IMAGE(CheckState);
    CHECK_IMAGE(ReviveStateMarker);
    CHECK_IMAGE(GetSeed);
    CHECK_IMAGE(RollRandom);
    CHECK_IMAGE(DestroyDamage);
#undef CHECK_IMAGE
}

int main(int argc, char** argv) {
    if (argc == 2) {
        try { TestNativeImage(std::filesystem::path(argv[1])); }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
        return Failures == 0 ? 0 : 1;
    }
    // Resolve the actual encoded operands. Old table and wrong hook choices
    // must fail these relationships even if their source strings look valid.
    CHECK(native::RelativeTarget(native::ServerDoTableWitnessRva,
        native::ServerDoTableWitnessExpected, 8, 12) == native::ServerDoTableRva);
    CHECK(native::RelativeTarget(native::ServerDoOneRva,
        native::ServerDoOneExpected, 1, 5) == native::ServerMissileStepRva);
    CHECK(native::RelativeTarget(native::ServerDoTableWitnessRva,
        native::ServerDoTableWitnessExpected, 8, 12) != 0x2380E80);
    CHECK(native::RelativeTarget(native::ServerDoOneRva,
        native::ServerDoOneExpected, 1, 5) != 0x466B40);
    for (std::size_t i = 8; i < 12; ++i) {
        auto changed = native::ServerDoTableWitnessExpected;
        changed[i] ^= 1;
        CHECK(native::RelativeTarget(native::ServerDoTableWitnessRva,
            changed, 8, 12) != native::ServerDoTableRva);
    }
    constexpr std::array<std::uint8_t, 4> backwards{0xF0, 0xFF, 0xFF, 0xFF};
    CHECK(native::RelativeTarget(0x100, backwards, 0, 4) == 0xF4);
    CHECK(native::RelativeTarget(0x100, backwards, 1, 4) == -1);
    CHECK(native::RelativeTarget(0x100, backwards, 5, 4) == -1);
    for (const auto frames : {-1, 0, 1, 2, 25}) {
        for (const auto result : {-1, 0, 1, 2, 3}) {
            int calls{}, removals{}, blasts{};
            const auto returned = RunTrackedServerDo(frames,
                [&]() noexcept { ++calls; CHECK(removals == 0); return result; },
                [&](bool expired) noexcept {
                    CHECK(calls == 1); ++removals;
                    if (expired) ++blasts;
                });
            CHECK(returned == result);
            CHECK(calls == 1);
            CHECK(removals == (result == 2 ? 1 : 0));
            CHECK(blasts == (result == 2 && frames == 1 ? 1 : 0));
        }
    }
    namespace cleanup = RuffnecKk::DamageCleanup;
    cleanup::ApiV1 api{sizeof(cleanup::ApiV1), cleanup::Version, cleanup::DamageLayout,
        +[](const cleanup::RequestV1*) noexcept { return true; },
        +[](const cleanup::RequestV1*, void*, cleanup::OperationFn, void*) noexcept {
            return cleanup::Result::Completed;
        }};
    CHECK(cleanup::ValidApi(&api));
    ++api.version; CHECK(!cleanup::ValidApi(&api)); --api.version;
    ++api.damageLayout; CHECK(!cleanup::ValidApi(&api)); --api.damageLayout;
    --api.structSize; CHECK(!cleanup::ValidApi(&api)); ++api.structSize;
    api.accepts = nullptr; CHECK(!cleanup::ValidApi(&api));
    // Class identity is independent of the callable helper that Bind And
    // Summon owns. The immutable tuple witness must still prove the layout.
    std::array<std::uint8_t, 16> unit{};
    const std::int32_t expectedClass = 691;
    std::memcpy(unit.data() + native::UnitClassIdOffset,
        &expectedClass, sizeof(expectedClass));
    CHECK(native::ReadUnitClassId(unit.data()) == expectedClass);
    CHECK(native::ReadUnitClassId(nullptr) == -1);
    static_assert(native::UnitIdentityLayoutExpected[8] == native::UnitClassIdOffset);
    static_assert(native::DeathCallContextRva == 0x44532A);
    static_assert(native::DeathCallContextExpected.size() == 71);
    // D2RCore owns the preceding five-byte cast-ID call. Its redirection
    // must not exempt any byte of the constructor-argument witness.
    std::array<std::uint8_t, 5 + native::DeathCallContextExpected.size()> callContext{};
    std::memcpy(callContext.data() + 5, native::DeathCallContextExpected.data(),
        native::DeathCallContextExpected.size());
    for (std::size_t i = 0; i < 5; ++i) callContext[i] = 0xCC;
    CHECK(native::MatchesBytes(callContext.data() + 5, callContext.size() - 5,
        native::DeathCallContextExpected));
    for (std::size_t i = 5; i < callContext.size(); ++i) {
        callContext[i] ^= 1;
        CHECK(!native::MatchesBytes(callContext.data() + 5, callContext.size() - 5,
            native::DeathCallContextExpected));
        callContext[i] ^= 1;
    }
    auto witness = native::UnitIdentityLayoutExpected;
    CHECK(native::MatchesBytes(witness.data(), witness.size(), witness));
    CHECK(!native::MatchesBytes(nullptr, witness.size(), witness));
    CHECK(!native::MatchesBytes(witness.data(), witness.size() - 1, witness));
    for (std::size_t index = 0; index < witness.size(); ++index) {
        auto changed = witness;
        changed[index] ^= 1;
        CHECK(!native::MatchesBytes(changed.data(), changed.size(), witness));
    }
    const auto lookupWitness = native::GetServerUnitExpected;
    CHECK(!native::MatchesBytes(nullptr, lookupWitness.size(), lookupWitness));
    CHECK(!native::MatchesBytes(lookupWitness.data(), lookupWitness.size() - 1, lookupWitness));
    for (std::size_t index = 0; index < lookupWitness.size(); ++index) {
        auto changed = lookupWitness;
        changed[index] ^= 1;
        CHECK(!native::MatchesBytes(changed.data(), changed.size(), lookupWitness));
    }
    TestDefaultsAndPolicy();
    TestParsing();
    TestConfigCandidates();
    TestSourceContracts();
    TestNativeStatAdapterPolicy();
    return Failures == 0 ? 0 : 1;
}
