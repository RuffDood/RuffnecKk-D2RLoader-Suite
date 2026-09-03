#include "d3d12_imgui_host.hpp"
#include "automap_sprite_package.hpp"
#include "automap_sprite_projection.hpp"
#include "external_atlas_cache.hpp"
#include "external_atlas_geometry.hpp"
#include "external_label_provider.hpp"
#include "mapsense_config.hpp"
#include "mapsense_data_catalog.hpp"
#include "navigation_engine.hpp"
#include "navigation_level_catalog.hpp"
#include "navigation_policy.hpp"
#include "navigation_resolver.hpp"
#include "native_automap_marker.hpp"
#include "native_automap_missile.hpp"
#include "native_automap_poi.hpp"
#include "native_ui_state.hpp"
#include "native_settings_layout.hpp"
#include "native_settings_panel.hpp"
#include "native_settings_policy.hpp"
#include "overlay_scene.hpp"
#include "reveal_engine.hpp"
#include "ui_localization.hpp"

#include <nlohmann/json.hpp>

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

int Failures{};

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++Failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

template <class Callable>
auto Throws(Callable&& callable) -> bool {
    try {
        callable();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

auto ReadFile(const char* path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open shipped configuration");
    }
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

void AppendAtlasU16(
        std::vector<std::uint8_t>& output,
        std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendAtlasU32(
        std::vector<std::uint8_t>& output,
        std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void AppendAtlasI32(
        std::vector<std::uint8_t>& output,
        std::int32_t value) {
    AppendAtlasU32(output, static_cast<std::uint32_t>(value));
}

void StoreAtlasU64(
        std::vector<std::uint8_t>& output,
        std::size_t offset,
        std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        output[offset + index] = static_cast<std::uint8_t>(
            value >> (index * 8U));
    }
}

void MixAtlasByte(std::uint64_t& digest, std::uint8_t value) {
    digest ^= value;
    digest *= UINT64_C(1099511628211);
}

void MixAtlasU32(std::uint64_t& digest, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        MixAtlasByte(digest, static_cast<std::uint8_t>(value >> shift));
    }
}

auto BuildExternalAtlasGeometryFixture() -> std::vector<std::uint8_t> {
    auto output = std::vector<std::uint8_t>{'M', 'S', 'A', '1'};
    AppendAtlasU16(
        output,
        RuffnecKk::MapSense::ExternalAtlasGeometryProtocolVersion);
    AppendAtlasU16(output, 1U);
    AppendAtlasU32(output, 1'395'822'899U);
    output.push_back(2U);
    output.push_back(2U);
    AppendAtlasU16(output, 0U);
    AppendAtlasU32(output, 2U);
    AppendAtlasU32(output, 3U);
    for (std::size_t index = 0U; index < 8U; ++index) {
        output.push_back(0U);
    }

    auto digest = UINT64_C(14695981039346656037);
    const auto appendLevel = [&output, &digest](
            std::int32_t levelId,
            std::uint8_t layer,
            std::uint32_t cellCount) {
        AppendAtlasI32(output, levelId);
        output.push_back(layer);
        output.insert(output.end(), 3U, 0U);
        AppendAtlasU32(output, cellCount);
        MixAtlasU32(digest, static_cast<std::uint32_t>(levelId));
        MixAtlasByte(digest, layer);
    };
    const auto appendCell = [&output, &digest](
            std::int32_t frame,
            std::int32_t tileX,
            std::int32_t tileY,
            bool wallTree,
            bool raised) {
        AppendAtlasI32(output, frame);
        AppendAtlasI32(output, tileX);
        AppendAtlasI32(output, tileY);
        output.push_back(wallTree ? 1U : 0U);
        output.push_back(raised ? 1U : 0U);
        output.insert(output.end(), 2U, 0U);
        MixAtlasU32(digest, static_cast<std::uint32_t>(frame));
        MixAtlasU32(digest, static_cast<std::uint32_t>(tileX));
        MixAtlasU32(digest, static_cast<std::uint32_t>(tileY));
        MixAtlasByte(digest, wallTree ? 1U : 0U);
        MixAtlasByte(digest, raised ? 1U : 0U);
    };
    appendLevel(75, 0U, 2U);
    appendCell(101, 4'000, 5'000, false, false);
    appendCell(102, 4'001, 5'000, true, false);
    appendLevel(76, 0U, 1U);
    appendCell(103, 4'002, 5'001, true, true);
    StoreAtlasU64(output, 24U, digest);
    return output;
}

class ScopedCatalogTestDirectory final {
public:
    explicit ScopedCatalogTestDirectory(std::string_view label) {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        for (std::uint32_t attempt{}; attempt < 64U; ++attempt) {
            path_ = base / (
                "ruffneckk-mapsense-catalog-" + std::string(label) + "-"
                + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error) continue;
        }
        throw std::runtime_error(
            "cannot create an isolated MapSense catalog test directory");
    }

    ScopedCatalogTestDirectory(const ScopedCatalogTestDirectory&) = delete;
    auto operator=(const ScopedCatalogTestDirectory&)
        -> ScopedCatalogTestDirectory& = delete;

    ~ScopedCatalogTestDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] auto Path() const noexcept
            -> const std::filesystem::path& {
        return path_;
    }

private:
    std::filesystem::path path_{};
};

void WriteCatalogFixture(
        const std::filesystem::path& path,
        std::string_view bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create catalog fixture directory");
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create catalog fixture");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write catalog fixture");
}

const std::map<std::string, std::string, std::less<>> CatalogTranslations{
    {"ExactCustomLevelName", "ExactCustomLevelName"},
    {"ItemStats1h", "Défense : %d"},
    {"LevelKey", "Sortie \xC3\x89preuve"},
    {"ShrineKey", "Sanctuaire d\xE2\x80\x99" "exp\xC3\xA9rience"},
    {"SuNameKey", "Unique supr\xC3\xAAme"},
    {"BossKey", "D\xC3\xA9mon majeur"},
};
bool EchoCatalogLocalizationKeys{};

auto __cdecl FakeCatalogGetStringByKey(
        const D2RL::PluginContext*,
        const char* key,
        char* output,
        std::uint32_t outputSize,
        std::uint32_t* requiredSize) noexcept
        -> D2RL::Localization::Result {
    if (key == nullptr || requiredSize == nullptr) {
        return D2RL::Localization::Result::InvalidArgument;
    }
    const auto found = CatalogTranslations.find(key);
    if (!EchoCatalogLocalizationKeys
        && found == CatalogTranslations.end()) {
        *requiredSize = 0U;
        return D2RL::Localization::Result::NotFound;
    }
    const std::string value = EchoCatalogLocalizationKeys
        ? std::string(key)
        : found->second;
    if (value.size()
            >= (std::numeric_limits<std::uint32_t>::max)()) {
        *requiredSize = 0U;
        return D2RL::Localization::Result::Unavailable;
    }
    const auto required = static_cast<std::uint32_t>(
        value.size() + 1U);
    *requiredSize = required;
    if (output == nullptr || outputSize < required) {
        return D2RL::Localization::Result::BufferTooSmall;
    }
    std::memcpy(output, value.c_str(), required);
    return D2RL::Localization::Result::Success;
}

const D2RL::LocalizationServiceV1 FakeCatalogLocalizationService{
    .serviceSize = D2RL::LocalizationServiceV1Size,
    .serviceVersion = D2RL::LocalizationServiceV1Version,
    .getStringById = nullptr,
    .getStringByKey = FakeCatalogGetStringByKey,
};

auto __cdecl FakeCatalogQueryService(
        const D2RL::PluginContext*,
        D2RL::ServiceId serviceId,
        std::uint32_t serviceVersion,
        const void** service) noexcept -> D2RL::ServiceQueryResult {
    if (service == nullptr) return D2RL::ServiceQueryResult::InvalidArgument;
    *service = nullptr;
    if (serviceId != D2RL::ServiceId::Localization) {
        return D2RL::ServiceQueryResult::UnknownService;
    }
    if (serviceVersion != D2RL::LocalizationServiceV1Version) {
        return D2RL::ServiceQueryResult::UnsupportedVersion;
    }
    *service = &FakeCatalogLocalizationService;
    return D2RL::ServiceQueryResult::Success;
}

const D2RL::PluginApi FakeCatalogPluginApi{
    .apiSize = D2RL::PluginApiSize,
    .queryService = FakeCatalogQueryService,
};

[[nodiscard]] auto MakeCatalogContext(
        const wchar_t* modDirectory,
        const char* activeMod = "TestMod") noexcept -> D2RL::PluginContext {
    D2RL::PluginContext context{};
    context.contextSize = sizeof(context);
    context.api = &FakeCatalogPluginApi;
    context.activeMod = activeMod;
    context.modDirectory = modDirectory;
    return context;
}

[[nodiscard]] auto HasCatalogDiagnostic(
        const RuffnecKk::MapSense::MapSenseDataCatalogLoadResult& result,
        std::string_view code) noexcept -> bool {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [code](const auto& diagnostic) { return diagnostic.code == code; });
}

void WriteCompleteCatalog(
        const std::filesystem::path& excel,
        std::string_view levelKey = "LevelKey") {
    WriteCatalogFixture(
        excel / "levels.txt",
        "Name\t*StringName\tId\tLevelName\tWaypoint\r\n"
        "Null\tNull\t0\t\t255\r\n"
        "Expansion\t\t\t\t\r\n"
        "Act 1 - Town\tPlayer Level\t12\t"
            + std::string(levelKey) + "\t0\r\n");
    WriteCatalogFixture(
        excel / "shrines.txt",
        "Name\tCode\tStringName\r\n"
        "None\t0\tNoneShrineKey\r\n"
        "Experience Shrine\t5\tShrineKey\r\n");
    WriteCatalogFixture(
        excel / "superuniques.txt",
        "Superunique\tName\tClass\thcIdx\r\n"
        "Expansion\t\t\t\r\n"
        "SUOne\tSuNameKey\tfallen\t7\r\n");
    WriteCatalogFixture(
        excel / "monstats.txt",
        "Id\t*hcIdx\tNameStr\tboss\tprimeevil\r\n"
        "Expansion\t\t\t\t\r\n"
        "DiabloMod\t42\tBossKey\t1\t1\r\n");
    WriteCatalogFixture(
        excel / "objects.txt",
        "Class\tName\t*ID\tSubClass\tLockable\tOperateFn\tPopulateFn\tInitFn\t"
        "ClientFn\tShrineFunction\r\n"
        "Expansion\t\t\t\t\t\t\t\t\t\r\n"
        "chest_mod\tChestKey\t99\t0\t1\t4\t3\t3\t0\t0\r\n");
    WriteCatalogFixture(
        excel / "missiles.txt",
        "Missile\tEType\tSrcDamage\tSrcMissDmg\tMinDamage\t"
        "MinLevDam1\tMinLevDam2\tMinLevDam3\tMinLevDam4\tMinLevDam5\t"
        "MaxDamage\tMaxLevDam1\tMaxLevDam2\tMaxLevDam3\tMaxLevDam4\t"
        "MaxLevDam5\r\n"
        "arrow\t\t128\t\t1\t0\t0\t0\t0\t0\t2\t0\t0\t0\t0\t0\r\n");
}

void WritePlacementCatalog(const std::filesystem::path& excel) {
    WriteCompleteCatalog(excel);
    WriteCatalogFixture(
        excel / "levels.txt",
        "Name\t*StringName\tId\tAct\tLayer\tSizeX\tSizeY\tSizeX(N)\t"
        "SizeY(N)\tSizeX(H)\tSizeY(H)\tOffsetX\tOffsetY\tDepend\t"
        "DrlgType\tLevelName\tWaypoint\r\n"
        "Null\tNull\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t\t255\r\n"
        "Fixed Custom\tFixed Custom\t138\t4\t98\t200\t200\t210\t220\t"
        "220\t240\t3000\t2100\t0\t1\tCustomLevelKey\t255\r\n"
        "Dependent Custom\tDependent Custom\t139\t4\t98\t40\t50\t60\t"
        "80\t70\t90\t10\t-20\t138\t2\tDependentLevelKey\t255\r\n"
        "Dynamic Custom\tDynamic Custom\t140\t4\t99\t-1\t-1\t-1\t-1\t"
        "-1\t-1\t-1\t-1\t0\t3\tDynamicLevelKey\t255\r\n"
        "Cycle A\tCycle A\t141\t4\t100\t20\t20\t20\t20\t20\t20\t"
        "1\t1\t142\t2\tCycleAKey\t255\r\n"
        "Cycle B\tCycle B\t142\t4\t100\t20\t20\t20\t20\t20\t20\t"
        "1\t1\t141\t2\tCycleBKey\t255\r\n");
}

void CheckMapSenseDataCatalogContract() {
    using namespace RuffnecKk::MapSense;
    try {
        CHECK(ClassifyDataCatalogMissileElement(0U, "", false)
            == DataCatalogMissileElement::Physical);
        CHECK(ClassifyDataCatalogMissileElement(2U, "ltng", false)
            == DataCatalogMissileElement::Lightning);
        CHECK(ClassifyDataCatalogMissileElement(18U, "", true)
            == DataCatalogMissileElement::Hidden);
        CHECK(ClassifyDataCatalogMissileElement(693U, "mag", false)
            == DataCatalogMissileElement::Poison);
        CHECK(ClassifyDataCatalogMissileElement(693U, "fire", false)
            == DataCatalogMissileElement::Fire);
        CHECK(ClassifyDataCatalogMissileElement(736U, "frze", false)
            == DataCatalogMissileElement::Cold);
        CHECK(ClassifyDataCatalogMissileElement(737U, "burn", false)
            == DataCatalogMissileElement::Fire);
        CHECK(ClassifyDataCatalogMissileElement(738U, "", true)
            == DataCatalogMissileElement::Physical);
        CHECK(ClassifyDataCatalogMissileElement(739U, "", false)
            == DataCatalogMissileElement::Hidden);
        CHECK(StockDataCatalogMissileElement(0U)
            == DataCatalogMissileElement::Physical);
        CHECK(StockDataCatalogMissileElement(18U)
            == DataCatalogMissileElement::Hidden);
        CHECK(StockDataCatalogMissileElement(736U)
            == DataCatalogMissileElement::Hidden);
        {
            const DataCatalogObject shrine{
                .subClass = 0x01U,
                .initFn = 1U,
            };
            const DataCatalogObject subclassOnly{.subClass = 0x01U};
            const DataCatalogObject ordinaryChest{.initFn = 3U};
            const DataCatalogObject betterChest{.initFn = 57U};
            const DataCatalogObject arcaneChest{
                .classId = "ArcaneChest3",
                .initFn = 3U,
            };
            const DataCatalogObject placeUniqueChest{
                .classId = "PlaceUniqueChest",
                .initFn = 79U,
            };
            const DataCatalogObject placeRandomTreasureChest{
                .classId = "PlaceRandomTreasureChest",
                .initFn = 79U,
            };
            const DataCatalogObject operateFourNonChest{
                .operateFn = 4U,
                .initFn = 45U,
            };
            const DataCatalogObject ordinaryWell{
                .subClass = 32U,
                .operateFn = 22U,
                .initFn = 16U,
            };
            CHECK(IsShrineObjectDefinition(shrine));
            CHECK(!IsShrineObjectDefinition(subclassOnly));
            CHECK(!IsChestObjectDefinition(shrine));
            CHECK(IsChestObjectDefinition(ordinaryChest));
            CHECK(IsChestObjectDefinition(betterChest));
            CHECK(!IsSpecialChestObjectDefinition(ordinaryChest));
            CHECK(IsSpecialChestObjectDefinition(betterChest));
            CHECK(IsSpecialChestObjectDefinition(arcaneChest));
            CHECK(IsSpecialChestPresetObjectDefinition(placeUniqueChest));
            CHECK(!IsSpecialChestPresetObjectDefinition(
                placeRandomTreasureChest));
            CHECK(!IsChestObjectDefinition(operateFourNonChest));
            CHECK(!IsShrineObjectDefinition(ordinaryWell));
            CHECK(!IsChestObjectDefinition(ordinaryWell));
            CHECK(!IsLockedChestInteractType(0x09U));
            CHECK(IsLockedChestInteractType(0x80U));
            CHECK(IsTrappedChestInteractType(0x01U));
            CHECK(IsTrappedChestInteractType(0x89U));
            CHECK(!IsTrappedChestInteractType(0x00U));
            CHECK(!IsTrappedChestInteractType(0x0AU));
            CHECK(IsSparklyChestRuntimeFlags(0x01U));
            CHECK(!IsSparklyChestRuntimeFlags(0x02U));
            CHECK(AutomapPoiWithinSubtileDistance(
                0,
                0,
                16 * NativeShrineLabelProximitySubtiles,
                8 * NativeShrineLabelProximitySubtiles,
                NativeShrineLabelProximitySubtiles));
            CHECK(!AutomapPoiWithinSubtileDistance(
                0,
                0,
                16 * (NativeShrineLabelProximitySubtiles + 1),
                8 * (NativeShrineLabelProximitySubtiles + 1),
                NativeShrineLabelProximitySubtiles));
            constexpr AutomapLabelRectangle firstLabel{
                10.0F, 20.0F, 110.0F, 40.0F};
            constexpr AutomapLabelRectangle overlappingLabel{
                100.0F, 22.0F, 180.0F, 42.0F};
            constexpr AutomapLabelRectangle separatedLabel{
                112.0F, 20.0F, 190.0F, 40.0F};
            static_assert(AutomapLabelRectanglesOverlap(
                firstLabel,
                overlappingLabel));
            static_assert(!AutomapLabelRectanglesOverlap(
                firstLabel,
                separatedLabel));
            static_assert(AutomapLabelRectanglesOverlap(
                firstLabel,
                separatedLabel,
                3.0F));
            CHECK(AutomapLabelRectanglesOverlap(
                firstLabel,
                overlappingLabel));
            CHECK(!AutomapLabelRectanglesOverlap(
                firstLabel,
                separatedLabel));
            CHECK(AutomapLabelRectanglesOverlap(
                firstLabel,
                separatedLabel,
                3.0F));
        }

        {
            ScopedCatalogTestDirectory directory("active");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            const auto vanillaExcel = directory.Path() / "vanilla";
            WriteCompleteCatalog(activeExcel);
            WriteCompleteCatalog(vanillaExcel, "VanillaLevelKey");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            MapSenseDataCatalogLoadOptions options{};
            options.vanillaExcelDirectories.push_back(vanillaExcel);
            const auto result = MapSenseDataCatalog::Load(&context, options);
            CHECK(result);
            CHECK(result.catalog->HasLocalizationService());
            CHECK(result.catalog->HasVerifiedPlayerFacingLocalization());
            CHECK(result.catalog->AllFamiliesAvailable());
            CHECK(result.catalog->ActiveExcelDirectories().size() == 2U);
            CHECK(result.catalog->ActiveTileDirectories().size() == 2U);
            CHECK(result.catalog->AtlasDataFingerprint() != 0U);
            for (std::size_t familyIndex = 0U;
                    familyIndex < result.catalog->FamilyStatuses().size();
                    ++familyIndex) {
                const auto& status =
                    result.catalog->FamilyStatuses()[familyIndex];
                CHECK(status.state == DataCatalogFamilyState::ActiveTxt);
                const auto expectedRows = familyIndex
                        == static_cast<std::size_t>(DataCatalogFamily::Shrines)
                    ? 2U
                    : 1U;
                CHECK(status.rowCount == expectedRows);
            }

            const auto* level = result.catalog->FindLevel(12);
            CHECK(level != nullptr);
            CHECK(level != nullptr && level->name.key == "LevelKey");
            CHECK(level != nullptr && level->name.localized);
            CHECK(level != nullptr
                && level->name.utf8 == "Sortie \xC3\x89preuve");
            CHECK(level != nullptr
                && level->waypointLabelUtf8
                    == "Sortie \xC3\x89preuve Waypoint");
            CHECK(level != nullptr && level->hasWaypoint);
            CHECK(level != nullptr && level->act == -1);
            CHECK(level != nullptr && !level->hasAtlasPlacement);
            CHECK(result.catalog->Levels().size() == 1U);
            DataCatalogLevelAtlasPlacement omittedPlacement{};
            CHECK(!result.catalog->ResolveLevelAtlasPlacement(
                12, 0U, omittedPlacement));
            CHECK(result.catalog->FindLevel(13) == nullptr);

            const auto* const noneShrine =
                result.catalog->FindShrineByRow(0U);
            CHECK(noneShrine != nullptr);
            CHECK(noneShrine != nullptr && noneShrine->code == 0U);
            CHECK(result.catalog->FindShrineByInteractType(0U) == nullptr);

            const auto* shrine = result.catalog->FindShrineByInteractType(1U);
            CHECK(shrine != nullptr);
            CHECK(shrine != nullptr && shrine->code == 5U);
            CHECK(shrine != nullptr && shrine->name.localized);
            CHECK(shrine != nullptr
                && shrine->name.utf8
                    == "Sanctuaire d\xE2\x80\x99" "exp\xC3\xA9rience");
            const auto shrineRows = result.catalog->ShrineRowsForCode(5U);
            CHECK(shrineRows.size() == 1U);
            CHECK(!shrineRows.empty() && shrineRows.front() == 1U);

            const auto* superUnique = result.catalog->FindSuperUnique(7U);
            CHECK(superUnique != nullptr);
            CHECK(superUnique != nullptr
                && superUnique->name.utf8 == "Unique supr\xC3\xAAme");
            // Runtime IDs are row ordinals. The descriptive *hcIdx/*ID
            // comments (42/99 in these fixtures) must not drive lookup.
            const auto* boss = result.catalog->FindMonStats(0U);
            CHECK(boss != nullptr);
            CHECK(boss != nullptr && boss->primeEvil);
            CHECK(boss != nullptr
                && boss->name.utf8 == "D\xC3\xA9mon majeur");
            CHECK(result.catalog->FindMonStats(42U) == nullptr);
            const auto* object = result.catalog->FindObjectById(0U);
            CHECK(object != nullptr);
            CHECK(object == result.catalog->FindObject("chest_mod"));
            CHECK(object != nullptr && object->lockable);
            CHECK(object != nullptr && object->subClass == 0U);
            CHECK(object != nullptr && object->operateFn == 4U);
            CHECK(object != nullptr && IsChestObjectDefinition(*object));
            CHECK(object != nullptr && !IsShrineObjectDefinition(*object));
            CHECK(object != nullptr && !object->name.localized);
            CHECK(object != nullptr && object->name.utf8 == "ChestKey");
            CHECK(result.catalog->FindObjectById(99U) == nullptr);
            const auto* missile = result.catalog->FindMissile(0U);
            CHECK(missile != nullptr);
            CHECK(missile != nullptr && missile->id == "arrow");
            CHECK(missile != nullptr
                && missile->element
                    == DataCatalogMissileElement::Physical);
            CHECK(result.catalog->FindMissile(1U) == nullptr);
        }

        {
            // An arbitrary mod level may use a player-facing localization key
            // whose value is byte-identical in the current language. Keep the
            // result independent of row order: the exact match deliberately
            // appears before the different translation that proves the D2R
            // language service is ready.
            ScopedCatalogTestDirectory directory("exact-custom-level-name");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            WriteCompleteCatalog(activeExcel);
            WriteCatalogFixture(
                activeExcel / "levels.txt",
                "Name\t*StringName\tId\tLevelName\tWaypoint\r\n"
                "Null\tNull\t0\t\t255\r\n"
                "Expansion\t\t\t\t\r\n"
                "Arbitrary Custom Level\tCustom Display Comment\t733\t"
                    "ExactCustomLevelName\t255\r\n"
                "Act 1 - Town\tPlayer Level\t12\tLevelKey\t0\r\n");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            const auto result = MapSenseDataCatalog::Load(&context);
            CHECK(result);
            CHECK(result.catalog->HasVerifiedPlayerFacingLocalization());
            const auto* custom = result.catalog->FindLevel(733);
            CHECK(custom != nullptr);
            CHECK(custom != nullptr && custom->name.localized);
            CHECK(custom != nullptr
                && custom->name.utf8 == "ExactCustomLevelName");
            const auto& status = result.catalog->FamilyStatus(
                DataCatalogFamily::Levels);
            CHECK(status.localizedNameCount == 2U);
            CHECK(status.unresolvedNameCount == 0U);
        }

        {
            ScopedCatalogTestDirectory directory("level-placement");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            WritePlacementCatalog(activeExcel);
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            const auto result = MapSenseDataCatalog::Load(&context);
            CHECK(result);
            CHECK(result.catalog->Levels().size() == 5U);

            const auto* fixed = result.catalog->FindLevel(138);
            CHECK(fixed != nullptr);
            CHECK(fixed != nullptr && fixed->act == 4);
            CHECK(fixed != nullptr && fixed->layer == 98);
            CHECK(fixed != nullptr && fixed->hasAtlasPlacement);
            DataCatalogLevelAtlasPlacement placement{};
            CHECK(result.catalog->ResolveLevelAtlasPlacement(
                138, 1U, placement));
            CHECK(placement.originSubtileX == 15'000);
            CHECK(placement.originSubtileY == 10'500);
            CHECK(placement.anchorSubtileX == 15'525);
            CHECK(placement.anchorSubtileY == 11'050);

            CHECK(result.catalog->ResolveLevelAtlasPlacement(
                139, 1U, placement));
            CHECK(placement.originSubtileX == 15'050);
            CHECK(placement.originSubtileY == 10'400);
            CHECK(placement.anchorSubtileX == 15'200);
            CHECK(placement.anchorSubtileY == 10'600);

            CHECK(!result.catalog->ResolveLevelAtlasPlacement(
                140, 0U, placement));
            CHECK(!result.catalog->ResolveLevelAtlasPlacement(
                141, 0U, placement));
            CHECK(!result.catalog->ResolveLevelAtlasPlacement(
                138, 3U, placement));
        }

        {
            // LocalizationService is callable before D2R's language tables are
            // ready and then echoes keys while reporting Success. Technical
            // LevelName/ShrId values must remain unresolved, never drawable.
            ScopedCatalogTestDirectory directory("localization-key-echo");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            WriteCompleteCatalog(activeExcel, "Cellar of Pity");
            WriteCatalogFixture(
                activeExcel / "levels.txt",
                "Name\t*StringName\tId\tLevelName\r\n"
                "Null\tNull\t0\t\r\n"
                "Expansion\t\t\t\r\n"
                "Act 5 - Frozen River\tFrozen River\t114\tCellar of Pity\r\n");
            WriteCatalogFixture(
                activeExcel / "shrines.txt",
                "Name\tCode\tStringName\r\n"
                "None\t0\tShrId0\r\n"
                "Resist Cold Shrine\t9\tShrId9\r\n");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            EchoCatalogLocalizationKeys = true;
            const auto result = MapSenseDataCatalog::Load(&context);
            EchoCatalogLocalizationKeys = false;
            CHECK(result);
            CHECK(result.catalog->HasLocalizationService());
            CHECK(!result.catalog->HasVerifiedPlayerFacingLocalization());
            const auto* level = result.catalog->FindLevel(114);
            CHECK(level != nullptr);
            CHECK(level != nullptr && level->name.key == "Cellar of Pity");
            CHECK(level != nullptr && !level->name.localized);
            const auto* shrine = result.catalog->FindShrineByInteractType(1U);
            CHECK(shrine != nullptr);
            CHECK(shrine != nullptr && shrine->name.key == "ShrId9");
            CHECK(shrine != nullptr && !shrine->name.localized);
        }

        {
            // Official fallback tables contain technical rows with no display
            // key, and comment columns may be omitted entirely by a valid mod.
            ScopedCatalogTestDirectory directory("blank-technical-rows");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            WriteCompleteCatalog(activeExcel);
            WriteCatalogFixture(
                activeExcel / "monstats.txt",
                "Id\tNameStr\tboss\tprimeevil\r\n"
                "Expansion\t\t\t\r\n"
                "TechnicalMonster\t\t0\t0\r\n"
                "DiabloMod\tBossKey\t1\t1\r\n");
            WriteCatalogFixture(
                activeExcel / "objects.txt",
                "Class\tName\tSubClass\tLockable\tOperateFn\tPopulateFn\t"
                "InitFn\tClientFn\tShrineFunction\r\n"
                "Expansion\t\t\t\t\t\t\t\t\r\n"
                "technical_object\t\t0\t0\t0\t0\t0\t0\t0\r\n"
                "chest_mod\tChestKey\t0\t1\t4\t3\t3\t0\t0\r\n");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            const auto result = MapSenseDataCatalog::Load(&context);
            CHECK(result);
            CHECK(result.catalog->AllFamiliesAvailable());
            const auto* technicalMonster =
                result.catalog->FindMonStats(0U);
            CHECK(technicalMonster != nullptr);
            CHECK(technicalMonster != nullptr
                && technicalMonster->name.key.empty());
            const auto* blankObject = result.catalog->FindObjectById(0U);
            CHECK(blankObject != nullptr);
            CHECK(blankObject != nullptr && blankObject->name.key.empty());
            const auto* ordinalBoss = result.catalog->FindMonStats(1U);
            CHECK(ordinalBoss != nullptr && ordinalBoss->primeEvil);
            CHECK(ordinalBoss != nullptr
                && ordinalBoss->name.utf8 == "D\xC3\xA9mon majeur");
            CHECK(result.catalog->FindObjectById(1U) != nullptr);
            CHECK(result.catalog->FamilyStatus(DataCatalogFamily::MonStats)
                .unresolvedNameCount == 0U);
            CHECK(result.catalog->FamilyStatus(DataCatalogFamily::Objects)
                .unresolvedNameCount == 1U);
        }

        {
            // An unrelated BKVince directory must never become an implicit
            // fallback for the active mod selected by D2RLoader.
            ScopedCatalogTestDirectory directory("no-bk-fallback");
            WriteCatalogFixture(
                directory.Path() / "BKVince.mpq" / "data" / "global"
                    / "excel" / "levels.txt",
                "Name\tId\tLevelName\r\n"
                "Act 1 - Town\t12\tLevelKey\r\n");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            const auto result = MapSenseDataCatalog::Load(&context);
            CHECK(result);
            CHECK(result.catalog->FamilyStatus(DataCatalogFamily::Levels).state
                == DataCatalogFamilyState::Unavailable);
            CHECK(result.catalog->FindLevel(12) == nullptr);
        }

        {
            // A compiled override without its auditable TXT source blocks
            // fallback for that family instead of silently using stale data.
            ScopedCatalogTestDirectory directory("binary-only");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            const auto vanillaExcel = directory.Path() / "vanilla";
            WriteCatalogFixture(activeExcel / "levels.bin", "binary");
            WriteCatalogFixture(
                vanillaExcel / "levels.txt",
                "Name\tId\tLevelName\r\n"
                "Act 1 - Town\t12\tLevelKey\r\n");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            MapSenseDataCatalogLoadOptions options{};
            options.vanillaExcelDirectories.push_back(vanillaExcel);
            const auto result = MapSenseDataCatalog::Load(&context, options);
            CHECK(result);
            CHECK(result.catalog->FamilyStatus(DataCatalogFamily::Levels).state
                == DataCatalogFamilyState::BinaryOnlyConflict);
            CHECK(result.catalog->FindLevel(12) == nullptr);
            CHECK(HasCatalogDiagnostic(result, "binary_without_txt"));
        }

        {
            ScopedCatalogTestDirectory directory("invalid");
            const auto activeExcel = directory.Path()
                / "TestMod.mpq" / "data" / "global" / "excel";
            WriteCatalogFixture(
                activeExcel / "levels.txt",
                "Name\tId\tLevelName\r\n"
                "Act 1 - Town\t12\tLevelKey\r\n"
                "Act 1 - Duplicate\t12\tOtherKey\r\n");
            WriteCatalogFixture(
                activeExcel / "shrines.txt",
                "Code\tWrongHeader\r\n5\tShrineKey\r\n");
            const auto root = directory.Path().wstring();
            const auto context = MakeCatalogContext(root.c_str());
            const auto result = MapSenseDataCatalog::Load(&context);
            CHECK(result);
            CHECK(result.catalog->FamilyStatus(DataCatalogFamily::Levels).state
                == DataCatalogFamilyState::Invalid);
            CHECK(result.catalog->FamilyStatus(DataCatalogFamily::Shrines).state
                == DataCatalogFamilyState::Invalid);
            CHECK(result.catalog->FindLevel(12) == nullptr);
            CHECK(result.catalog->FindShrineByRow(0U) == nullptr);
            CHECK(HasCatalogDiagnostic(result, "table_parse_failed"));
        }
    } catch (const std::exception& exception) {
        std::cerr << "FAIL MapSense data catalog: " << exception.what()
                  << '\n';
        ++Failures;
    }
}

auto ProjectNavigationClientIdentity(
        void*,
        std::int32_t clientX,
        std::int32_t clientY,
        RuffnecKk::MapSense::NavigationNativePoint& output) noexcept -> bool {
    output = {.x = clientX, .y = clientY};
    return true;
}

void CheckNavigationProjectionDiagnosticCacheContract() {
    using RuffnecKk::MapSense::Detail::
        MaximumNavigationProjectionDiagnosticEntries;
    using RuffnecKk::MapSense::Detail::
        NavigationProjectionDiagnosticCache;

    NavigationProjectionDiagnosticCache cache;
    constexpr auto waypointId = UINT64_C(18214375364822279195);
    constexpr auto progressionId = UINT64_C(17344995562435206827);
    static_assert(waypointId % 16U == progressionId % 16U);

    CHECK(cache.ShouldLog(111, 0U, waypointId, 101U));
    CHECK(cache.ShouldLog(111, 1U, progressionId, 202U));
    CHECK(!cache.ShouldLog(111, 0U, waypointId, 101U));
    CHECK(!cache.ShouldLog(111, 1U, progressionId, 202U));
    CHECK(cache.ShouldLog(111, 0U, waypointId, 303U));

    CHECK(cache.ShouldLog(123, 0U, waypointId, 101U));
    CHECK(cache.ShouldLog(123, 1U, progressionId, 202U));

    cache.Reset();
    for (std::size_t index = 0U;
            index < MaximumNavigationProjectionDiagnosticEntries;
            ++index) {
        CHECK(cache.ShouldLog(
            7,
            static_cast<std::uint8_t>(index % 4U),
            static_cast<std::uint64_t>(index),
            static_cast<std::uint64_t>(index + 1U)));
    }
    CHECK(!cache.ShouldLog(7, 0U, UINT64_MAX, UINT64_MAX));
}

void CheckRevealPersistenceContract() {
    using namespace RuffnecKk::MapSense;

    RevealPersistenceState state;
    state.ResetProcess();
    state.BeginSession(101U);

    CHECK(state.Difficulty() == UnknownRevealDifficulty);
    CHECK(state.ObserveDifficulty(UnknownRevealDifficulty)
        == RevealDifficultyObservation::Invalid);
    CHECK(!state.HasAnyIntent());
    CHECK(!state.RememberLevel(0, 3));
    CHECK(!state.RememberAct(0, 0));
    CHECK(!state.SetRevealAll(0, true));

    CHECK(state.ObserveDifficulty(0)
        == RevealDifficultyObservation::Initialized);
    CHECK(state.RememberLevel(0, 3));
    CHECK(state.RememberLevel(0, 3));
    CHECK(state.RememberAct(0, 0));
    CHECK(state.RememberAct(0, 0));
    CHECK(state.SetRevealAll(0, true));
    CHECK(state.HasAnyIntent(0));
    CHECK(state.ShouldReplayLevel(0, 3));
    CHECK(state.HasReplayIntentForLevel(0, 0, 3));
    CHECK(state.HasReplayIntentForLevel(0, 1, 40));
    CHECK(state.ShouldReplayCurrentLevel(0, 0, 3));
    CHECK(state.ShouldReplayCurrentLevel(0, 1, 40));
    CHECK(state.ShouldRevealWholeAct(0, 0));
    CHECK(state.ShouldRevealWholeAct(0, 1));

    CHECK(state.MarkLevelAccepted(0, 3));
    CHECK(!state.ShouldReplayLevel(0, 3));
    CHECK(state.ShouldReplayCurrentLevel(0, 0, 3));
    CHECK(state.MarkActAccepted(0, 0));
    CHECK(!state.ShouldReplayCurrentLevel(0, 0, 3));
    CHECK(state.IsActAccepted(0, 0));
    // Whole-act command acceptance never substitutes for direct confirmation
    // of each current level. This is the Save & Exit regression guard.
    CHECK(state.ShouldReplayCurrentLevel(0, 0, 4));
    CHECK(state.MarkLevelAccepted(0, 4));
    CHECK(!state.ShouldReplayCurrentLevel(0, 0, 4));
    CHECK(state.IsLevelAccepted(0, 3));

    // Re-entering the same session cannot accidentally reopen accepted work.
    state.BeginSession(101U);
    CHECK(!state.ShouldReplayLevel(0, 3));
    CHECK(!state.ShouldReplayCurrentLevel(0, 0, 3));

    // GameLeft clears only the session generation. Its process-lifetime
    // intent must survive the Save & Exit boundary unchanged.
    state.BeginSession(0U);
    CHECK(state.HasAnyIntent(0));
    CHECK(!state.ShouldReplayLevel(0, 3));

    // A new game in the same difficulty replays stable ids on fresh geometry.
    state.BeginSession(102U);
    CHECK(state.ShouldReplayLevel(0, 3));
    CHECK(state.ShouldReplayCurrentLevel(0, 0, 3));
    CHECK(state.ShouldReplayCurrentLevel(0, 1, 40));

    // Unknown difficulty is fail-closed and cannot consume or reuse the state.
    CHECK(state.ObserveDifficulty(UnknownRevealDifficulty)
        == RevealDifficultyObservation::Invalid);
    CHECK(!state.ShouldReplayLevel(UnknownRevealDifficulty, 3));
    CHECK(!state.ShouldReplayCurrentLevel(
        UnknownRevealDifficulty, 0, 3));
    CHECK(state.ShouldReplayLevel(0, 3));

    // A real act transition cannot credit the accepted old-act reveal to the
    // newly entered act. The current DRLG's LevelId supplies the stable ActId.
    CHECK(RevealActForLevelId(39) == 0);
    CHECK(RevealActForLevelId(40) == 1);
    CHECK(RevealActForLevelId(74) == 1);
    CHECK(RevealActForLevelId(75) == 2);
    CHECK(RevealActForLevelId(102) == 2);
    CHECK(RevealActForLevelId(103) == 3);
    CHECK(RevealActForLevelId(108) == 3);
    CHECK(RevealActForLevelId(109) == 4);
    CHECK(RevealActForLevelId(137) == 4);
    CHECK(RevealActForLevelId(0) == -1);
    CHECK(RevealActForLevelId(138) == -1);
    CHECK(state.ShouldReplayCurrentLevel(
        0, RevealActForLevelId(39), 39));
    CHECK(state.MarkActAccepted(0, RevealActForLevelId(39)));
    CHECK(state.ShouldReplayCurrentLevel(
        0, RevealActForLevelId(39), 39));
    CHECK(state.MarkLevelAccepted(0, 39));
    CHECK(!state.ShouldReplayCurrentLevel(
        0, RevealActForLevelId(39), 39));
    CHECK(state.ShouldReplayCurrentLevel(
        0, RevealActForLevelId(40), 40));

    // Every real 0/1/2 transition invalidates all remembered intents.
    CHECK(state.ObserveDifficulty(1)
        == RevealDifficultyObservation::Changed);
    CHECK(state.Difficulty() == 1);
    CHECK(!state.HasAnyIntent());
    CHECK(!state.ShouldReplayLevel(1, 3));
    CHECK(!state.ShouldReplayCurrentLevel(1, 0, 3));

    CHECK(state.RememberLevel(1, 40));
    CHECK(state.RememberAct(1, 2));
    CHECK(state.SetRevealAll(1, true));
    CHECK(state.ObserveDifficulty(2)
        == RevealDifficultyObservation::Changed);
    CHECK(state.Difficulty() == 2);
    CHECK(!state.HasAnyIntent());

    CHECK(state.RememberLevel(2, 109));
    CHECK(state.RememberAct(2, 4));
    CHECK(state.SetRevealAll(2, true));
    CHECK(state.ObserveDifficulty(0)
        == RevealDifficultyObservation::Changed);
    CHECK(state.Difficulty() == 0);
    CHECK(!state.HasAnyIntent());
}

void CheckRevealReplayRequestPolicy() {
    using namespace RuffnecKk::MapSense;

    RevealReplayRequestState pending{
        .sessionGeneration = 77U,
        .targetLevelId = 40,
        .retriesRemaining = 3U,
        .playerReady = true,
        .automapObserved = false,
        .reconcilePending = true,
    };
    CHECK(MergePendingRevealAutomapObservation(pending, 40, true));
    CHECK(pending.automapObserved);

    pending.automapObserved = false;
    CHECK(MergePendingRevealAutomapObservation(
        pending,
        UnknownRevealLevelId,
        false));
    CHECK(!pending.automapObserved);
    CHECK(!MergePendingRevealAutomapObservation(pending, 41, true));
    CHECK(!pending.automapObserved);

    pending.reconcilePending = false;
    CHECK(!MergePendingRevealAutomapObservation(pending, 40, true));
    CHECK(!CanConfirmReplayedLevelReveal(
        false,
        RevealOutcome::Complete));
    CHECK(!CanConfirmReplayedLevelReveal(
        true,
        RevealOutcome::Unavailable));
    CHECK(CanConfirmReplayedLevelReveal(
        true,
        RevealOutcome::Complete));

    const auto waiting = MakeRevealReplaySubmissionPolicy(
        false,
        true,
        false,
        false);
    CHECK(waiting.waitForAutomap);
    CHECK(!waiting.submitWholeAct);
    CHECK(!waiting.submitCurrentLevel);

    const auto wholeAct = MakeRevealReplaySubmissionPolicy(
        true,
        true,
        false,
        false);
    CHECK(!wholeAct.waitForAutomap);
    CHECK(wholeAct.submitWholeAct);
    CHECK(!wholeAct.submitCurrentLevel);

    const auto oneLevel = MakeRevealReplaySubmissionPolicy(
        true,
        false,
        false,
        false);
    CHECK(!oneLevel.waitForAutomap);
    CHECK(!oneLevel.submitWholeAct);
    CHECK(oneLevel.submitCurrentLevel);

    const auto alreadyAccepted = MakeRevealReplaySubmissionPolicy(
        true,
        true,
        true,
        true);
    CHECK(!alreadyAccepted.waitForAutomap);
    CHECK(!alreadyAccepted.submitWholeAct);
    CHECK(!alreadyAccepted.submitCurrentLevel);

    // A completed act traversal cannot suppress the local fallback for a
    // LevelId that the generated Vis graph did not reach. This is the
    // Lower Kurast/Kurast Bazaar disappearance regression guard.
    const auto acceptedActNewLevel = MakeRevealReplaySubmissionPolicy(
        true,
        true,
        true,
        false);
    CHECK(!acceptedActNewLevel.waitForAutomap);
    CHECK(!acceptedActNewLevel.submitWholeAct);
    CHECK(acceptedActNewLevel.submitCurrentLevel);
}

void CheckProgressiveRevealGraphContract() {
    using namespace RuffnecKk::MapSense;

    ProgressiveRevealGraphState graph;
    CHECK(graph.Begin(76));
    CHECK(graph.HasCurrent());
    CHECK(graph.Current() == 76);
    CHECK(graph.LevelCount() == 1U);
    CHECK(graph.Cursor() == 0U);

    CHECK(graph.Add(84));
    CHECK(graph.Add(85));
    CHECK(graph.Add(84));
    CHECK(graph.LevelCount() == 3U);
    CHECK(graph.LevelAt(0U) == 76);
    CHECK(graph.LevelAt(1U) == 84);
    CHECK(graph.LevelAt(2U) == 85);
    CHECK(graph.LevelAt(3U) == UnknownRevealLevelId);
    CHECK(graph.Contains(76));
    CHECK(graph.Contains(84));
    CHECK(graph.Contains(85));
    CHECK(!graph.Contains(83));
    CHECK(!graph.Add(UnknownRevealLevelId));
    CHECK(!graph.Add(0));

    CHECK(graph.Advance());
    CHECK(graph.Current() == 84);
    CHECK(graph.Advance());
    CHECK(graph.Current() == 85);
    CHECK(graph.Advance());
    CHECK(!graph.HasCurrent());
    CHECK(graph.Current() == UnknownRevealLevelId);
    CHECK(!graph.Advance());

    graph.Reset();
    CHECK(!graph.HasCurrent());
    CHECK(graph.LevelCount() == 0U);
    CHECK(graph.Begin(1));
    for (std::int32_t levelId = 2;
            levelId <= static_cast<std::int32_t>(
                ProgressiveRevealLevelCapacity);
            ++levelId) {
        CHECK(graph.Add(levelId));
    }
    CHECK(graph.LevelCount() == ProgressiveRevealLevelCapacity);
    CHECK(graph.LevelAt(ProgressiveRevealLevelCapacity - 1U)
        == static_cast<std::int32_t>(ProgressiveRevealLevelCapacity));
    CHECK(graph.LevelAt(ProgressiveRevealLevelCapacity)
        == UnknownRevealLevelId);
    CHECK(!graph.Add(static_cast<std::int32_t>(
        ProgressiveRevealLevelCapacity + 1U)));
}

void CheckExternalLabelProviderCoordinatorPolicy() {
    using namespace RuffnecKk::MapSense;

    const ExternalLabelProviderRequestIdentity current{
        .sessionGeneration = 7U,
        .seed = 1'395'822'899U,
        .difficulty = 2U,
        .act = 2,
        .currentLevelId = 75,
        .dataFingerprint = UINT64_C(0x123456789ABCDEF0),
    };
    auto nextAct = current;
    nextAct.act = 4;
    nextAct.currentLevelId = 109;
    const std::optional<ExternalLabelProviderRequestIdentity> none;

    CHECK(ExternalLabelProviderOperationTimeoutMilliseconds(
        ExternalLabelProviderOperation::Labels) == 5'000U);
    CHECK(ExternalLabelProviderOperationTimeoutMilliseconds(
        ExternalLabelProviderOperation::PrimaryGeometry) == 30'000U);
    CHECK(ExternalLabelProviderOperationTimeoutMilliseconds(
        ExternalLabelProviderOperation::PrewarmGeometry) == 30'000U);
    CHECK(DecideExternalLabelProviderSubmission(
        current, none, none, none, false,
        ExternalLabelProviderOperation::None)
        == ExternalLabelProviderSubmission::Queue);

    // Exact replays retain the in-flight transaction serial instead of
    // queuing a self-staling copy while geometry is still being generated.
    CHECK(DecideExternalLabelProviderSubmission(
        current, none, current, none, false,
        ExternalLabelProviderOperation::PrimaryGeometry)
        == ExternalLabelProviderSubmission::Duplicate);
    CHECK(DecideExternalLabelProviderSubmission(
        current, current, none, none, false,
        ExternalLabelProviderOperation::PrewarmGeometry)
        == ExternalLabelProviderSubmission::Duplicate);
    CHECK(DecideExternalLabelProviderSubmission(
        current, none, none, current, false,
        ExternalLabelProviderOperation::None)
        == ExternalLabelProviderSubmission::Duplicate);
    CHECK(DecideExternalLabelProviderSubmission(
        current, none, none, none, true,
        ExternalLabelProviderOperation::None)
        == ExternalLabelProviderSubmission::Duplicate);

    // A real transition preempts both primary and speculative helper work.
    CHECK(DecideExternalLabelProviderSubmission(
        nextAct, current, current, none, false,
        ExternalLabelProviderOperation::PrimaryGeometry)
        == ExternalLabelProviderSubmission::QueueAndCancel);
    CHECK(DecideExternalLabelProviderSubmission(
        nextAct, current, none, none, false,
        ExternalLabelProviderOperation::PrewarmGeometry)
        == ExternalLabelProviderSubmission::QueueAndCancel);

    CHECK(ShouldContinueExternalAtlasPrewarm(
        false, false, 11U, 11U, 7U, 7U));
    CHECK(!ShouldContinueExternalAtlasPrewarm(
        false, true, 11U, 11U, 7U, 7U));
    CHECK(!ShouldContinueExternalAtlasPrewarm(
        false, false, 11U, 12U, 7U, 7U));
    CHECK(!ShouldContinueExternalAtlasPrewarm(
        false, false, 11U, 11U, 7U, 8U));

    CHECK(DecideExternalLabelProviderCompletion(
        true, true, true, true)
        == ExternalLabelProviderCompletion::Publish);
    CHECK(DecideExternalLabelProviderCompletion(
        true, true, false, false)
        == ExternalLabelProviderCompletion::Failed);
    CHECK(DecideExternalLabelProviderCompletion(
        false, true, true, true)
        == ExternalLabelProviderCompletion::Stale);
}

void CheckExternalAtlasTopologyContract() {
    using namespace RuffnecKk::MapSense;

    // Act III exterior levels share one coordinate space through seam edges.
    // The dungeon interiors and their reciprocal links use warp edges and must
    // never enter the visible geometry component.
    const std::array edges{
        ExternalAtlasTopologyEdge{75, 76, 1},
        ExternalAtlasTopologyEdge{76, 77, 1},
        ExternalAtlasTopologyEdge{77, 78, 1},
        ExternalAtlasTopologyEdge{78, 79, 1},
        ExternalAtlasTopologyEdge{79, 80, 1},
        ExternalAtlasTopologyEdge{80, 81, 1},
        ExternalAtlasTopologyEdge{81, 82, 1},
        ExternalAtlasTopologyEdge{82, 83, 1},
        ExternalAtlasTopologyEdge{76, 84, 0},
        ExternalAtlasTopologyEdge{84, 76, 0},
        ExternalAtlasTopologyEdge{80, 92, 0},
        ExternalAtlasTopologyEdge{92, 80, 0},
    };
    std::vector<std::int32_t> visible;
    CHECK(CollectExternalAtlasVisibleLevels(
        75,
        edges.data(),
        edges.size(),
        visible));
    const std::vector<std::int32_t> expected{
        75, 76, 77, 78, 79, 80, 81, 82, 83,
    };
    CHECK(visible == expected);
    CHECK(std::find(visible.begin(), visible.end(), 84) == visible.end());
    CHECK(std::find(visible.begin(), visible.end(), 92) == visible.end());

    // Starting inside a dungeon does not import the exterior through its warp.
    CHECK(CollectExternalAtlasVisibleLevels(
        84,
        edges.data(),
        edges.size(),
        visible));
    CHECK(visible == std::vector<std::int32_t>{84});
    CHECK(!CollectExternalAtlasVisibleLevels(0, nullptr, 0U, visible));
    CHECK(visible.empty());

    // BKVince keeps the stock Uber-branch topology where level 133 has a
    // warp target at Act I level 17. A disconnected warp target is valid even
    // when it is outside this generated act (or is a future mod-defined id),
    // but a continuous seam may never cross the atlas membership boundary.
    const std::array<std::int32_t, 2> actFiveLevels{109, 133};
    CHECK(IsExternalAtlasTopologyEdgeValid(
        actFiveLevels,
        ExternalAtlasTopologyEdge{133, 17, 0}));
    CHECK(IsExternalAtlasTopologyEdgeValid(
        actFiveLevels,
        ExternalAtlasTopologyEdge{133, 733, 0}));
    CHECK(!IsExternalAtlasTopologyEdgeValid(
        actFiveLevels,
        ExternalAtlasTopologyEdge{133, 17, 1}));
    CHECK(!IsExternalAtlasTopologyEdgeValid(
        actFiveLevels,
        ExternalAtlasTopologyEdge{146, 133, 0}));
    CHECK(!IsExternalAtlasTopologyEdgeValid(
        actFiveLevels,
        ExternalAtlasTopologyEdge{133, 0, 0}));
}

void CheckExternalAtlasGeometryContract() {
    using namespace RuffnecKk::MapSense;

    CHECK(IsExternalAtlasStandardCampaignLevel(0U, 1));
    CHECK(IsExternalAtlasStandardCampaignLevel(0U, 39));
    CHECK(!IsExternalAtlasStandardCampaignLevel(0U, 40));
    CHECK(IsExternalAtlasStandardCampaignLevel(4U, 109));
    CHECK(IsExternalAtlasStandardCampaignLevel(4U, 132));
    CHECK(!IsExternalAtlasStandardCampaignLevel(4U, 133));
    CHECK(!IsExternalAtlasStandardCampaignLevel(4U, 136));
    CHECK(!IsExternalAtlasStandardCampaignLevel(4U, 138));
    CHECK(!IsExternalAtlasStandardCampaignLevel(5U, 109));

    const auto fixture = BuildExternalAtlasGeometryFixture();
    ExternalAtlasGeometry atlas;
    ExternalAtlasGeometryParseError error{};
    CHECK(ParseExternalAtlasGeometry(
        fixture,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::None);
    CHECK(atlas.seed == 1'395'822'899U);
    CHECK(atlas.difficulty == 2U);
    CHECK(atlas.act == 2U);
    CHECK(atlas.levels.size() == 2U);
    CHECK(atlas.cells.size() == 3U);
    CHECK(atlas.levels[0].levelId == 75);
    CHECK(atlas.levels[0].firstCell == 0U);
    CHECK(atlas.levels[0].cellCount == 2U);
    CHECK(atlas.levels[1].levelId == 76);
    CHECK(atlas.levels[1].firstCell == 2U);
    CHECK(atlas.cells[0].frame == 101);
    CHECK(atlas.cells[0].tileX == 4'000);
    CHECK(!atlas.cells[0].wallTree);
    CHECK(!atlas.cells[0].raised);
    CHECK(atlas.cells[1].wallTree);
    CHECK(!atlas.cells[1].raised);
    CHECK(atlas.cells[2].wallTree);
    CHECK(atlas.cells[2].raised);

    CHECK(!ParseExternalAtlasGeometry(
        fixture,
        1'395'822'898U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::IdentityMismatch);
    CHECK(atlas.levels.empty());
    CHECK(atlas.cells.empty());

    auto corrupt = fixture;
    corrupt[4U] = 1U;
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::UnsupportedVersion);

    corrupt = fixture;
    corrupt[44U] ^= 1U;
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::DigestMismatch);

    corrupt = fixture;
    corrupt[14U] = 1U;
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::InvalidReservedBytes);

    corrupt = fixture;
    corrupt[56U] = 2U;
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::InvalidCell);

    corrupt = fixture;
    corrupt[57U] = 2U;
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::InvalidCell);

    corrupt = fixture;
    corrupt[58U] = 1U;
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::InvalidCell);

    corrupt = fixture;
    corrupt.pop_back();
    CHECK(!ParseExternalAtlasGeometry(
        corrupt,
        1'395'822'899U,
        2U,
        2U,
        atlas,
        &error));
    CHECK(error == ExternalAtlasGeometryParseError::InvalidLength);
}

void CheckAtlasProjectionContract() {
    using namespace RuffnecKk::MapSense;

    AtlasProjectionWitness witness;
    CHECK(BuildAtlasProjectionWitness(
        10'000,
        20'000,
        {500, 400},
        {564, 432},
        {436, 432},
        {500, 464},
        256,
        witness));
    CHECK(witness.valid);
    AtlasProjectedPoint projected{};
    CHECK(ProjectAtlasClientPoint(
        witness,
        10'256,
        20'256,
        projected));
    CHECK(std::abs(projected.x - 500.0) < 0.001);
    CHECK(std::abs(projected.y - 464.0) < 0.001);
    CHECK(ProjectAtlasClientPoint(
        witness,
        10'160,
        20'080,
        projected));
    CHECK(std::abs(projected.x - 520.0) < 0.001);
    CHECK(std::abs(projected.y - 430.0) < 0.001);

    AutomapSpriteProjectedQuad sprite{};
    CHECK(ProjectAutomapSpriteQuad(witness, 100, 80, sprite));
    // The test witness maps client X to (+0.25,+0.125) and client Y to
    // (-0.25,+0.125) native pixels per client unit.
    CHECK(std::abs(sprite.topRight.x - sprite.topLeft.x - 40.0) < 0.001);
    CHECK(std::abs(sprite.topRight.y - sprite.topLeft.y - 20.0) < 0.001);
    CHECK(std::abs(sprite.bottomLeft.x - sprite.topLeft.x + 80.0) < 0.001);
    CHECK(std::abs(sprite.bottomLeft.y - sprite.topLeft.y - 40.0) < 0.001);

    CHECK(!BuildAtlasProjectionWitness(
        10'000,
        20'000,
        {500, 400},
        {564, 432},
        {436, 432},
        {510, 464},
        256,
        witness));
    CHECK(!witness.valid);
    CHECK(!ProjectAtlasClientPoint(
        witness,
        10'000,
        20'000,
        projected));

    CHECK(!BuildAtlasProjectionWitness(
        10'000,
        20'000,
        {500, 400},
        {564, 432},
        {564, 432},
        {628, 464},
        256,
        witness));
}

void CheckNativeAutomapAtlasPolicy() {
    using namespace RuffnecKk::MapSense;

    ExternalPhysicalSeamAnchor seam{};
    const std::array<std::pair<std::int32_t, std::int32_t>, 0>
        noSeamAnchors{};
    CHECK(!SelectUniqueExternalPhysicalSeamAnchor(noSeamAnchors, seam));
    const std::array oneSeamAnchor{
        std::pair<std::int32_t, std::int32_t>{5'000, 4'268},
    };
    CHECK(SelectUniqueExternalPhysicalSeamAnchor(oneSeamAnchor, seam));
    CHECK(seam.subtileX == 5'000);
    CHECK(seam.subtileY == 4'268);
    const std::array twoSeamAnchors{
        std::pair<std::int32_t, std::int32_t>{5'000, 4'268},
        std::pair<std::int32_t, std::int32_t>{5'020, 4'380},
    };
    CHECK(!SelectUniqueExternalPhysicalSeamAnchor(twoSeamAnchors, seam));

    CHECK(ClassifyNativeAutomapActiveOwner(false, -1, 57)
        == NativeAutomapActiveOwnerState::Pending);
    CHECK(ClassifyNativeAutomapActiveOwner(true, 57, 57)
        == NativeAutomapActiveOwnerState::Ready);
    CHECK(ClassifyNativeAutomapActiveOwner(true, 58, 57)
        == NativeAutomapActiveOwnerState::Mismatch);
    CHECK(ClassifyNativeAutomapActiveOwner(true, -1, -1)
        == NativeAutomapActiveOwnerState::Mismatch);
    CHECK(NativeAutomapOwnerRequiresPulse(
        NativeAutomapActiveOwnerState::Pending));
    CHECK(!NativeAutomapOwnerRequiresPulse(
        NativeAutomapActiveOwnerState::Ready));
    CHECK(NativeAutomapOwnerRequiresPulse(
        NativeAutomapActiveOwnerState::Mismatch));

    CHECK(NativeAutomapOrdinaryCellTag == 0U);
    CHECK(NativeAutomapRestoredCellTag == 1U);
    CHECK(NativeAutomapSyntheticCellTag == NativeAutomapRestoredCellTag);
    CHECK(NativeAutomapCellIsSerialized(NativeAutomapOrdinaryCellTag));
    CHECK(!NativeAutomapCellIsSerialized(NativeAutomapRestoredCellTag));
    CHECK(NativeAutomapCellIsSerialized(0x0100U));
    CHECK(!NativeAutomapCellIsSerialized(0x0101U));
    CHECK(NativeAutomapLayerCompletionIsReusable(true, 16, 16));
    CHECK(!NativeAutomapLayerCompletionIsReusable(false, 16, 16));
    CHECK(!NativeAutomapLayerCompletionIsReusable(true, 16, 17));
    CHECK(!NativeAutomapLayerCompletionIsReusable(true, -1, -1));

    NativeAutomapCellKeyValue floor{};
    CHECK(BuildNativeAutomapCellKeyValue(
        101, 4'000, 3'900, false, floor));
    CHECK(floor.frame == 101);
    CHECK(floor.x == 800);
    CHECK(floor.y == 31'600);

    NativeAutomapCellKeyValue raised{};
    CHECK(BuildNativeAutomapCellKeyValue(
        101, 4'000, 3'900, true, raised));
    CHECK(raised.x == floor.x);
    CHECK(raised.y == floor.y + 24);

    CHECK(NativeAutomapSerializedBytesPerCell == 6U);
    CHECK(NativeAutomapMaximumEmittedTreeCells == 5'461U);
    CHECK(CanAppendNativeAutomapEmittedCell(5'460U));
    CHECK(!CanAppendNativeAutomapEmittedCell(5'461U));
    CHECK(!CanAppendNativeAutomapEmittedCell(10'517U));
    CHECK(5'461U * NativeAutomapSerializedBytesPerCell == 32'766U);
    CHECK(5'462U * NativeAutomapSerializedBytesPerCell == 32'772U);

    NativeAutomapCellKeyValue rejected{};
    CHECK(!BuildNativeAutomapCellKeyValue(
        32'768, 4'000, 3'900, false, rejected));
    CHECK(!BuildNativeAutomapCellKeyValue(
        101, -1, 3'900, false, rejected));
    CHECK(!BuildNativeAutomapCellKeyValue(
        101,
        (std::numeric_limits<std::int32_t>::max)(),
        0,
        false,
        rejected));

    NativeAutomapLayerCatalog catalog{
        .sessionGeneration = 42U,
        .seed = 1'395'822'899U,
        .difficulty = 2U,
        .act = 2U,
        .geometryDigest = UINT64_C(0x123456789ABCDEF0),
        .levels = {
            {.levelId = 75, .layer = 57},
            {.levelId = 76, .layer = 57},
            {.levelId = 83, .layer = 57},
            {.levelId = 84, .layer = 58},
        },
        .readyLayers = {57},
    };
    std::int32_t layer{};
    CHECK(FindNativeAutomapLayer(catalog, 75, layer));
    CHECK(layer == 57);
    CHECK(!FindNativeAutomapLayer(catalog, 74, layer));
    CHECK(NativeAutomapLevelsShareLayer(catalog, 75, 76));
    CHECK(NativeAutomapLevelsShareLayer(catalog, 75, 83));
    CHECK(!NativeAutomapLevelsShareLayer(catalog, 75, 84));
    CHECK(NativeAutomapLevelsShareLayer(catalog, 84, 84));
    CHECK(NativeAutomapLevelsShareReadyLayer(catalog, 75, 76));
    CHECK(NativeAutomapLevelsShareReadyLayer(catalog, 75, 83));
    CHECK(!NativeAutomapLevelsShareReadyLayer(catalog, 75, 84));
    CHECK(!NativeAutomapLevelsShareReadyLayer(catalog, 84, 84));
    catalog.readyLayers.push_back(58);
    CHECK(NativeAutomapLevelsShareReadyLayer(catalog, 84, 84));
}

void CheckAutomapSpritePackageContract(const char* path) {
    using namespace RuffnecKk::MapSense;

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(input.is_open());
    if (!input.is_open()) return;
    const auto length = input.tellg();
    CHECK(length == static_cast<std::streamoff>(
        AutomapSpritePackageBytes));
    if (length <= 0) return;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    CHECK(static_cast<bool>(input));

    AutomapSpriteRgbaAtlas atlas;
    AutomapSpritePackageParseError error{};
    CHECK(ParseAutomapSpritePackage(bytes, atlas, &error));
    CHECK(error == AutomapSpritePackageParseError::None);
    CHECK(atlas.width == AutomapSpriteIndexAtlasWidth);
    CHECK(atlas.height == AutomapSpriteRgbaAtlasHeight);
    CHECK(atlas.pixels.size()
        == static_cast<std::size_t>(atlas.width) * atlas.height * 4U);
    CHECK(std::any_of(
        atlas.pixels.begin() + 3,
        atlas.pixels.end(),
        [position = std::size_t{3}](std::uint8_t value) mutable {
            const auto alpha = (position++ % 4U) == 3U;
            return alpha && value != 0U;
        }));

    bytes.back() ^= 1U;
    CHECK(!ParseAutomapSpritePackage(bytes, atlas, &error));
    CHECK(error == AutomapSpritePackageParseError::DigestMismatch);
    CHECK(atlas.pixels.empty());
}

void CheckExternalAtlasCacheContract() {
    using namespace RuffnecKk::MapSense;

    ScopedCatalogTestDirectory directory("atlas-cache");
    const ExternalAtlasCacheKey key{
        .seed = 1'395'822'899U,
        .difficulty = 2U,
        .act = 2U,
    };
    std::filesystem::path path;
    CHECK(BuildExternalAtlasCachePath(directory.Path(), key, path));
    CHECK(path.filename() == L"act-2-r4.msa");
    CHECK(path.parent_path().filename() == L"difficulty-2");

    auto moddedKey = key;
    moddedKey.dataFingerprint = UINT64_C(0x0123456789ABCDEF);
    std::filesystem::path moddedPath;
    CHECK(BuildExternalAtlasCachePath(
        directory.Path(), moddedKey, moddedPath));
    CHECK(moddedPath != path);
    CHECK(moddedPath.parent_path().parent_path().parent_path().filename()
        == L"data-0123456789ABCDEF");

    ExternalAtlasGeometry atlas;
    CHECK(LoadExternalAtlasGeometryCache(directory.Path(), key, atlas)
        == ExternalAtlasCacheResult::Miss);
    const auto fixture = BuildExternalAtlasGeometryFixture();
    CHECK(StoreExternalAtlasGeometryCache(directory.Path(), key, fixture));
    CHECK(LoadExternalAtlasGeometryCache(directory.Path(), key, atlas)
        == ExternalAtlasCacheResult::Hit);
    CHECK(atlas.levels.size() == 2U);
    CHECK(atlas.cells.size() == 3U);

    auto corrupt = fixture;
    corrupt[44U] ^= 1U;
    CHECK(!StoreExternalAtlasGeometryCache(
        directory.Path(), key, corrupt));
    CHECK(LoadExternalAtlasGeometryCache(directory.Path(), key, atlas)
        == ExternalAtlasCacheResult::Hit);

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(corrupt.data()),
            static_cast<std::streamsize>(corrupt.size()));
    }
    ExternalAtlasGeometryParseError parseError{};
    CHECK(LoadExternalAtlasGeometryCache(
            directory.Path(), key, atlas, &parseError)
        == ExternalAtlasCacheResult::Invalid);
    CHECK(parseError == ExternalAtlasGeometryParseError::DigestMismatch);
    CHECK(atlas.levels.empty());
    CHECK(atlas.cells.empty());

    CHECK(!HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty));
    CHECK(StoreExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty, true));
    CHECK(HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty));
    CHECK(!HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed + 1U, key.difficulty));
    CHECK(!HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, 1U));
    const auto intentPath = path.parent_path() / L"reveal-map.intent";
    {
        std::fstream intent(
            intentPath,
            std::ios::binary | std::ios::in | std::ios::out);
        CHECK(static_cast<bool>(intent));
        intent.seekp(12, std::ios::beg);
        intent.put(static_cast<char>(1));
    }
    CHECK(!HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty));
    CHECK(StoreExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty, true));
    {
        std::ofstream intent(intentPath, std::ios::binary | std::ios::app);
        CHECK(static_cast<bool>(intent));
        intent.put(static_cast<char>(0));
    }
    CHECK(!HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty));
    CHECK(StoreExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty, true));
    CHECK(StoreExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty, false));
    CHECK(!HasExternalAtlasRevealMapIntent(
        directory.Path(), key.seed, key.difficulty));
}

void CheckStaticPoiRoomSelectionContract() {
    using namespace RuffnecKk::MapSense;

    CHECK(SelectStaticPoiRoomAction(0U, false, false, false)
        == StaticPoiRoomAction::Ignore);
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWarpMask,
            false,
            false,
            false)
        == StaticPoiRoomAction::Materialize);
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWarpMask,
            true,
            false,
            false)
        == StaticPoiRoomAction::Reuse);
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWaypointMask,
            false,
            false,
            false)
        == StaticPoiRoomAction::Materialize);
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWaypointMask
                | StaticPoiRoomPresetUnitsAddedMask,
            false,
            true,
            false)
        == StaticPoiRoomAction::Reuse);
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWarpMask | StaticPoiRoomWaypointMask,
            true,
            false,
            false)
        == StaticPoiRoomAction::Materialize);

    // Existing native room state is never taken over or cleaned by the
    // distant-name path, even when an expected descriptor is not ready yet.
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWarpMask,
            false,
            false,
            true)
        == StaticPoiRoomAction::Wait);
    CHECK(SelectStaticPoiRoomAction(
            StaticPoiRoomWaypointMask | StaticPoiRoomHasRoomMask,
            false,
            false,
            false)
        == StaticPoiRoomAction::Wait);
}

void CheckNativeUiPanelPolicy() {
    using namespace RuffnecKk::MapSense;

    std::array<std::uint8_t, NativeUiStateCount> states{};
    CHECK(NativeUiStateMask(states) == 0U);
    CHECK(NativeUiBlockingPanelMask(states) == 0U);

    // World/HUD states coexist with MapSense.
    for (const auto state : std::array<std::size_t, 8>{
            0U, 10U, 12U, 18U, 20U, 26U, 28U, 29U}) {
        states.fill(0U);
        states[state] = 1U;
        CHECK(!IsNativeUiPanelState(state));
        CHECK(NativeUiBlockingPanelMask(states) == 0U);
    }

    // Known panels and every unclassified state fail closed. Quest is native
    // interface state 0x0E; 0x0F remains unknown and therefore full-screen.
    for (const auto state : std::array<std::size_t, 14>{
            1U, 2U, 3U, 4U, 5U, 8U, 9U, 11U,
            14U, 15U, 19U, 21U, 24U, 25U}) {
        states.fill(0U);
        states[state] = 1U;
        CHECK(IsNativeUiPanelState(state));
        CHECK(NativeUiBlockingPanelMask(states)
            == (std::uint32_t{1U} << state));
    }

    states.fill(0U);
    states[31U] = 1U;
    CHECK(IsNativeUiPanelState(31U));
    CHECK(NativeUiBlockingPanelMask(states) == 0x80000000U);

    // Native panels never hide the launcher or all map additions. Map pixels
    // are constrained later by the current UI state and side-panel geometry.
    CHECK(ShouldDrawMapSenseSettingsMenu(true, false));
    CHECK(ShouldDrawMapSenseSettingsMenu(true, true));
    CHECK(!ShouldDrawMapSenseSettingsMenu(false, false));
    CHECK(ShouldDrawMapSenseOwnedMapOverlay(true, false));
    CHECK(ShouldDrawMapSenseOwnedMapOverlay(true, true));
    CHECK(!ShouldDrawMapSenseOwnedMapOverlay(false, false));

    constexpr auto gameplayAutomapMask = NativeUiGameStateMask
        | NativeUiAutomapStateMask;
    CHECK(IsNativeGameplayAutomapFrame(gameplayAutomapMask));
    CHECK(IsNativeGameplayAutomapFrame(
        gameplayAutomapMask | (std::uint32_t{1U} << 20U)));
    CHECK(!IsNativeGameplayAutomapFrame(NativeUiGameStateMask));
    CHECK(!IsNativeGameplayAutomapFrame(NativeUiAutomapStateMask));
    CHECK(!IsNativeGameplayAutomapFrame(0U));

    CHECK(Detail::IsLocalPlayerAliveMode(1U));
    CHECK(Detail::IsLocalPlayerAliveMode(16U));
    CHECK(Detail::IsLocalPlayerAliveMode(18U));
    CHECK(!Detail::IsLocalPlayerAliveMode(Detail::LocalPlayerModeDeath));
    CHECK(!Detail::IsLocalPlayerAliveMode(Detail::LocalPlayerModeDead));

    CHECK(ShouldDrawMapSenseOwnedVisualFrame(
        true,
        gameplayAutomapMask,
        true));
    CHECK(!ShouldDrawMapSenseOwnedVisualFrame(
        false,
        gameplayAutomapMask,
        true));
    CHECK(!ShouldDrawMapSenseOwnedVisualFrame(
        true,
        NativeUiGameStateMask,
        true));
    CHECK(!ShouldDrawMapSenseOwnedVisualFrame(
        true,
        NativeUiAutomapStateMask,
        true));
    CHECK(!ShouldDrawMapSenseOwnedVisualFrame(
        true,
        gameplayAutomapMask,
        false));

    CHECK(ClassifyNativeUiMapPanelCoverage(
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 10U))
        == NativeUiMapPanelCoverage::None);
    CHECK(ClassifyNativeUiMapPanelCoverage(
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 1U)
            | (std::uint32_t{1U} << 10U))
        == NativeUiMapPanelCoverage::Right);
    CHECK(ClassifyNativeUiMapPanelCoverage(
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 2U)
            | (std::uint32_t{1U} << 10U))
        == NativeUiMapPanelCoverage::Left);
    CHECK(ClassifyNativeUiMapPanelCoverage(
        (std::uint32_t{1U} << 0U)
            | (std::uint32_t{1U} << NativeUiQuestPanelState)
            | (std::uint32_t{1U} << 10U))
        == NativeUiMapPanelCoverage::Left);
    CHECK(ClassifyNativeUiMapPanelCoverage(
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 15U)
            | (std::uint32_t{1U} << 10U))
        == NativeUiMapPanelCoverage::Full);
    CHECK(ClassifyNativeUiMapPanelCoverage(
        (std::uint32_t{1U} << 1U) | (std::uint32_t{1U} << 2U))
        == NativeUiMapPanelCoverage::Full);
    CHECK(ClassifyNativeUiMapPanelCoverage(
        std::uint32_t{1U} << 9U)
        == NativeUiMapPanelCoverage::Full);

    constexpr auto questWorldMask =
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 10U)
        | (std::uint32_t{1U} << 20U) | (std::uint32_t{1U} << 29U);
    CHECK(ShouldRetainNativeAutomapProjectionForQuest(
        questWorldMask,
        true,
        true,
        1'000U,
        1'050U));
    CHECK(!ShouldRetainNativeAutomapProjectionForQuest(
        questWorldMask,
        false,
        true,
        1'000U,
        1'050U));
    CHECK(!ShouldRetainNativeAutomapProjectionForQuest(
        questWorldMask,
        true,
        false,
        1'000U,
        1'050U));
    CHECK(!ShouldRetainNativeAutomapProjectionForQuest(
        questWorldMask & ~(std::uint32_t{1U} << 10U),
        true,
        true,
        1'000U,
        1'050U));
    CHECK(!ShouldRetainNativeAutomapProjectionForQuest(
        questWorldMask,
        true,
        true,
        1'000U,
        1'501U));
    CHECK(!ShouldRetainNativeAutomapProjectionForQuest(
        questWorldMask | (std::uint32_t{1U} << 9U),
        true,
        true,
        1'000U,
        1'050U));

    NativeUiMapHorizontalClip panelClip{};
    CHECK(TryResolveNativeUiMapHorizontalClip(
        2560,
        1440,
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 1U),
        panelClip));
    CHECK(panelClip.left == 0);
    CHECK(panelClip.right == 1210);
    CHECK(panelClip.coverage == NativeUiMapPanelCoverage::Right);
    CHECK(TryResolveNativeUiMapHorizontalClip(
        2560,
        1440,
        (std::uint32_t{1U} << 0U)
            | (std::uint32_t{1U} << NativeUiQuestPanelState),
        panelClip));
    CHECK(panelClip.left == 1339);
    CHECK(panelClip.right == 2560);
    CHECK(panelClip.coverage == NativeUiMapPanelCoverage::Left);
    CHECK(TryResolveNativeUiMapHorizontalClip(
        3840,
        2160,
        (std::uint32_t{1U} << 0U) | (std::uint32_t{1U} << 1U),
        panelClip));
    CHECK(panelClip.right == 1819);
    CHECK(!TryResolveNativeUiMapHorizontalClip(
        2560,
        1440,
        (std::uint32_t{1U} << 9U),
        panelClip));

    NativeAutomapClipBounds clip{};
    CHECK(TryResolveNativeAutomapClipBounds(
        NativeAutomapViewportSnapshot{
            .nativeWidth = 3840,
            .nativeHeight = 2160,
            .clipLeft = 0,
            .clipTop = 0,
            .clipWidth = 1920,
            .clipHeight = 2160,
        },
        clip));
    CHECK(clip.left == 0);
    CHECK(clip.top == 0);
    CHECK(clip.right == 1920);
    CHECK(clip.bottom == 2160);

    CHECK(TryResolveNativeAutomapClipBounds(
        NativeAutomapViewportSnapshot{
            .nativeWidth = 3840,
            .nativeHeight = 2160,
            .clipLeft = 1920,
            .clipTop = 0,
            .clipWidth = 1920,
            .clipHeight = 2160,
        },
        clip));
    CHECK(clip.left == 1920);
    CHECK(clip.right == 3840);

    CHECK(TryResolveNativeAutomapClipBounds(
        NativeAutomapViewportSnapshot{
            .nativeWidth = 3840,
            .nativeHeight = 2160,
            .clipLeft = -20,
            .clipTop = -10,
            .clipWidth = 3880,
            .clipHeight = 2180,
        },
        clip));
    CHECK(clip.left == 0);
    CHECK(clip.top == 0);
    CHECK(clip.right == 3840);
    CHECK(clip.bottom == 2160);

    CHECK(!TryResolveNativeAutomapClipBounds(
        NativeAutomapViewportSnapshot{
            .nativeWidth = 3840,
            .nativeHeight = 2160,
            .clipLeft = 4000,
            .clipTop = 0,
            .clipWidth = 100,
            .clipHeight = 100,
        },
        clip));
}

void CheckNavigationEngineContract() {
    using namespace RuffnecKk::MapSense;

    CHECK(ShouldRequestNavigationRefresh(
        NavigationAutomapObservationResult::LevelChanged,
        false));
    CHECK(!ShouldRequestNavigationRefresh(
        NavigationAutomapObservationResult::LevelChanged,
        true));
    CHECK(!ShouldRequestNavigationRefresh(
        NavigationAutomapObservationResult::Projected,
        false));

    InitializeNavigationEngine();
    ResetNavigationSession(41U);
    CHECK(!BindNavigationLevelForPublish(40U, 3));
    CHECK(BindNavigationLevelForPublish(41U, 3));
    CHECK(!BindNavigationLevelForPublish(41U, 4));

    const std::array destinations{
        NavigationSubtileDestination{
            .destinationId = 1001U,
            .subtileX = 20,
            .subtileY = 10,
            .kind = NavigationLineKind::Waypoint,
            .exactClientX = 321,
            .exactClientY = 123,
            .useExactClientCoordinates = true,
        },
        NavigationSubtileDestination{
            .destinationId = 1002U,
            .subtileX = 60,
            .subtileY = 20,
            .kind = NavigationLineKind::Progression,
        },
    };
    CHECK(!PublishNavigationDestinations(
        40U,
        3,
        destinations.data(),
        destinations.size()));
    CHECK(!PublishNavigationDestinations(
        41U,
        4,
        destinations.data(),
        destinations.size()));
    CHECK(PublishNavigationDestinations(
        41U,
        3,
        destinations.data(),
        destinations.size()));

    std::uint8_t borrowedContext{};
    auto pass = NavigationAutomapPass{
        .currentLevelId = 3,
        .playerClientX = 100,
        .playerClientY = 100,
        .nativeWidth = 800,
        .nativeHeight = 600,
        .clipLeft = 0,
        .clipTop = 0,
        .clipWidth = 800,
        .clipHeight = 600,
        .projectClient = ProjectNavigationClientIdentity,
        .borrowedAutomapContext = &borrowedContext,
    };
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);

    std::vector<NavigationLineSnapshot> lines;
    CHECK(AcquireNavigationLineSnapshots(lines) == 2U);
    CHECK(lines[0].destinationId == 1001U);
    CHECK(lines[0].sessionGeneration == 41U);
    CHECK(lines[0].levelId == 3);
    CHECK(lines[0].startX == 100);
    CHECK(lines[0].startY == 100);
    CHECK(lines[0].endX == 321);
    CHECK(lines[0].endY == 123);
    CHECK(lines[0].kind == NavigationLineKind::Waypoint);
    CHECK(lines[1].destinationId == 1002U);
    CHECK(lines[1].endX == 599);
    CHECK(lines[1].endY == 599);
    CHECK(lines[1].kind == NavigationLineKind::Progression);
    CHECK(WantsNavigationLineFrame());

    std::this_thread::sleep_for(std::chrono::milliseconds(275));
    CHECK(!WantsNavigationLineFrame());
    CHECK(AcquireNavigationLineSnapshots(lines) == 0U);
    CHECK(WantsNavigationLineFrame(true));
    CHECK(AcquireNavigationLineSnapshots(lines, true) == 2U);
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);

    const auto projectedStatus = GetNavigationEngineStatus();
    InvalidateNavigationProjection();
    CHECK(AcquireNavigationLineSnapshots(lines) == 0U);
    CHECK(!WantsNavigationLineFrame());
    const auto invalidatedStatus = GetNavigationEngineStatus();
    CHECK(invalidatedStatus.sessionGeneration
        == projectedStatus.sessionGeneration);
    CHECK(invalidatedStatus.destinationRevision
        == projectedStatus.destinationRevision);
    CHECK(invalidatedStatus.levelId == projectedStatus.levelId);
    CHECK(invalidatedStatus.destinationCount
        == projectedStatus.destinationCount);
    CHECK(invalidatedStatus.projectedLineCount == 0U);
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);
    CHECK(AcquireNavigationLineSnapshots(lines) == 2U);

    const std::array captiveCages{
        NavigationSubtileDestination{
            .destinationId = 3001U,
            .subtileX = 10,
            .subtileY = 3,
            .kind = NavigationLineKind::Quest,
            .exactClientX = 112,
            .exactClientY = 104,
            .useExactClientCoordinates = true,
            .selection = NavigationDestinationSelection::NearestToPlayer,
        },
        NavigationSubtileDestination{
            .destinationId = 3002U,
            .subtileX = 20,
            .subtileY = 3,
            .kind = NavigationLineKind::Quest,
            .exactClientX = 272,
            .exactClientY = 184,
            .useExactClientCoordinates = true,
            .selection = NavigationDestinationSelection::NearestToPlayer,
        },
        NavigationSubtileDestination{
            .destinationId = 3003U,
            .subtileX = 30,
            .subtileY = 3,
            .kind = NavigationLineKind::Quest,
            .exactClientX = 432,
            .exactClientY = 264,
            .useExactClientCoordinates = true,
            .selection = NavigationDestinationSelection::NearestToPlayer,
        },
    };
    CHECK(PublishNavigationDestinations(
        41U,
        3,
        captiveCages.data(),
        captiveCages.size()));
    pass.playerClientX = 100;
    pass.playerClientY = 100;
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);
    CHECK(AcquireNavigationLineSnapshots(lines) == 1U);
    CHECK(lines[0].destinationId == 3001U);
    CHECK(lines[0].kind == NavigationLineKind::Quest);
    CHECK(lines[0].endX == 112);
    CHECK(lines[0].endY == 104);

    pass.playerClientX = 400;
    pass.playerClientY = 240;
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);
    CHECK(AcquireNavigationLineSnapshots(lines) == 1U);
    CHECK(lines[0].destinationId == 3003U);
    CHECK(lines[0].endX == 432);
    CHECK(lines[0].endY == 264);

    CHECK(PublishNavigationDestinations(
        41U,
        3,
        destinations.data(),
        destinations.size()));
    pass.playerClientX = 100;
    pass.playerClientY = 100;
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);
    CHECK(AcquireNavigationLineSnapshots(lines) == 2U);

    pass.currentLevelId = UnknownNavigationLevelId;
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Ignored);
    CHECK(AcquireNavigationLineSnapshots(lines) == 0U);
    CHECK(!WantsNavigationLineFrame());
    pass.currentLevelId = 3;
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Projected);
    CHECK(AcquireNavigationLineSnapshots(lines) == 2U);

    pass.currentLevelId = 4;
    pass.inTown = true;
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::LevelChanged);
    CHECK(AcquireNavigationLineSnapshots(lines) == 0U);
    CHECK(!WantsNavigationLineFrame());
    const auto changedStatus = GetNavigationEngineStatus();
    CHECK(changedStatus.levelId == 4);
    CHECK(changedStatus.destinationCount == 0U);
    CHECK(changedStatus.observedLevelChanges == 1U);
    CHECK(!PublishNavigationDestinations(
        41U,
        3,
        destinations.data(),
        destinations.size()));
    CHECK(BindNavigationLevelForPublish(41U, 4));
    CHECK(PublishNavigationDestinations(
        41U,
        4,
        destinations.data(),
        destinations.size()));
    CHECK(GetNavigationEngineStatus().destinationCount == 2U);
    CHECK(ObserveNavigationAutomapPass(pass)
        == NavigationAutomapObservationResult::Ignored);
    CHECK(AcquireNavigationLineSnapshots(lines) == 0U);
    CHECK(!WantsNavigationLineFrame());
    const auto townStatus = GetNavigationEngineStatus();
    CHECK(townStatus.levelId == 4);
    CHECK(townStatus.destinationCount == 0U);
    CHECK(townStatus.projectedLineCount == 0U);
    ShutdownNavigationEngine();
}

void CheckNavigationPolicyContract() {
    using namespace RuffnecKk::MapSense;

    CHECK(MainProgressionTargetFor(3).value_or(-1) == 4);
    CHECK(MainProgressionTargetFor(6).value_or(-1) == 7);
    CHECK(MainProgressionTargetFor(7).value_or(-1) == 26);
    CHECK(MainProgressionTargetFor(28).value_or(-1) == 29);
    CHECK(MainProgressionTargetFor(29).value_or(-1) == 30);
    CHECK(MainProgressionTargetFor(40).value_or(-1) == 41);
    CHECK(MainProgressionTargetFor(76).value_or(-1) == 78);
    CHECK(MainProgressionTargetFor(107).value_or(-1) == 108);
    CHECK(MainProgressionTargetFor(130).value_or(-1) == 131);
    CHECK(!MainProgressionTargetFor(37).has_value());
    CHECK(MainProgressionTargetFor(54).value_or(-1) == 74);
    CHECK(MainProgressionTargetFor(66).value_or(-1) == 73);
    CHECK(MainProgressionTargetFor(74).value_or(-1) == 46);
    CHECK(MainProgressionTargetFor(102).value_or(-1) == 103);
    CHECK(MainProgressionTargetFor(108).value_or(-1) == 109);
    CHECK(MainProgressionTargetFor(131).value_or(-1) == 132);

    std::array<std::int32_t, 4U> preparationTargets{};
    const std::array tamoeCustomTargets{12, 12, 7, -1};
    const auto tamoePreparationCount = BuildNavigationPreparationTargets(
        7,
        tamoeCustomTargets,
        preparationTargets);
    CHECK(tamoePreparationCount == 2U);
    CHECK(preparationTargets[0] == 26);
    CHECK(preparationTargets[1] == 12);
    CHECK(BuildNavigationPreparationTargets(
        28,
        std::span<const std::int32_t>{},
        preparationTargets) == 1U);
    CHECK(preparationTargets[0] == 29);
    CHECK(BuildNavigationPreparationTargets(
        29,
        std::span<const std::int32_t>{},
        preparationTargets) == 1U);
    CHECK(preparationTargets[0] == 30);
    CHECK(BuildNavigationPreparationTargets(
        74,
        std::span<const std::int32_t>{},
        preparationTargets) == 0U);

    struct ProgressionRegression final {
        std::int32_t from{};
        std::array<std::int32_t, MaximumMainProgressionTargets> targets{};
        std::size_t targetCount{};
        bool inTown{};
        std::int32_t dynamicObjectClassId{-1};
    };
    constexpr std::array progressionMatrix{
        ProgressionRegression{1, {2, -1}, 1U, true},
        ProgressionRegression{2, {3, -1}, 1U},
        ProgressionRegression{3, {4, -1}, 1U},
        ProgressionRegression{4, {10, -1}, 1U},
        ProgressionRegression{5, {6, -1}, 1U},
        ProgressionRegression{6, {7, -1}, 1U},
        ProgressionRegression{7, {26, -1}, 1U},
        ProgressionRegression{9, {13, -1}, 1U},
        ProgressionRegression{10, {5, -1}, 1U},
        ProgressionRegression{11, {15, -1}, 1U},
        ProgressionRegression{12, {16, -1}, 1U},
        ProgressionRegression{26, {27, -1}, 1U},
        ProgressionRegression{27, {28, -1}, 1U},
        ProgressionRegression{28, {29, -1}, 1U},
        ProgressionRegression{29, {30, -1}, 1U},
        ProgressionRegression{30, {31, -1}, 1U},
        ProgressionRegression{31, {32, -1}, 1U},
        ProgressionRegression{32, {33, -1}, 1U},
        ProgressionRegression{33, {34, -1}, 1U},
        ProgressionRegression{34, {35, -1}, 1U},
        ProgressionRegression{35, {36, -1}, 1U},
        ProgressionRegression{36, {37, -1}, 1U},

        ProgressionRegression{40, {41, -1}, 1U, true},
        ProgressionRegression{41, {42, -1}, 1U},
        ProgressionRegression{42, {43, -1}, 1U},
        ProgressionRegression{43, {44, -1}, 1U},
        ProgressionRegression{44, {45, -1}, 1U},
        ProgressionRegression{45, {58, -1}, 1U},
        ProgressionRegression{50, {51, -1}, 1U},
        ProgressionRegression{51, {52, -1}, 1U},
        ProgressionRegression{52, {53, -1}, 1U},
        ProgressionRegression{53, {54, -1}, 1U},
        ProgressionRegression{54, {74, -1}, 1U, false, 298},
        ProgressionRegression{55, {59, -1}, 1U},
        ProgressionRegression{58, {61, -1}, 1U},
        ProgressionRegression{66, {73, -1}, 1U, false, 100},
        ProgressionRegression{67, {73, -1}, 1U, false, 100},
        ProgressionRegression{68, {73, -1}, 1U, false, 100},
        ProgressionRegression{69, {73, -1}, 1U, false, 100},
        ProgressionRegression{70, {73, -1}, 1U, false, 100},
        ProgressionRegression{71, {73, -1}, 1U, false, 100},
        ProgressionRegression{72, {73, -1}, 1U, false, 100},
        ProgressionRegression{74, {46, -1}, 1U, false, 60},

        ProgressionRegression{75, {76, -1}, 1U, true},
        ProgressionRegression{76, {78, 77}, 2U},
        ProgressionRegression{77, {78, 76}, 2U},
        ProgressionRegression{78, {79, -1}, 1U},
        ProgressionRegression{79, {80, -1}, 1U},
        ProgressionRegression{80, {81, -1}, 1U},
        ProgressionRegression{81, {82, -1}, 1U},
        ProgressionRegression{82, {83, -1}, 1U},
        ProgressionRegression{83, {100, -1}, 1U},
        ProgressionRegression{86, {87, -1}, 1U},
        ProgressionRegression{87, {90, -1}, 1U},
        ProgressionRegression{100, {101, -1}, 1U},
        ProgressionRegression{101, {102, -1}, 1U},
        ProgressionRegression{102, {103, -1}, 1U, false, 342},

        ProgressionRegression{103, {104, -1}, 1U, true},
        ProgressionRegression{104, {105, -1}, 1U},
        ProgressionRegression{105, {106, -1}, 1U},
        ProgressionRegression{106, {107, -1}, 1U},
        ProgressionRegression{107, {108, -1}, 1U},
        ProgressionRegression{108, {109, -1}, 1U, false, 566},

        ProgressionRegression{109, {110, -1}, 1U, true},
        ProgressionRegression{110, {111, -1}, 1U},
        ProgressionRegression{111, {112, -1}, 1U},
        ProgressionRegression{112, {113, -1}, 1U},
        ProgressionRegression{113, {115, -1}, 1U},
        ProgressionRegression{115, {117, -1}, 1U},
        ProgressionRegression{117, {118, -1}, 1U},
        ProgressionRegression{118, {120, -1}, 1U},
        ProgressionRegression{120, {128, -1}, 1U},
        ProgressionRegression{128, {129, -1}, 1U},
        ProgressionRegression{129, {130, -1}, 1U},
        ProgressionRegression{130, {131, -1}, 1U},
        ProgressionRegression{131, {132, -1}, 1U, false, 563},
    };
    std::array<NavigationSubtileDestination, 4U> interiorDestinations{};
    for (const auto& regression : progressionMatrix) {
        CHECK(MainProgressionTargetFor(regression.from).value_or(-1)
            == regression.targets[0]);
        std::array<std::int32_t, MaximumMainProgressionTargets>
            actualTargets{};
        CHECK(MainProgressionTargetsFor(regression.from, actualTargets)
            == regression.targetCount);
        for (std::size_t index = 0U;
                index < regression.targetCount;
                ++index) {
            CHECK(actualTargets[index] == regression.targets[index]);
        }
        std::array<std::int32_t, MaximumStaticQuestRouteTargets>
            questRouteTargets{};
        const auto questRouteCount = StaticQuestRouteTargetsFor(
            regression.from,
            questRouteTargets);
        const auto preparationCount = BuildNavigationPreparationTargets(
            regression.from,
            std::span<const std::int32_t>{},
            preparationTargets);
        const auto isDynamic = regression.dynamicObjectClassId >= 0;
        const auto preparedProgressionCount = isDynamic
            ? 0U : regression.targetCount;
        CHECK(preparationCount
            == preparedProgressionCount + questRouteCount);
        CHECK(HasDynamicMainProgressionTargetFor(regression.from)
            == isDynamic);
        if (isDynamic) {
            CHECK(DynamicMainProgressionTargetFor(
                regression.from,
                regression.dynamicObjectClassId).value_or(-1)
                == regression.targets[0]);
            CHECK(!DynamicMainProgressionTargetFor(
                regression.from,
                regression.dynamicObjectClassId + 1).has_value());
        }
        std::array<
            NavigationExitCandidate,
            MaximumMainProgressionTargets + MaximumStaticQuestRouteTargets>
            exactExits{};
        for (std::size_t index = 0U;
                index < regression.targetCount;
                ++index) {
            if (!isDynamic) {
                CHECK(preparationTargets[index]
                    == regression.targets[index]);
            }
            exactExits[index] = NavigationExitCandidate{
                .destinationId = static_cast<std::uint64_t>(
                    regression.targets[index]),
                .targetLevelId = regression.targets[index],
                .subtileX = 1'000 + regression.from
                    + static_cast<std::int32_t>(index),
                .subtileY = 2'000 + regression.targets[index],
                .exactClientX = isDynamic ? -3'200 : 0,
                .exactClientY = isDynamic ? 4'800 : 0,
                .useExactClientCoordinates = isDynamic,
            };
        }
        for (std::size_t index = 0U;
                index < questRouteCount;
                ++index) {
            CHECK(preparationTargets[preparedProgressionCount + index]
                == questRouteTargets[index]);
            exactExits[regression.targetCount + index] =
                NavigationExitCandidate{
                    .destinationId = static_cast<std::uint64_t>(
                        questRouteTargets[index]),
                    .targetLevelId = questRouteTargets[index],
                    .subtileX = 3'000 + regression.from
                        + static_cast<std::int32_t>(index),
                    .subtileY = 4'000 + questRouteTargets[index],
                };
        }
        const auto exactExitSpan = std::span(
            exactExits.data(),
            regression.targetCount + questRouteCount);
        CHECK(SelectMainProgressionTargetFor(
            regression.from,
            exactExitSpan).value_or(-1) == regression.targets[0]);
        CHECK(EvaluateNavigationResolutionCompleteness(
            regression.from,
            exactExitSpan) == NavigationResolutionCompleteness::Complete);
        CHECK(EvaluateNavigationResolutionCompleteness(
            regression.from,
            std::span<const NavigationExitCandidate>{})
            == (isDynamic && questRouteCount == 0U
                ? NavigationResolutionCompleteness::Complete
                : NavigationResolutionCompleteness::PartialRetryable));
        const auto progressionDestinationCount = BuildNavigationDestinations(
            NavigationPolicyInput{
                .currentLevelId = regression.from,
                .inTown = regression.inTown,
                .exits = exactExitSpan,
            },
            interiorDestinations);
        if (regression.inTown) {
            CHECK(progressionDestinationCount == 0U);
            continue;
        }
        CHECK(progressionDestinationCount == 1U + questRouteCount);
        CHECK(interiorDestinations[0].kind
            == NavigationLineKind::Progression);
        CHECK(interiorDestinations[0].subtileX
            == 1'000 + regression.from);
        CHECK(interiorDestinations[0].subtileY
            == 2'000 + regression.targets[0]);
        CHECK(interiorDestinations[0].useExactClientCoordinates
            == isDynamic);
        if (isDynamic) {
            CHECK(interiorDestinations[0].exactClientX == -3'200);
            CHECK(interiorDestinations[0].exactClientY == 4'800);
        }
        for (std::size_t index = 0U;
                index < questRouteCount;
                ++index) {
            CHECK(interiorDestinations[1U + index].kind
                == NavigationLineKind::Quest);
            CHECK(interiorDestinations[1U + index].destinationId
                == static_cast<std::uint64_t>(questRouteTargets[index]));
        }
    }

    // Great Marsh can be generated either with a direct Flayer Jungle seam or
    // as a dead-end off Spider Forest. Always prefer the direct exit, but keep
    // the exact return seam as the next green hop when the bypass is required.
    const std::array greatMarshDeadEndExits{
        NavigationExitCandidate{
            .destinationId = 77'076U,
            .targetLevelId = 76,
            .subtileX = 7'600,
            .subtileY = 7'601,
        },
    };
    CHECK(SelectMainProgressionTargetFor(
        77,
        greatMarshDeadEndExits).value_or(-1) == 76);
    CHECK(EvaluateNavigationResolutionCompleteness(
        77,
        greatMarshDeadEndExits) == NavigationResolutionCompleteness::Complete);
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 77,
            .exits = greatMarshDeadEndExits,
        },
        interiorDestinations) == 1U);
    CHECK(interiorDestinations[0].kind == NavigationLineKind::Progression);
    CHECK(interiorDestinations[0].subtileX == 7'600);
    CHECK(interiorDestinations[0].subtileY == 7'601);

    const std::array greatMarshDirectExits{
        greatMarshDeadEndExits[0],
        NavigationExitCandidate{
            .destinationId = 77'078U,
            .targetLevelId = 78,
            .subtileX = 7'800,
            .subtileY = 7'801,
        },
    };
    CHECK(SelectMainProgressionTargetFor(
        77,
        greatMarshDirectExits).value_or(-1) == 78);
    CHECK(EvaluateNavigationResolutionCompleteness(
        77,
        std::span<const NavigationExitCandidate>{})
        == NavigationResolutionCompleteness::PartialRetryable);

    struct QuestRouteRegression final {
        std::int32_t from{};
        std::array<std::int32_t, MaximumStaticQuestRouteTargets> targets{};
        std::size_t targetCount{};
    };
    constexpr std::array questRouteMatrix{
        QuestRouteRegression{2, {8, -1}, 1U},
        QuestRouteRegression{3, {17, -1}, 1U},
        QuestRouteRegression{6, {20, -1}, 1U},
        QuestRouteRegression{20, {21, -1}, 1U},
        QuestRouteRegression{21, {22, -1}, 1U},
        QuestRouteRegression{22, {23, -1}, 1U},
        QuestRouteRegression{23, {24, -1}, 1U},
        QuestRouteRegression{24, {25, -1}, 1U},

        QuestRouteRegression{47, {48, -1}, 1U},
        QuestRouteRegression{48, {49, -1}, 1U},
        QuestRouteRegression{42, {56, -1}, 1U},
        QuestRouteRegression{56, {57, -1}, 1U},
        QuestRouteRegression{57, {60, -1}, 1U},
        QuestRouteRegression{43, {62, -1}, 1U},
        QuestRouteRegression{62, {63, -1}, 1U},
        QuestRouteRegression{63, {64, -1}, 1U},

        QuestRouteRegression{76, {85, -1}, 1U},
        QuestRouteRegression{78, {88, -1}, 1U},
        QuestRouteRegression{88, {89, -1}, 1U},
        QuestRouteRegression{89, {91, -1}, 1U},
        QuestRouteRegression{80, {92, 94}, 2U},
        QuestRouteRegression{81, {92, -1}, 1U},
        QuestRouteRegression{92, {93, -1}, 1U},

        QuestRouteRegression{113, {114, -1}, 1U},
        QuestRouteRegression{121, {122, -1}, 1U},
        QuestRouteRegression{122, {123, -1}, 1U},
        QuestRouteRegression{123, {124, -1}, 1U},
    };
    for (const auto& regression : questRouteMatrix) {
        std::array<std::int32_t, MaximumStaticQuestRouteTargets>
            actualQuestTargets{};
        const auto questTargetCount = StaticQuestRouteTargetsFor(
            regression.from,
            actualQuestTargets);
        CHECK(questTargetCount == regression.targetCount);
        for (std::size_t index = 0U;
                index < regression.targetCount;
                ++index) {
            CHECK(actualQuestTargets[index] == regression.targets[index]);
            CHECK(IsStaticQuestRouteTarget(
                regression.from,
                regression.targets[index]));
        }

        std::array<std::int32_t, MaximumMainProgressionTargets>
            mainTargets{};
        const auto mainTargetCount = MainProgressionTargetsFor(
            regression.from,
            mainTargets);
        std::array<
            NavigationExitCandidate,
            MaximumMainProgressionTargets + MaximumStaticQuestRouteTargets>
            routeExits{};
        for (std::size_t index = 0U; index < mainTargetCount; ++index) {
            routeExits[index] = NavigationExitCandidate{
                .destinationId = static_cast<std::uint64_t>(
                    10'000 + mainTargets[index]),
                .targetLevelId = mainTargets[index],
                .subtileX = 100 + static_cast<std::int32_t>(index),
                .subtileY = 200 + mainTargets[index],
            };
        }
        for (std::size_t index = 0U; index < questTargetCount; ++index) {
            routeExits[mainTargetCount + index] = NavigationExitCandidate{
                .destinationId = static_cast<std::uint64_t>(
                    20'000 + actualQuestTargets[index]),
                .targetLevelId = actualQuestTargets[index],
                .subtileX = 300 + static_cast<std::int32_t>(index),
                .subtileY = 400 + actualQuestTargets[index],
            };
        }
        const auto routeExitCount = mainTargetCount + questTargetCount;
        const auto routeExitSpan = std::span(
            routeExits.data(),
            routeExitCount);
        CHECK(EvaluateNavigationResolutionCompleteness(
            regression.from,
            routeExitSpan) == NavigationResolutionCompleteness::Complete);
        if (mainTargetCount > 0U) {
            CHECK(EvaluateNavigationResolutionCompleteness(
                regression.from,
                std::span(
                    routeExits.data() + mainTargetCount,
                    questTargetCount))
                == NavigationResolutionCompleteness::PartialRetryable);
        }
        CHECK(BuildNavigationDestinations(
            NavigationPolicyInput{
                .currentLevelId = regression.from,
                .exits = routeExitSpan,
            },
            interiorDestinations)
            == (mainTargetCount > 0U ? 1U : 0U) + questTargetCount);

        const auto firstQuestDestination = mainTargetCount > 0U ? 1U : 0U;
        for (std::size_t index = 0U; index < questTargetCount; ++index) {
            const auto& destination =
                interiorDestinations[firstQuestDestination + index];
            CHECK(destination.kind == NavigationLineKind::Quest);
            CHECK(destination.destinationId == static_cast<std::uint64_t>(
                20'000 + actualQuestTargets[index]));
        }
        for (std::size_t omittedQuest = 0U;
                omittedQuest < questTargetCount;
                ++omittedQuest) {
            std::array<
                NavigationExitCandidate,
                MaximumMainProgressionTargets
                    + MaximumStaticQuestRouteTargets>
                incompleteRouteExits{};
            std::size_t incompleteRouteExitCount{};
            for (std::size_t index = 0U; index < routeExitCount; ++index) {
                if (index == mainTargetCount + omittedQuest) continue;
                incompleteRouteExits[incompleteRouteExitCount++] =
                    routeExits[index];
            }
            CHECK(EvaluateNavigationResolutionCompleteness(
                regression.from,
                std::span(
                    incompleteRouteExits.data(),
                    incompleteRouteExitCount))
                == NavigationResolutionCompleteness::PartialRetryable);
        }

        const auto preparationCount = BuildNavigationPreparationTargets(
            regression.from,
            std::span<const std::int32_t>{},
            preparationTargets);
        CHECK(preparationCount == mainTargetCount + questTargetCount);
        for (std::size_t index = 0U; index < mainTargetCount; ++index) {
            CHECK(preparationTargets[index] == mainTargets[index]);
        }
        for (std::size_t index = 0U; index < questTargetCount; ++index) {
            CHECK(preparationTargets[mainTargetCount + index]
                == actualQuestTargets[index]);
        }
    }

    struct QuestRouteExclusion final {
        std::int32_t from{};
        std::int32_t to{};
    };
    constexpr std::array questRouteExclusions{
        // Farming-only side areas.
        QuestRouteExclusion{3, 9},
        QuestRouteExclusion{6, 11},
        QuestRouteExclusion{7, 12},
        QuestRouteExclusion{17, 18},
        QuestRouteExclusion{17, 19},
        QuestRouteExclusion{41, 55},
        QuestRouteExclusion{44, 65},
        QuestRouteExclusion{76, 84},
        QuestRouteExclusion{78, 86},
        QuestRouteExclusion{80, 95},
        QuestRouteExclusion{81, 96},
        QuestRouteExclusion{81, 97},
        QuestRouteExclusion{82, 98},
        QuestRouteExclusion{82, 99},
        QuestRouteExclusion{115, 116},
        QuestRouteExclusion{118, 119},
        QuestRouteExclusion{111, 125},
        QuestRouteExclusion{112, 126},
        QuestRouteExclusion{117, 127},

        // Quest paths already owned by main progression stay green.
        QuestRouteExclusion{3, 4},
        QuestRouteExclusion{4, 10},
        QuestRouteExclusion{44, 45},
        QuestRouteExclusion{45, 58},
        QuestRouteExclusion{58, 61},
        QuestRouteExclusion{54, 74},
        QuestRouteExclusion{74, 46},
        QuestRouteExclusion{83, 100},
        QuestRouteExclusion{107, 108},
        QuestRouteExclusion{118, 120},
        QuestRouteExclusion{120, 128},

        // Secret, Pandemonium and separately quest-selected destinations.
        QuestRouteExclusion{1, 39},
        QuestRouteExclusion{46, 66},
        QuestRouteExclusion{46, 67},
        QuestRouteExclusion{46, 68},
        QuestRouteExclusion{46, 69},
        QuestRouteExclusion{46, 70},
        QuestRouteExclusion{46, 71},
        QuestRouteExclusion{46, 72},
        QuestRouteExclusion{109, 133},
        QuestRouteExclusion{109, 134},
        QuestRouteExclusion{109, 135},
        QuestRouteExclusion{109, 136},
    };
    for (const auto& exclusion : questRouteExclusions) {
        CHECK(!IsStaticQuestRouteTarget(exclusion.from, exclusion.to));
        std::array<std::int32_t, MaximumStaticQuestRouteTargets>
            actualQuestTargets{};
        const auto targetCount = StaticQuestRouteTargetsFor(
            exclusion.from,
            actualQuestTargets);
        CHECK(std::find(
            actualQuestTargets.begin(),
            actualQuestTargets.begin() + targetCount,
            exclusion.to) == actualQuestTargets.begin() + targetCount);
    }
    CHECK(!IsStaticQuestRouteTarget(-1, 8));
    CHECK(!IsStaticQuestRouteTarget(2, -1));
    CHECK(StaticQuestRouteTargetsFor(
        -1,
        preparationTargets) == 0U);

    const std::array tamoeProgressionAndPitExits{
        NavigationExitCandidate{26U, 26, 1'700, 2'600},
        NavigationExitCandidate{12U, 12, 1'701, 2'601},
    };
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 7,
            .exits = tamoeProgressionAndPitExits,
        },
        interiorDestinations) == 1U);
    CHECK(interiorDestinations[0].destinationId == 26U);
    CHECK(interiorDestinations[0].kind == NavigationLineKind::Progression);

    const std::array spiderMarshOnlyExit{
        NavigationExitCandidate{77U, 77, 1'760, 2'770},
        NavigationExitCandidate{85U, 85, 1'785, 2'850},
    };
    CHECK(SelectMainProgressionTargetFor(
        76,
        spiderMarshOnlyExit).value_or(-1) == 77);
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 76,
            .exits = spiderMarshOnlyExit,
        },
        interiorDestinations) == 2U);
    CHECK(interiorDestinations[0].destinationId == 77U);
    CHECK(interiorDestinations[0].kind == NavigationLineKind::Progression);
    CHECK(interiorDestinations[1].destinationId == 85U);
    CHECK(interiorDestinations[1].kind == NavigationLineKind::Quest);
    CHECK(EvaluateNavigationResolutionCompleteness(
        76,
        spiderMarshOnlyExit) == NavigationResolutionCompleteness::Complete);

    const std::array spiderBothExits{
        NavigationExitCandidate{77U, 77, 1'760, 2'770},
        NavigationExitCandidate{78U, 78, 1'761, 2'780},
        NavigationExitCandidate{85U, 85, 1'785, 2'850},
    };
    CHECK(SelectMainProgressionTargetFor(
        76,
        spiderBothExits).value_or(-1) == 78);
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 76,
            .exits = spiderBothExits,
        },
        interiorDestinations) == 2U);
    CHECK(interiorDestinations[0].destinationId == 78U);
    CHECK(interiorDestinations[0].kind == NavigationLineKind::Progression);
    CHECK(interiorDestinations[1].destinationId == 85U);
    CHECK(interiorDestinations[1].kind == NavigationLineKind::Quest);
    CHECK(EvaluateNavigationResolutionCompleteness(
        76,
        spiderBothExits) == NavigationResolutionCompleteness::Complete);
    CHECK(EvaluateNavigationResolutionCompleteness(
        76,
        std::span<const NavigationExitCandidate>{})
        == NavigationResolutionCompleteness::PartialRetryable);
    CHECK(!HasDynamicMainProgressionTargetFor(76));
    CHECK(!DynamicMainProgressionTargetFor(74, 59).has_value());
    CHECK(!DynamicMainProgressionTargetFor(3, -1).has_value());
    const auto arcaneSummoner = PresetMainProgressionTargetFor(74, 1U, 250);
    CHECK(arcaneSummoner.has_value());
    CHECK(arcaneSummoner->targetLevelId == 46);
    CHECK(arcaneSummoner->kind == NavigationPresetProgressionKind::Boss);
    const auto arcaneTome = PresetMainProgressionTargetFor(74, 2U, 357);
    CHECK(arcaneTome.has_value());
    CHECK(arcaneTome->targetLevelId == 46);
    CHECK(arcaneTome->kind
        == NavigationPresetProgressionKind::QuestObject);
    CHECK(!PresetMainProgressionTargetFor(74, 2U, 60).has_value());
    CHECK(!PresetMainProgressionTargetFor(73, 1U, 250).has_value());

    struct QuestPresetRegression final {
        std::int32_t levelId{};
        std::uint32_t presetType{};
        std::int32_t presetClassId{};
        NavigationDestinationSelection selection{
            NavigationDestinationSelection::All};
    };
    constexpr std::array questPresetMatrix{
        QuestPresetRegression{4, 2U, 21},
        QuestPresetRegression{5, 2U, 30},
        QuestPresetRegression{38, 2U, 26},
        QuestPresetRegression{28, 2U, 108},
        QuestPresetRegression{60, 2U, 354},
        QuestPresetRegression{61, 2U, 149},
        QuestPresetRegression{64, 2U, 356},
        QuestPresetRegression{66, 2U, 152},
        QuestPresetRegression{67, 2U, 152},
        QuestPresetRegression{68, 2U, 152},
        QuestPresetRegression{69, 2U, 152},
        QuestPresetRegression{70, 2U, 152},
        QuestPresetRegression{71, 2U, 152},
        QuestPresetRegression{72, 2U, 152},
        QuestPresetRegression{85, 2U, 407},
        QuestPresetRegression{91, 2U, 406},
        QuestPresetRegression{93, 2U, 405},
        QuestPresetRegression{94, 2U, 193},
        QuestPresetRegression{83, 2U, 404},
        QuestPresetRegression{107, 2U, 376},
        QuestPresetRegression{
            111,
            2U,
            473,
            NavigationDestinationSelection::NearestToPlayer},
        QuestPresetRegression{114, 2U, 558},
        QuestPresetRegression{120, 2U, 546},
    };
    for (const auto& regression : questPresetMatrix) {
        const auto target = StaticQuestPresetTargetFor(
            regression.levelId,
            regression.presetType,
            regression.presetClassId);
        CHECK(target.has_value());
        CHECK(target->selection == regression.selection);
        CHECK(!StaticQuestPresetTargetFor(
            regression.levelId,
            regression.presetType == 2U ? 1U : 2U,
            regression.presetClassId).has_value());
    }
    CHECK(!StaticQuestPresetTargetFor(4, 2U, 30).has_value());
    CHECK(!StaticQuestPresetTargetFor(78, 2U, 407).has_value());
    CHECK(!StaticQuestPresetTargetFor(111, 2U, -1).has_value());

    std::int32_t converted{};
    CHECK(CheckedNavigationSubtileCoordinate(1085, 0, converted));
    CHECK(converted == 5425);
    CHECK(CheckedNavigationSubtileCoordinate(100, 3, converted));
    CHECK(converted == 503);
    CHECK(!CheckedNavigationSubtileCoordinate(-1, 0, converted));
    CHECK(!CheckedNavigationSubtileCoordinate(
        (std::numeric_limits<std::int32_t>::max)(),
        0,
        converted));

    NavigationNativePoint client{};
    CHECK(ConvertNavigationSubtileToClientCoordinates(20, 10, client));
    CHECK(client.x == 160);
    CHECK(client.y == 240);
    CHECK(ConvertNavigationSubtileToClientCoordinates(100, 200, client));
    CHECK(client.x == -1'600);
    CHECK(client.y == 2'400);
    CHECK(!ConvertNavigationSubtileToClientCoordinates(-1, 0, client));
    CHECK(!ConvertNavigationSubtileToClientCoordinates(
        (std::numeric_limits<std::int32_t>::max)(),
        (std::numeric_limits<std::int32_t>::max)(),
        client));
    NavigationNativePoint subtile{};
    CHECK(ConvertNavigationClientToSubtileCoordinates(160, 240, subtile));
    CHECK(subtile.x == 20);
    CHECK(subtile.y == 10);
    CHECK(ConvertNavigationClientToSubtileCoordinates(
        -1'600,
        2'400,
        subtile));
    CHECK(subtile.x == 100);
    CHECK(subtile.y == 200);
    CHECK(!ConvertNavigationClientToSubtileCoordinates(161, 240, subtile));

    const std::array exits{
        NavigationExitCandidate{100U, 4, 400, 500},
        NavigationExitCandidate{101U, 9, 600, 700},
    };
    const NavigationPointCandidate waypoint{
        .destinationId = 200U,
        .subtileX = 300,
        .subtileY = 350,
        .exactClientX = -800,
        .exactClientY = 5'200,
        .useExactClientCoordinates = true,
    };
    const std::array quests{
        NavigationPointCandidate{300U, 900, 950},
    };
    const std::array customTargetIds{9};
    std::array<NavigationSubtileDestination, 8U> destinations{};
    const auto count = BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 3,
            .exits = exits,
            .waypoint = &waypoint,
            .questTargets = quests,
            .customTargetLevelIds = customTargetIds,
        },
        destinations);
    CHECK(count == 4U);
    CHECK(destinations[0].kind == NavigationLineKind::Waypoint);
    CHECK(destinations[0].useExactClientCoordinates);
    CHECK(destinations[0].exactClientX == -800);
    CHECK(destinations[0].exactClientY == 5'200);
    CHECK(destinations[1].kind == NavigationLineKind::Progression);
    CHECK(destinations[1].destinationId == 100U);
    CHECK(destinations[2].kind == NavigationLineKind::CustomLevel);
    CHECK(destinations[2].destinationId == 101U);
    CHECK(destinations[3].kind == NavigationLineKind::Quest);
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 1,
            .inTown = true,
            .exits = exits,
            .waypoint = &waypoint,
            .questTargets = quests,
            .customTargetLevelIds = customTargetIds,
        },
        destinations) == 0U);
    CHECK(EvaluateNavigationResolutionCompleteness(
        3,
        exits) == NavigationResolutionCompleteness::PartialRetryable);
    const std::array coldPlainsCompleteExits{
        exits[0],
        exits[1],
        NavigationExitCandidate{102U, 17, 800, 900},
    };
    CHECK(EvaluateNavigationResolutionCompleteness(
        3,
        coldPlainsCompleteExits)
        == NavigationResolutionCompleteness::Complete);
    CHECK(EvaluateNavigationResolutionCompleteness(
        3,
        std::span<const NavigationExitCandidate>{})
        == NavigationResolutionCompleteness::PartialRetryable);

    const std::array overlapTargetIds{4};
    const auto overlapCount = BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 3,
            .exits = exits,
            .customTargetLevelIds = overlapTargetIds,
        },
        destinations);
    CHECK(overlapCount == 2U);
    CHECK(destinations[0].kind == NavigationLineKind::Progression);
    CHECK(destinations[1].kind == NavigationLineKind::CustomLevel);

    const std::array canyonCorrectTombExit{
        NavigationExitCandidate{
            .destinationId = 700U,
            .targetLevelId = 70,
            .subtileX = 4'600,
            .subtileY = 5'700,
            .exactClientX = -8'800,
            .exactClientY = 9'600,
            .useExactClientCoordinates = true,
        },
    };
    const std::array canyonQuestTarget{
        NavigationPointCandidate{
            .destinationId = canyonCorrectTombExit[0].destinationId,
            .subtileX = canyonCorrectTombExit[0].subtileX,
            .subtileY = canyonCorrectTombExit[0].subtileY,
            .exactClientX = canyonCorrectTombExit[0].exactClientX,
            .exactClientY = canyonCorrectTombExit[0].exactClientY,
            .useExactClientCoordinates = true,
        },
    };
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 46,
            .exits = canyonCorrectTombExit,
            .questTargets = canyonQuestTarget,
        },
        destinations) == 1U);
    CHECK(destinations[0].kind == NavigationLineKind::Quest);
    CHECK(destinations[0].destinationId == 700U);
    CHECK(destinations[0].useExactClientCoordinates);
    CHECK(destinations[0].exactClientX == -8'800);
    CHECK(destinations[0].exactClientY == 9'600);

    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 46,
            .exits = canyonCorrectTombExit,
            .progressionTargetOverride = 70,
        },
        destinations) == 1U);
    CHECK(destinations[0].kind == NavigationLineKind::Progression);
    CHECK(destinations[0].destinationId == 700U);
    CHECK(destinations[0].useExactClientCoordinates);
    CHECK(destinations[0].exactClientX == -8'800);
    CHECK(destinations[0].exactClientY == 9'600);

    const std::array staffOrificeQuestTarget{
        NavigationPointCandidate{
            .destinationId = 800U,
            .subtileX = 5'100,
            .subtileY = 5'200,
        },
    };
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 66,
            .questTargets = staffOrificeQuestTarget,
        },
        destinations) == 1U);
    CHECK(destinations[0].kind == NavigationLineKind::Quest);
    CHECK(destinations[0].destinationId == 800U);

    const std::array durielPortalExit{
        NavigationExitCandidate{
            .destinationId = 801U,
            .targetLevelId = 73,
            .subtileX = 5'300,
            .subtileY = 5'400,
        },
    };
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 66,
            .exits = durielPortalExit,
            .questTargets = staffOrificeQuestTarget,
        },
        destinations) == 1U);
    CHECK(destinations[0].kind == NavigationLineKind::Progression);
    CHECK(destinations[0].destinationId == 801U);

    std::array<NavigationSubtileDestination, 1U> bounded{};
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 3,
            .exits = exits,
            .waypoint = &waypoint,
            .questTargets = quests,
            .customTargetLevelIds = customTargetIds,
        },
        bounded) == 1U);
}

void CheckNavigationResolverHelpers() {
    using namespace RuffnecKk::MapSense;

    const std::array waypointCandidates{
        Detail::NavigationWaypointPresetCandidate{100, 200, 503, 1'004, 119},
        Detail::NavigationWaypointPresetCandidate{100, 200, 501, 1'006, 145},
        Detail::NavigationWaypointPresetCandidate{101, 200, 507, 1'004, 156},
    };
    const auto exactWaypoint = Detail::SelectExactWaypointPreset(
        100,
        200,
        waypointCandidates);
    CHECK(exactWaypoint.has_value());
    CHECK(exactWaypoint->subtileX == 503);
    CHECK(exactWaypoint->subtileY == 1'004);
    CHECK(exactWaypoint->classId == 119);
    CHECK(!Detail::SelectExactWaypointPreset(
        99,
        200,
        waypointCandidates).has_value());

    const std::array onePassiveWaypoint{
        Detail::NavigationWaypointPresetCandidate{120, 240, 600, 1'200, 119},
    };
    const std::array twoPassiveWaypoints{
        Detail::NavigationWaypointPresetCandidate{120, 240, 600, 1'200, 119},
        Detail::NavigationWaypointPresetCandidate{121, 240, 605, 1'200, 145},
    };
    const std::array<Detail::NavigationWaypointPresetCandidate, 0U>
        noPassiveWaypoints{};
    const auto passiveExact = Detail::SelectPassiveWaypointPreset(
        onePassiveWaypoint,
        false);
    CHECK(!passiveExact.pending);
    CHECK(passiveExact.exact.has_value());
    CHECK(passiveExact.exact->subtileX == 600);
    const auto passiveNone = Detail::SelectPassiveWaypointPreset(
        noPassiveWaypoints,
        false);
    CHECK(!passiveNone.pending);
    CHECK(!passiveNone.exact.has_value());
    CHECK(Detail::SelectPassiveWaypointPreset(
        twoPassiveWaypoints,
        false).pending);
    CHECK(Detail::SelectPassiveWaypointPreset(
        onePassiveWaypoint,
        true).pending);

    const auto exitOnly = Detail::MakePassivePoiPublicationPolicy(true, false);
    CHECK(!exitOnly.publishExitLabels);
    CHECK(exitOnly.mergeProvenExitFragments);
    CHECK(exitOnly.publishWaypoint);
    const auto waypointOnly = Detail::MakePassivePoiPublicationPolicy(false, true);
    CHECK(waypointOnly.publishExitLabels);
    CHECK(!waypointOnly.mergeProvenExitFragments);
    CHECK(!waypointOnly.publishWaypoint);

    const std::array roomTileLinks{
        Detail::NavigationRoomTileLinkCandidate{501, 29},
        Detail::NavigationRoomTileLinkCandidate{502, 30},
        Detail::NavigationRoomTileLinkCandidate{503, 12},
        Detail::NavigationRoomTileLinkCandidate{504, -1},
        Detail::NavigationRoomTileLinkCandidate{505, 14},
    };
    CHECK(Detail::SelectRoomTileTargetLevel(501, roomTileLinks)
        .value_or(-1) == 29);
    CHECK(Detail::SelectRoomTileTargetLevel(502, roomTileLinks)
        .value_or(-1) == 30);
    CHECK(Detail::SelectRoomTileTargetLevel(503, roomTileLinks)
        .value_or(-1) == 12);
    CHECK(Detail::SelectRoomTileTargetLevel(505, roomTileLinks)
        .value_or(-1) == 14);
    CHECK(!Detail::SelectRoomTileTargetLevel(
        504,
        roomTileLinks).has_value());
    CHECK(!Detail::SelectRoomTileTargetLevel(
        999,
        roomTileLinks).has_value());

    const std::array tamoeVisibleTargets{26, 12, 6, 0, 0, 0, 0, 0};
    CHECK(Detail::IsDirectVisibleTarget(7, 26, tamoeVisibleTargets));
    CHECK(Detail::IsDirectVisibleTarget(7, 12, tamoeVisibleTargets));
    CHECK(!Detail::IsDirectVisibleTarget(7, 65, tamoeVisibleTargets));
    CHECK(!Detail::IsDirectVisibleTarget(7, 7, tamoeVisibleTargets));

    std::array<NavigationSubtileDestination, 4U> routeDestinations{};
    const std::array barracksExit{
        NavigationExitCandidate{1U, 29, 1'100, 1'200},
    };
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{.currentLevelId = 28, .exits = barracksExit},
        routeDestinations) == 1U);
    CHECK(routeDestinations[0].kind == NavigationLineKind::Progression);
    CHECK(routeDestinations[0].subtileX == 1'100);
    const std::array jailOneExit{
        NavigationExitCandidate{2U, 30, 1'300, 1'400},
    };
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{.currentLevelId = 29, .exits = jailOneExit},
        routeDestinations) == 1U);
    CHECK(routeDestinations[0].kind == NavigationLineKind::Progression);
    CHECK(routeDestinations[0].subtileY == 1'400);
    const std::array pitExit{
        NavigationExitCandidate{3U, 12, 1'500, 1'600},
    };
    const std::array pitTarget{12};
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 7,
            .exits = pitExit,
            .customTargetLevelIds = pitTarget,
        },
        routeDestinations) == 1U);
    CHECK(routeDestinations[0].kind == NavigationLineKind::CustomLevel);
    CHECK(routeDestinations[0].subtileX == 1'500);
    CHECK(routeDestinations[0].subtileY == 1'600);
    const std::array undergroundTwoExit{
        NavigationExitCandidate{4U, 14, 1'700, 1'800},
    };
    const std::array undergroundTwoTarget{14};
    CHECK(BuildNavigationDestinations(
        NavigationPolicyInput{
            .currentLevelId = 10,
            .exits = undergroundTwoExit,
            .customTargetLevelIds = undergroundTwoTarget,
        },
        routeDestinations) == 1U);
    CHECK(routeDestinations[0].kind == NavigationLineKind::CustomLevel);
    CHECK(routeDestinations[0].subtileX == 1'700);
    CHECK(routeDestinations[0].subtileY == 1'800);

    const auto source = Detail::NavigationRoomRectangle{
        .levelId = 3,
        .tileX = 100,
        .tileY = 200,
        .width = 2,
        .height = 2,
    };
    std::array<std::uint16_t, 100U> sourceCollisionCells{};
    std::array<std::uint16_t, 100U> neighbourCollisionCells{};
    const auto collisionGrid = [](
            std::array<std::uint16_t, 100U>& cells,
            std::int32_t originX,
            std::int32_t originY) noexcept {
        return Detail::NavigationCollisionGridView{
            .originX = originX,
            .originY = originY,
            .width = 10,
            .height = 10,
            .cells = std::span<const std::uint16_t>(cells),
        };
    };
    const auto setCell = [](
            std::array<std::uint16_t, 100U>& cells,
            std::int32_t x,
            std::int32_t y,
            std::uint16_t value) noexcept {
        cells[static_cast<std::size_t>(y * 10 + x)] = value;
    };
    Detail::NavigationOutdoorOpening opening{};

    std::int32_t checkedSubtile{123};
    CHECK(Detail::TryAddNavigationSubtileOffset(
        (std::numeric_limits<std::int32_t>::max)() - 1,
        1,
        checkedSubtile));
    CHECK(checkedSubtile == (std::numeric_limits<std::int32_t>::max)());
    checkedSubtile = 123;
    CHECK(!Detail::TryAddNavigationSubtileOffset(
        (std::numeric_limits<std::int32_t>::max)(),
        1,
        checkedSubtile));
    CHECK(checkedSubtile == 123);
    CHECK(!Detail::TryAddNavigationSubtileOffset(-1, 1, checkedSubtile));

    sourceCollisionCells.fill(1U);
    neighbourCollisionCells.fill(1U);
    for (std::int32_t y = 2; y < 7; ++y) {
        setCell(sourceCollisionCells, 8, y, 0U);
        setCell(sourceCollisionCells, 9, y, 0U);
        setCell(neighbourCollisionCells, 0, y, 0U);
        setCell(neighbourCollisionCells, 1, y, 0U);
    }
    CHECK(Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 102, 200, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 510, 1'000),
        opening));
    CHECK(opening.subtileX == 510);
    CHECK(opening.subtileY == 1'004);
    CHECK(opening.spanSubtiles == 5);

    // The absolute midpoint remains representable even when adding it to the
    // grid origin as an intermediate 32-bit expression would overflow.
    constexpr std::int32_t highTileY = 214'748'364;
    constexpr std::int32_t highOriginY = highTileY * 5;
    sourceCollisionCells.fill(1U);
    neighbourCollisionCells.fill(1U);
    for (std::int32_t y = 7; y < 10; ++y) {
        setCell(sourceCollisionCells, 8, y, 0U);
        setCell(sourceCollisionCells, 9, y, 0U);
        setCell(neighbourCollisionCells, 0, y, 0U);
        setCell(neighbourCollisionCells, 1, y, 0U);
    }
    CHECK(Detail::FindOutdoorCollisionOpening(
        Detail::NavigationRoomRectangle{3, 100, highTileY, 2, 2},
        Detail::NavigationRoomRectangle{4, 102, highTileY, 2, 2},
        collisionGrid(sourceCollisionCells, 500, highOriginY),
        collisionGrid(neighbourCollisionCells, 510, highOriginY),
        opening));
    CHECK(opening.subtileX == 510);
    CHECK(opening.subtileY == highOriginY + 8);
    CHECK(opening.spanSubtiles == 3);

    sourceCollisionCells.fill(1U);
    neighbourCollisionCells.fill(1U);
    for (std::int32_t y = 3; y < 7; ++y) {
        setCell(sourceCollisionCells, 0, y, 0U);
        setCell(sourceCollisionCells, 1, y, 0U);
        setCell(neighbourCollisionCells, 8, y, 0U);
        setCell(neighbourCollisionCells, 9, y, 0U);
    }
    CHECK(Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 98, 200, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 490, 1'000),
        opening));
    CHECK(opening.subtileX == 499);
    CHECK(opening.subtileY == 1'005);
    CHECK(opening.spanSubtiles == 4);

    sourceCollisionCells.fill(1U);
    neighbourCollisionCells.fill(1U);
    for (std::int32_t x = 1; x < 7; ++x) {
        setCell(sourceCollisionCells, x, 8, 0U);
        setCell(sourceCollisionCells, x, 9, 0U);
        setCell(neighbourCollisionCells, x, 0, 0U);
        setCell(neighbourCollisionCells, x, 1, 0U);
    }
    CHECK(Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 100, 202, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 500, 1'010),
        opening));
    CHECK(opening.subtileX == 504);
    CHECK(opening.subtileY == 1'010);
    CHECK(opening.spanSubtiles == 6);

    sourceCollisionCells.fill(1U);
    neighbourCollisionCells.fill(1U);
    for (std::int32_t x = 4; x < 7; ++x) {
        setCell(sourceCollisionCells, x, 0, 0U);
        setCell(sourceCollisionCells, x, 1, 0U);
        setCell(neighbourCollisionCells, x, 8, 0U);
        setCell(neighbourCollisionCells, x, 9, 0U);
    }
    CHECK(Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 100, 198, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 500, 990),
        opening));
    CHECK(opening.subtileX == 505);
    CHECK(opening.subtileY == 999);
    CHECK(opening.spanSubtiles == 3);

    // A broad source-side gap is reduced to the exact intersection exposed by
    // the destination room. This is the Tamoe/Monastery regression contract.
    sourceCollisionCells.fill(1U);
    neighbourCollisionCells.fill(1U);
    for (std::int32_t y = 0; y < 10; ++y) {
        setCell(sourceCollisionCells, 8, y, 0U);
        setCell(sourceCollisionCells, 9, y, 0U);
    }
    for (std::int32_t y = 4; y < 7; ++y) {
        setCell(neighbourCollisionCells, 0, y, 0U);
        setCell(neighbourCollisionCells, 1, y, 0U);
    }
    CHECK(Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 102, 200, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 510, 1'000),
        opening));
    CHECK(opening.subtileX == 510);
    CHECK(opening.subtileY == 1'005);
    CHECK(opening.spanSubtiles == 3);

    neighbourCollisionCells.fill(1U);
    for (std::int32_t y = 4; y < 6; ++y) {
        setCell(neighbourCollisionCells, 0, y, 0U);
        setCell(neighbourCollisionCells, 1, y, 0U);
    }
    CHECK(!Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 102, 200, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 510, 1'000),
        opening));
    CHECK(!Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 102, 202, 2, 2},
        collisionGrid(sourceCollisionCells, 500, 1'000),
        collisionGrid(neighbourCollisionCells, 510, 1'000),
        opening));
    auto wrongGrid = collisionGrid(sourceCollisionCells, 500, 1'000);
    wrongGrid.originX = 499;
    CHECK(!Detail::FindOutdoorCollisionOpening(
        source,
        Detail::NavigationRoomRectangle{4, 102, 200, 2, 2},
        wrongGrid,
        collisionGrid(neighbourCollisionCells, 510, 1'000),
        opening));

    // Room fragments from both sides of one real level boundary must merge
    // before intersection. The endpoint stays on the current level's outer
    // edge, not on an arbitrary destination Room edge.
    std::array fragmentedSourceSpans{
        Detail::NavigationBoundarySpan{
            3, Detail::NavigationBoundarySide::Right, 1'040, 1'043},
        Detail::NavigationBoundarySpan{
            3, Detail::NavigationBoundarySide::Right, 1'043, 1'047},
    };
    std::array fragmentedTargetSpans{
        Detail::NavigationBoundarySpan{
            26, Detail::NavigationBoundarySide::Right, 1'039, 1'044},
        Detail::NavigationBoundarySpan{
            26, Detail::NavigationBoundarySide::Right, 1'044, 1'048},
    };
    std::size_t mergedSourceSpanCount{};
    std::size_t mergedTargetSpanCount{};
    CHECK(Detail::MergeOutdoorBoundarySpans(
        fragmentedSourceSpans,
        fragmentedSourceSpans.size(),
        mergedSourceSpanCount));
    CHECK(Detail::MergeOutdoorBoundarySpans(
        fragmentedTargetSpans,
        fragmentedTargetSpans.size(),
        mergedTargetSpanCount));
    CHECK(mergedSourceSpanCount == 1U);
    CHECK(mergedTargetSpanCount == 1U);
    CHECK(fragmentedSourceSpans[0].startSubtile == 1'040);
    CHECK(fragmentedSourceSpans[0].endSubtile == 1'047);
    CHECK(fragmentedTargetSpans[0].startSubtile == 1'039);
    CHECK(fragmentedTargetSpans[0].endSubtile == 1'048);
    const auto levelBounds = Detail::NavigationLevelSubtileBounds{
        .left = 500,
        .top = 1'000,
        .right = 800,
        .bottom = 1'300,
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        26,
        std::span(fragmentedSourceSpans).first(mergedSourceSpanCount),
        std::span(fragmentedTargetSpans).first(mergedTargetSpanCount),
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Found);
    CHECK(opening.subtileX == 799);
    CHECK(opening.subtileY == 1'043);
    CHECK(opening.spanSubtiles == 7);

    // Runtime spans retain the exact RoomsNear pair and its fixed seam. Two
    // fragments of that same pair may merge, and the endpoint must stay on
    // the source cell beside the shared seam rather than being reconstructed
    // from the complete level's outer bounds.
    std::array pairedSourceFragments{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'040,
            .endSubtile = 1'043,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x1000U,
            .targetRoomIdentity = 0x2000U,
        },
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'043,
            .endSubtile = 1'047,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x1000U,
            .targetRoomIdentity = 0x2000U,
        },
    };
    std::array pairedTargetFragments{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'039,
            .endSubtile = 1'044,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x1000U,
            .targetRoomIdentity = 0x2000U,
        },
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'044,
            .endSubtile = 1'048,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x1000U,
            .targetRoomIdentity = 0x2000U,
        },
    };
    std::size_t pairedSourceCount{};
    std::size_t pairedTargetCount{};
    CHECK(Detail::MergeOutdoorBoundarySpans(
        pairedSourceFragments,
        pairedSourceFragments.size(),
        pairedSourceCount));
    CHECK(Detail::MergeOutdoorBoundarySpans(
        pairedTargetFragments,
        pairedTargetFragments.size(),
        pairedTargetCount));
    CHECK(pairedSourceCount == 1U);
    CHECK(pairedTargetCount == 1U);
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        26,
        std::span(pairedSourceFragments).first(pairedSourceCount),
        std::span(pairedTargetFragments).first(pairedTargetCount),
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Found);
    CHECK(opening.subtileX == 649);
    CHECK(opening.subtileY == 1'043);
    CHECK(opening.spanSubtiles == 7);
    const auto normalizedPairedBoundary = opening.boundaryIdentity;
    CHECK(normalizedPairedBoundary.Valid());
    CHECK(normalizedPairedBoundary.axis
        == NavigationBoundaryAxis::Vertical);
    CHECK(normalizedPairedBoundary.fixedSubtile == 650);
    CHECK(normalizedPairedBoundary.startSubtile == 1'040);
    CHECK(normalizedPairedBoundary.endSubtile == 1'047);

    // The reciprocal level sees the same seam from its opposite side. Its
    // projected point may differ by one source cell, but its retained physical
    // identity must be byte-for-byte equal for reveal-wide deduplication.
    const std::array reciprocalSourceSpans{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Left,
            .startSubtile = 1'040,
            .endSubtile = 1'047,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x2000U,
            .targetRoomIdentity = 0x1000U,
        },
    };
    const std::array reciprocalTargetSpans{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Left,
            .startSubtile = 1'039,
            .endSubtile = 1'048,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x2000U,
            .targetRoomIdentity = 0x1000U,
        },
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        7,
        reciprocalSourceSpans,
        reciprocalTargetSpans,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Found);
    CHECK(opening.boundaryIdentity == normalizedPairedBoundary);

    // Exact RoomsNear identities remain separate even when side, fixed seam,
    // and projected interval are identical. A target fragment from the other
    // pair must not cross-match the first source pair.
    std::array parallelSourceSpans{
        pairedSourceFragments[0],
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'040,
            .endSubtile = 1'047,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x3000U,
            .targetRoomIdentity = 0x4000U,
        },
    };
    std::size_t parallelSourceCount{};
    CHECK(Detail::MergeOutdoorBoundarySpans(
        parallelSourceSpans,
        parallelSourceSpans.size(),
        parallelSourceCount));
    CHECK(parallelSourceCount == 2U);
    const std::array crossedPairTarget{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'040,
            .endSubtile = 1'047,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x3000U,
            .targetRoomIdentity = 0x4000U,
        },
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        26,
        std::span(pairedSourceFragments).first(pairedSourceCount),
        crossedPairTarget,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::NotFound);

    // If two explicit RoomsNear pairs expose different valid seams, the
    // resolver must fail closed instead of selecting either one.
    const std::array explicitAmbiguousSource{
        pairedSourceFragments[0],
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'050,
            .endSubtile = 1'056,
            .fixedSubtile = 700,
            .sourceRoomIdentity = 0x3000U,
            .targetRoomIdentity = 0x4000U,
        },
    };
    const std::array explicitAmbiguousTarget{
        pairedTargetFragments[0],
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'050,
            .endSubtile = 1'056,
            .fixedSubtile = 700,
            .sourceRoomIdentity = 0x3000U,
            .targetRoomIdentity = 0x4000U,
        },
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        26,
        explicitAmbiguousSource,
        explicitAmbiguousTarget,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Ambiguous);

    CHECK(Detail::HasNavigationOutdoorVisibilitySlot(0x10U, 0U));
    CHECK(Detail::HasNavigationOutdoorVisibilitySlot(0x800U, 7U));
    CHECK(!Detail::HasNavigationOutdoorVisibilitySlot(0x20U, 0U));
    CHECK(!Detail::HasNavigationOutdoorVisibilitySlot(0x800U, 8U));
    CHECK(Detail::IsNavigationPlayerPathOpen(0U));
    CHECK(!Detail::IsNavigationPlayerPathOpen(0x0001U));
    CHECK(!Detail::IsNavigationPlayerPathOpen(0x0008U));
    CHECK(!Detail::IsNavigationPlayerPathOpen(0x0400U));
    CHECK(!Detail::IsNavigationPlayerPathOpen(0x0800U));
    CHECK(!Detail::IsNavigationPlayerPathOpen(0x1000U));
    CHECK(Detail::IsNavigationPlayerPathOpen(0x0080U));
    CHECK(Detail::IsNavigationPlayerPathOpen(0x0100U));

    Detail::NavigationOutdoorOpening nativeMonasteryAnchor{};
    CHECK(Detail::TryMakeNavigationLevelTileAnchor(
        100,
        200,
        27,
        13,
        nativeMonasteryAnchor));
    CHECK(nativeMonasteryAnchor.subtileX == 635);
    CHECK(nativeMonasteryAnchor.subtileY == 1'065);
    CHECK(nativeMonasteryAnchor.spanSubtiles == 0);
    CHECK(!Detail::TryMakeNavigationLevelTileAnchor(
        (std::numeric_limits<std::int32_t>::max)(),
        200,
        27,
        13,
        nativeMonasteryAnchor));

    // Multiple exact player paths to one outdoor target are legitimate in the
    // jungle generator. Strict callers can reject them, while runtime keeps
    // the first geographically sorted path; width is not selection evidence.
    const auto tamoeBounds = Detail::NavigationLevelSubtileBounds{
        .left = 14'900,
        .top = 4'900,
        .right = 15'500,
        .bottom = 5'500,
    };
    const std::array tamoeFacadeSource{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Bottom,
            .startSubtile = 15'040,
            .endSubtile = 15'044,
            .fixedSubtile = 5'091,
            .sourceRoomIdentity = 0x5000U,
            .targetRoomIdentity = 0x6000U,
        },
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Bottom,
            .startSubtile = 15'142,
            .endSubtile = 15'182,
            .fixedSubtile = 5'091,
            .sourceRoomIdentity = 0x7000U,
            .targetRoomIdentity = 0x8000U,
        },
    };
    const std::array tamoeFacadeTarget{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Bottom,
            .startSubtile = 15'040,
            .endSubtile = 15'044,
            .fixedSubtile = 5'091,
            .sourceRoomIdentity = 0x5000U,
            .targetRoomIdentity = 0x6000U,
        },
        Detail::NavigationBoundarySpan{
            .targetLevelId = 26,
            .side = Detail::NavigationBoundarySide::Bottom,
            .startSubtile = 15'142,
            .endSubtile = 15'182,
            .fixedSubtile = 5'091,
            .sourceRoomIdentity = 0x7000U,
            .targetRoomIdentity = 0x8000U,
        },
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        tamoeBounds,
        26,
        tamoeFacadeSource,
        tamoeFacadeTarget,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Ambiguous);
    std::array<Detail::NavigationOutdoorOpening, 4> tamoePhysicalOpenings{};
    std::size_t tamoePhysicalOpeningCount{};
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        tamoeBounds,
        26,
        tamoeFacadeSource,
        tamoeFacadeTarget,
        opening,
        Detail::NavigationOutdoorOpeningSelectionPolicy::
            AcceptStablePlayerPath,
        tamoePhysicalOpenings,
        &tamoePhysicalOpeningCount)
        == Detail::NavigationOutdoorBoundaryMatchResult::Found);
    CHECK(opening.subtileX == 15'042);
    CHECK(opening.subtileY == 5'090);
    CHECK(opening.spanSubtiles == 4);
    CHECK(tamoePhysicalOpeningCount == 2U);
    CHECK(tamoePhysicalOpenings[0].subtileX == 15'042);
    CHECK(tamoePhysicalOpenings[0].subtileY == 5'090);
    CHECK(tamoePhysicalOpenings[1].subtileX == 15'162);
    CHECK(tamoePhysicalOpenings[1].subtileY == 5'090);
    CHECK(tamoePhysicalOpenings[1].spanSubtiles == 40);
    CHECK(tamoePhysicalOpenings[0].boundaryIdentity.Valid());
    CHECK(tamoePhysicalOpenings[1].boundaryIdentity.Valid());
    CHECK(tamoePhysicalOpenings[0].boundaryIdentity.axis
        == NavigationBoundaryAxis::Horizontal);
    CHECK(tamoePhysicalOpenings[1].boundaryIdentity.axis
        == NavigationBoundaryAxis::Horizontal);
    CHECK(tamoePhysicalOpenings[0].boundaryIdentity.fixedSubtile == 5'091);
    CHECK(tamoePhysicalOpenings[1].boundaryIdentity.fixedSubtile == 5'091);
    CHECK(tamoePhysicalOpenings[0].boundaryIdentity.startSubtile == 15'040);
    CHECK(tamoePhysicalOpenings[0].boundaryIdentity.endSubtile == 15'044);
    CHECK(tamoePhysicalOpenings[1].boundaryIdentity.startSubtile == 15'142);
    CHECK(tamoePhysicalOpenings[1].boundaryIdentity.endSubtile == 15'182);
    CHECK(tamoePhysicalOpenings[0].boundaryIdentity
        != tamoePhysicalOpenings[1].boundaryIdentity);

    std::array partiallyIdentifiedSpan{
        Detail::NavigationBoundarySpan{
            .targetLevelId = 7,
            .side = Detail::NavigationBoundarySide::Right,
            .startSubtile = 1'040,
            .endSubtile = 1'047,
            .fixedSubtile = 650,
            .sourceRoomIdentity = 0x1000U,
        },
    };
    std::size_t invalidMergedCount{};
    CHECK(!Detail::MergeOutdoorBoundarySpans(
        partiallyIdentifiedSpan,
        partiallyIdentifiedSpan.size(),
        invalidMergedCount));

    // The already merged source frontier is cacheable across targets; only
    // the independently collected destination spans carry the target id.
    const std::array secondTargetBoundary{
        Detail::NavigationBoundarySpan{
            27, Detail::NavigationBoundarySide::Right, 1'041, 1'045},
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        27,
        std::span(fragmentedSourceSpans).first(mergedSourceSpanCount),
        secondTargetBoundary,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Found);
    CHECK(opening.subtileX == 799);
    CHECK(opening.subtileY == 1'043);
    CHECK(opening.spanSubtiles == 4);

    // A broad destination-side seam is harmless when the complete current
    // level boundary exposes only the narrow, real road opening.
    const std::array narrowOuterBoundary{
        Detail::NavigationBoundarySpan{
            3, Detail::NavigationBoundarySide::Right, 1'100, 1'104},
    };
    const std::array broadDestinationBoundary{
        Detail::NavigationBoundarySpan{
            26, Detail::NavigationBoundarySide::Right, 1'060, 1'140},
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        26,
        narrowOuterBoundary,
        broadDestinationBoundary,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Found);
    CHECK(opening.subtileX == 799);
    CHECK(opening.subtileY == 1'102);
    CHECK(opening.spanSubtiles == 4);

    // If two disconnected openings survive full-level validation, neither
    // the widest nor the lowest coordinate may be guessed.
    const std::array ambiguousSourceBoundary{
        Detail::NavigationBoundarySpan{
            3, Detail::NavigationBoundarySide::Left, 1'010, 1'050},
        Detail::NavigationBoundarySpan{
            3, Detail::NavigationBoundarySide::Right, 1'100, 1'104},
    };
    const std::array ambiguousTargetBoundary{
        Detail::NavigationBoundarySpan{
            26, Detail::NavigationBoundarySide::Left, 1'010, 1'050},
        Detail::NavigationBoundarySpan{
            26, Detail::NavigationBoundarySide::Right, 1'100, 1'104},
    };
    CHECK(Detail::FindUniqueOutdoorLevelBoundaryOpening(
        levelBounds,
        26,
        ambiguousSourceBoundary,
        ambiguousTargetBoundary,
        opening) == Detail::NavigationOutdoorBoundaryMatchResult::Ambiguous);

    std::array<Detail::NavigationExitSelection, 3U> selections{};
    std::size_t selectionCount{};
    const auto makeSelection = [](
            std::uint64_t destinationId,
            std::int32_t targetLevelId,
            std::int32_t subtileX,
            std::int32_t subtileY,
            Detail::NavigationExitEvidence evidence,
            std::int32_t span,
            NavigationExitBoundaryIdentity boundaryIdentity = {},
            bool canonicalLevelPairAnchor = false) noexcept {
        return Detail::NavigationExitSelection{
            .candidate = NavigationExitCandidate{
                .destinationId = destinationId,
                .targetLevelId = targetLevelId,
                .subtileX = subtileX,
                .subtileY = subtileY,
            },
            .evidence = evidence,
            .spanSubtiles = span,
            .boundaryIdentity = boundaryIdentity,
            .canonicalLevelPairAnchor = canonicalLevelPairAnchor,
        };
    };
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            10U,
            4,
            100,
            200,
            Detail::NavigationExitEvidence::OutdoorCollision,
            10)));
    CHECK(selectionCount == 1U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            11U,
            4,
            90,
            190,
            Detail::NavigationExitEvidence::OutdoorCollision,
            8)));
    CHECK(selections[0].candidate.destinationId == 10U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            12U,
            4,
            120,
            220,
            Detail::NavigationExitEvidence::OutdoorCollision,
            20)));
    CHECK(selections[0].candidate.destinationId == 12U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            19U,
            4,
            110,
            210,
            Detail::NavigationExitEvidence::OutdoorCollision,
            21)));
    CHECK(selections[0].candidate.destinationId == 19U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            13U,
            4,
            300,
            400,
            Detail::NavigationExitEvidence::RoomTile,
            0)));
    CHECK(selections[0].candidate.destinationId == 13U);
    CHECK(selections[0].evidence
        == Detail::NavigationExitEvidence::RoomTile);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            21U,
            4,
            305,
            405,
            Detail::NavigationExitEvidence::QuestPreset,
            0)));
    CHECK(selections[0].candidate.destinationId == 21U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            22U,
            4,
            307,
            407,
            Detail::NavigationExitEvidence::BossPreset,
            0)));
    CHECK(selections[0].candidate.destinationId == 22U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            20U,
            4,
            310,
            410,
            Detail::NavigationExitEvidence::RuntimeObject,
            0)));
    CHECK(selections[0].candidate.destinationId == 20U);
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            14U,
            4,
            50,
            60,
            Detail::NavigationExitEvidence::OutdoorCollision,
            100)));
    CHECK(selections[0].candidate.destinationId == 20U);
    CHECK(!Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            15U,
            3,
            50,
            60,
            Detail::NavigationExitEvidence::RoomTile,
            0)));
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            16U,
            5,
            500,
            600,
            Detail::NavigationExitEvidence::OutdoorCollision,
            10)));
    CHECK(Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            17U,
            6,
            700,
            800,
            Detail::NavigationExitEvidence::OutdoorCollision,
            10)));
    CHECK(selectionCount == selections.size());
    CHECK(!Detail::UpsertExitSelection(
        3,
        selections,
        selectionCount,
        makeSelection(
            18U,
            7,
            900,
            1'000,
            Detail::NavigationExitEvidence::OutdoorCollision,
            10)));

    std::array<Detail::NavigationExitSelection, 8> physicalEvidence{};
    std::size_t physicalEvidenceCount{};
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        physicalEvidence,
        physicalEvidenceCount,
        makeSelection(
            30U,
            4,
            100,
            200,
            Detail::NavigationExitEvidence::OutdoorCollision,
            8)));
    CHECK(physicalEvidenceCount == 1U);
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        physicalEvidence,
        physicalEvidenceCount,
        makeSelection(
            31U,
            4,
            108,
            207,
            Detail::NavigationExitEvidence::RoomTile,
            0)));
    CHECK(physicalEvidenceCount == 2U);
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        physicalEvidence,
        physicalEvidenceCount,
        makeSelection(
            32U,
            4,
            140,
            200,
            Detail::NavigationExitEvidence::OutdoorCollision,
            8)));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        physicalEvidence,
        physicalEvidenceCount,
        makeSelection(
            33U,
            5,
            100,
            200,
            Detail::NavigationExitEvidence::RoomTile,
            0)));
    CHECK(!Detail::AppendPhysicalExitEvidence(
        3,
        physicalEvidence,
        physicalEvidenceCount,
        makeSelection(
            34U,
            3,
            500,
            600,
            Detail::NavigationExitEvidence::RoomTile,
            0)));

    std::array<Detail::NavigationPhysicalExitSelection, 8> physicalLabels{};
    std::size_t physicalLabelCount{};
    CHECK(Detail::ReducePhysicalExitEvidence(
        physicalEvidence,
        physicalEvidenceCount,
        physicalLabels,
        physicalLabelCount));
    CHECK(physicalLabelCount == 3U);
    CHECK(physicalLabels[0].winner.candidate.destinationId == 31U);

    auto samePointA = makeSelection(
        80U, 8, 400, 500,
        Detail::NavigationExitEvidence::RoomTile, 0);
    auto samePointB = samePointA;
    samePointB.candidate.destinationId = 79U;
    std::array<Detail::NavigationExitSelection, 2> samePointForward{};
    std::array<Detail::NavigationExitSelection, 2> samePointReverse{};
    std::size_t samePointForwardCount{};
    std::size_t samePointReverseCount{};
    CHECK(Detail::AppendPhysicalExitEvidence(
        3, samePointForward, samePointForwardCount, samePointA));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3, samePointForward, samePointForwardCount, samePointB));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3, samePointReverse, samePointReverseCount, samePointB));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3, samePointReverse, samePointReverseCount, samePointA));
    CHECK(samePointForwardCount == 1U);
    CHECK(samePointReverseCount == 1U);
    CHECK(samePointForward[0].candidate.destinationId == 79U);
    CHECK(samePointReverse[0].candidate.destinationId == 79U);

    const NavigationExitBoundaryIdentity exactPointBoundary{
        .axis = NavigationBoundaryAxis::Horizontal,
        .fixedSubtile = 500,
        .startSubtile = 390,
        .endSubtile = 410,
    };
    const auto exactPointOutdoor = makeSelection(
        81U, 8, 400, 500,
        Detail::NavigationExitEvidence::OutdoorCollision, 20,
        exactPointBoundary);
    const auto exactPointRoomTile = makeSelection(
        82U, 8, 400, 500,
        Detail::NavigationExitEvidence::RoomTile, 0);
    std::array<Detail::NavigationExitSelection, 2>
        exactPointIdentityForward{};
    std::array<Detail::NavigationExitSelection, 2>
        exactPointIdentityReverse{};
    std::size_t exactPointIdentityForwardCount{};
    std::size_t exactPointIdentityReverseCount{};
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        exactPointIdentityForward,
        exactPointIdentityForwardCount,
        exactPointOutdoor));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        exactPointIdentityForward,
        exactPointIdentityForwardCount,
        exactPointRoomTile));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        exactPointIdentityReverse,
        exactPointIdentityReverseCount,
        exactPointRoomTile));
    CHECK(Detail::AppendPhysicalExitEvidence(
        3,
        exactPointIdentityReverse,
        exactPointIdentityReverseCount,
        exactPointOutdoor));
    CHECK(exactPointIdentityForwardCount == 1U);
    CHECK(exactPointIdentityReverseCount == 1U);
    CHECK(exactPointIdentityForward[0].candidate.destinationId == 82U);
    CHECK(exactPointIdentityReverse[0].candidate.destinationId == 82U);
    CHECK(exactPointIdentityForward[0].boundaryIdentity
        == exactPointBoundary);
    CHECK(exactPointIdentityReverse[0].boundaryIdentity
        == exactPointBoundary);

    const std::array chain{
        makeSelection(
            40U, 4, 100, 100,
            Detail::NavigationExitEvidence::OutdoorCollision, 0),
        makeSelection(
            41U, 4, 108, 100,
            Detail::NavigationExitEvidence::RuntimeObject, 0),
        makeSelection(
            42U, 4, 116, 100,
            Detail::NavigationExitEvidence::RoomTile, 0),
    };
    auto forwardChain = chain;
    auto reverseChain = std::array{
        chain[2], chain[1], chain[0],
    };
    std::array<Detail::NavigationPhysicalExitSelection, 3> forwardClusters{};
    std::array<Detail::NavigationPhysicalExitSelection, 3> reverseClusters{};
    std::size_t forwardCount{};
    std::size_t reverseCount{};
    CHECK(Detail::ReducePhysicalExitEvidence(
        forwardChain,
        forwardChain.size(),
        forwardClusters,
        forwardCount));
    CHECK(Detail::ReducePhysicalExitEvidence(
        reverseChain,
        reverseChain.size(),
        reverseClusters,
        reverseCount));
    CHECK(forwardCount == 2U);
    CHECK(reverseCount == forwardCount);
    for (std::size_t index = 0U; index < forwardCount; ++index) {
        CHECK(forwardClusters[index].anchorSubtileX
            == reverseClusters[index].anchorSubtileX);
        CHECK(forwardClusters[index].anchorSubtileY
            == reverseClusters[index].anchorSubtileY);
        CHECK(forwardClusters[index].winner.candidate.destinationId
            == reverseClusters[index].winner.candidate.destinationId);
    }

    const NavigationExitBoundaryIdentity sharedBoundary{
        .axis = NavigationBoundaryAxis::Vertical,
        .fixedSubtile = 2'000,
        .startSubtile = 3'000,
        .endSubtile = 3'010,
    };
    auto sameBoundaryEvidence = std::array{
        makeSelection(
            90U, 9, 1'000, 1'000,
            Detail::NavigationExitEvidence::OutdoorCollision, 10,
            sharedBoundary),
        makeSelection(
            91U, 9, 1'080, 1'000,
            Detail::NavigationExitEvidence::OutdoorCollision, 10,
            sharedBoundary),
    };
    std::array<Detail::NavigationPhysicalExitSelection, 2>
        sameBoundaryClusters{};
    std::size_t sameBoundaryCount{};
    CHECK(Detail::ReducePhysicalExitEvidence(
        sameBoundaryEvidence,
        sameBoundaryEvidence.size(),
        sameBoundaryClusters,
        sameBoundaryCount));
    CHECK(sameBoundaryCount == 1U);
    CHECK(sameBoundaryClusters[0].boundaryIdentity == sharedBoundary);

    const NavigationExitBoundaryIdentity nearbyDistinctBoundary{
        .axis = NavigationBoundaryAxis::Vertical,
        .fixedSubtile = 2'001,
        .startSubtile = 3'000,
        .endSubtile = 3'010,
    };
    auto distinctBoundaryEvidence = std::array{
        makeSelection(
            92U, 9, 1'000, 1'000,
            Detail::NavigationExitEvidence::OutdoorCollision, 10,
            sharedBoundary),
        makeSelection(
            93U, 9, 1'002, 1'002,
            Detail::NavigationExitEvidence::OutdoorCollision, 10,
            nearbyDistinctBoundary),
    };
    std::array<Detail::NavigationPhysicalExitSelection, 2>
        distinctBoundaryClusters{};
    std::size_t distinctBoundaryCount{};
    CHECK(Detail::ReducePhysicalExitEvidence(
        distinctBoundaryEvidence,
        distinctBoundaryEvidence.size(),
        distinctBoundaryClusters,
        distinctBoundaryCount));
    CHECK(distinctBoundaryCount == 2U);

    // Stronger non-structural evidence may choose the displayed coordinate,
    // but it must not erase the proven seam used to merge the reciprocal side.
    auto retainedBoundaryEvidence = std::array{
        makeSelection(
            94U, 9, 1'000, 1'000,
            Detail::NavigationExitEvidence::OutdoorCollision, 10,
            sharedBoundary),
        makeSelection(
            95U, 9, 1'003, 1'003,
            Detail::NavigationExitEvidence::RoomTile, 0),
    };
    std::array<Detail::NavigationPhysicalExitSelection, 2>
        retainedBoundaryClusters{};
    std::size_t retainedBoundaryCount{};
    CHECK(Detail::ReducePhysicalExitEvidence(
        retainedBoundaryEvidence,
        retainedBoundaryEvidence.size(),
        retainedBoundaryClusters,
        retainedBoundaryCount));
    CHECK(retainedBoundaryCount == 1U);
    CHECK(retainedBoundaryClusters[0].winner.candidate.destinationId == 95U);
    CHECK(retainedBoundaryClusters[0].boundaryIdentity == sharedBoundary);

    const AutomapExitLabelDefinition forwardDefinition{
        .sourceLevelId = 7,
        .targetLevelId = 26,
        .subtileX = 1'000,
        .subtileY = 1'000,
        .boundaryIdentity = sharedBoundary,
    };
    const AutomapExitLabelDefinition reverseDefinition{
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .boundaryIdentity = sharedBoundary,
    };
    const AutomapExitLabelDefinition separateDefinition{
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .boundaryIdentity = nearbyDistinctBoundary,
    };
    CHECK(SameAutomapExitLevelPair(forwardDefinition, reverseDefinition));
    CHECK(SameAutomapExitPhysicalBoundary(
        forwardDefinition,
        reverseDefinition));
    CHECK(IsAutomapExitPhysicalGroupMember(
        forwardDefinition,
        reverseDefinition,
        false));
    CHECK(!SameAutomapExitPhysicalBoundary(
        forwardDefinition,
        separateDefinition));
    CHECK(!IsAutomapExitPhysicalGroupMember(
        forwardDefinition,
        separateDefinition,
        false));

    const AutomapExitLabelDefinition externalOrdinaryExit{
        .sourceLevelId = 112,
        .targetLevelId = 113,
        .subtileX = 2'800,
        .subtileY = 4'700,
    };
    const AutomapExitLabelDefinition nativeOrdinaryExit{
        .sourceLevelId = 112,
        .targetLevelId = 113,
        .subtileX = 2'804,
        .subtileY = 4'704,
    };
    const AutomapExitLabelDefinition externalPermanentPortal{
        .sourceLevelId = 112,
        .targetLevelId = 126,
        .subtileX = 2'787,
        .subtileY = 4'705,
    };
    const std::array nativeOwnerDefinitions{nativeOrdinaryExit};
    CHECK(NativeAutomapExitOverridesExternal(
        externalOrdinaryExit,
        nativeOrdinaryExit));
    CHECK(!NativeAutomapExitOverridesExternal(
        externalPermanentPortal,
        nativeOrdinaryExit));
    CHECK(!ShouldRetainExternalAutomapExit(
        externalOrdinaryExit,
        nativeOwnerDefinitions,
        true));
    CHECK(ShouldRetainExternalAutomapExit(
        externalPermanentPortal,
        nativeOwnerDefinitions,
        true));
    CHECK(ShouldRetainExternalAutomapExit(
        externalOrdinaryExit,
        nativeOwnerDefinitions,
        false));

    const AutomapExitLabelDefinition reverseCanonicalMonastery{
        .stableId = 210U,
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .subtileX = 1'100,
        .subtileY = 1'200,
        .canonicalLevelPairAnchor = true,
    };
    const AutomapExitLabelDefinition reverseMonasteryFragment{
        .stableId = 211U,
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .subtileX = 1'240,
        .subtileY = 1'310,
        .boundaryIdentity = nearbyDistinctBoundary,
    };
    CHECK(SameAutomapExitOwnerFragment(
        reverseCanonicalMonastery,
        reverseMonasteryFragment));
    CHECK(PreferAutomapExitOwnerFragment(
        reverseCanonicalMonastery,
        reverseMonasteryFragment));
    const AutomapExitLabelDefinition separateForwardFragment{
        .stableId = 212U,
        .sourceLevelId = 7,
        .targetLevelId = 26,
        .subtileX = 1'008,
        .subtileY = 1'000,
        .boundaryIdentity = nearbyDistinctBoundary,
    };
    CHECK(!SameAutomapExitOwnerFragment(
        forwardDefinition,
        separateForwardFragment));

    const std::array canonicalPairDefinitions{
        forwardDefinition,
        reverseDefinition,
        separateDefinition,
        AutomapExitLabelDefinition{
            .sourceLevelId = 7,
            .targetLevelId = 26,
            .canonicalLevelPairAnchor = true,
        },
        AutomapExitLabelDefinition{
            .sourceLevelId = 26,
            .targetLevelId = 27,
        },
    };
    CHECK(HasCanonicalAutomapExitLevelPair(
        canonicalPairDefinitions,
        forwardDefinition));
    std::size_t canonicalGroupCount{};
    for (const auto& definition : canonicalPairDefinitions) {
        if (IsAutomapExitPhysicalGroupMember(
                forwardDefinition,
                definition,
                true)) {
            ++canonicalGroupCount;
        }
    }
    CHECK(canonicalGroupCount == 4U);

    const AutomapExitLabelDefinition mixedReverseWithoutIdentity{
        .stableId = 300U,
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .subtileX = 1'001,
        .subtileY = 1'000,
    };
    const auto mixedIdentityFirst = std::array{
        forwardDefinition,
        mixedReverseWithoutIdentity,
    };
    const auto mixedIdentityLast = std::array{
        mixedReverseWithoutIdentity,
        forwardDefinition,
    };
    CHECK(ResolvedAutomapExitBoundaryIdentity(
        mixedIdentityFirst,
        mixedReverseWithoutIdentity) == sharedBoundary);
    CHECK(ResolvedAutomapExitBoundaryIdentity(
        mixedIdentityLast,
        mixedReverseWithoutIdentity) == sharedBoundary);
    CHECK(IsResolvedAutomapExitPhysicalGroupMember(
        mixedIdentityFirst,
        forwardDefinition,
        mixedReverseWithoutIdentity,
        false));
    CHECK(IsResolvedAutomapExitPhysicalGroupMember(
        mixedIdentityFirst,
        mixedReverseWithoutIdentity,
        forwardDefinition,
        false));
    CHECK(IsResolvedAutomapExitPhysicalGroupMember(
        mixedIdentityLast,
        forwardDefinition,
        mixedReverseWithoutIdentity,
        false));

    const AutomapExitLabelDefinition secondValidForward{
        .stableId = 301U,
        .sourceLevelId = 7,
        .targetLevelId = 26,
        .subtileX = 1'008,
        .subtileY = 1'000,
        .boundaryIdentity = nearbyDistinctBoundary,
    };
    const AutomapExitLabelDefinition reverseNearFirst{
        .stableId = 302U,
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .subtileX = 1'002,
        .subtileY = 1'000,
    };
    const auto mixedDistinctForward = std::array{
        forwardDefinition,
        secondValidForward,
        reverseNearFirst,
    };
    const auto mixedDistinctReverse = std::array{
        reverseNearFirst,
        secondValidForward,
        forwardDefinition,
    };
    CHECK(ResolvedAutomapExitBoundaryIdentity(
        mixedDistinctForward,
        reverseNearFirst) == sharedBoundary);
    CHECK(ResolvedAutomapExitBoundaryIdentity(
        mixedDistinctReverse,
        reverseNearFirst) == sharedBoundary);
    CHECK(IsResolvedAutomapExitPhysicalGroupMember(
        mixedDistinctForward,
        forwardDefinition,
        reverseNearFirst,
        false));
    CHECK(!IsResolvedAutomapExitPhysicalGroupMember(
        mixedDistinctForward,
        secondValidForward,
        reverseNearFirst,
        false));
    CHECK(!IsResolvedAutomapExitPhysicalGroupMember(
        mixedDistinctReverse,
        forwardDefinition,
        secondValidForward,
        false));

    const AutomapExitLabelDefinition reverseEquidistant{
        .stableId = 303U,
        .sourceLevelId = 26,
        .targetLevelId = 7,
        .subtileX = 1'004,
        .subtileY = 1'000,
    };
    const auto mixedTieForward = std::array{
        forwardDefinition,
        secondValidForward,
        reverseEquidistant,
    };
    const auto mixedTieReverse = std::array{
        reverseEquidistant,
        secondValidForward,
        forwardDefinition,
    };
    CHECK(ResolvedAutomapExitBoundaryIdentity(
        mixedTieForward,
        reverseEquidistant) == sharedBoundary);
    CHECK(ResolvedAutomapExitBoundaryIdentity(
        mixedTieReverse,
        reverseEquidistant) == sharedBoundary);
}

void CheckNavigationLevelCatalogContract() {
    using namespace RuffnecKk::MapSense;

    CHECK(ResolveCanonicalLevelName("Pit Level 1").value_or(-1) == 12);
    CHECK(ResolveCanonicalLevelName("Underground Passage Level 2")
        .value_or(-1) == 14);
    CHECK(ResolveCanonicalLevelName("mausoleum").value_or(-1) == 19);
    CHECK(ResolveCanonicalLevelName("ANCIENT TUNNELS").value_or(-1) == 65);
    CHECK(ResolveCanonicalLevelName("Icy Cellar").value_or(-1) == 119);
    CHECK(!ResolveCanonicalLevelName("Tal Rasha's Tomb").has_value());
    CHECK(!ResolveCanonicalLevelName("Sewers Level 1").has_value());
    CHECK(!ResolveCanonicalLevelName("Tristram").has_value());
    CHECK(!ResolveCanonicalLevelName("Not A D2R Level").has_value());
}

struct NativeLayoutAudit {
    std::set<std::string> names;
    std::set<std::string> messages;
    std::vector<std::array<int, 4>> actionRects;
    int buttonCount{};
    int clickCatcherCount{};
    int toggleCount{};
    int tabBarCount{};
    int scrollViewCount{};
    int scrollControllerCount{};
    int tableCount{};
    bool namesUnique{true};
    bool textRectsExplicit{true};
};

void AuditNativeLayoutNode(
        const nlohmann::json& node,
        NativeLayoutAudit& audit) {
    if (!node.is_object()) return;
    const auto type = node.value("type", std::string{});
    const auto name = node.value("name", std::string{});
    if (name.empty() || !audit.names.insert(name).second) {
        audit.namesUnique = false;
    }

    if (type == "ButtonWidget") ++audit.buttonCount;
    if (type == "ClickCatcherWidget") ++audit.clickCatcherCount;
    if (type == "ToggleButtonWidget") ++audit.toggleCount;
    if (type == "TabBarWidget") ++audit.tabBarCount;
    if (type == "ScrollViewWidget") ++audit.scrollViewCount;
    if (type == "ScrollControllerWidget") ++audit.scrollControllerCount;
    if (type == "TableWidget") ++audit.tableCount;

    if (node.contains("fields") && node["fields"].is_object()) {
        const auto& fields = node["fields"];
        if (fields.contains("onClickMessage")
            && fields["onClickMessage"].is_string()) {
            audit.messages.insert(
                fields["onClickMessage"].get<std::string>());
        }
        if (type == "TextBoxWidget") {
            audit.textRectsExplicit = audit.textRectsExplicit
                && fields.contains("rect")
                && fields["rect"].is_object()
                && fields["rect"].value("width", 0) > 0
                && fields["rect"].value("height", 0) > 0;
        }
        if (type == "ButtonWidget"
            && name != "CloseButton"
            && fields.contains("rect")
            && fields["rect"].is_object()) {
            const auto& rect = fields["rect"];
            audit.actionRects.push_back({
                rect.value("x", 0),
                rect.value("y", 0),
                527,
                117,
            });
        }
    }

    if (!node.contains("children") || !node["children"].is_array()) return;
    for (const auto& child : node["children"]) {
        AuditNativeLayoutNode(child, audit);
    }
}

auto RectsOverlap(
        const std::array<int, 4>& left,
        const std::array<int, 4>& right) -> bool {
    return left[0] < right[0] + right[2]
        && left[0] + left[2] > right[0]
        && left[1] < right[1] + right[3]
        && left[1] + left[3] > right[1];
}

void CheckAutomapWaypointCatalogContract() {
    using RuffnecKk::MapSense::AutomapWaypointLabelDefinition;
    using RuffnecKk::MapSense::Detail::LayeredAutomapWaypointDefinitionCatalog;

    const AutomapWaypointLabelDefinition levelOne{
        .stableId = 101U,
        .levelId = 1,
        .subtileX = 110,
        .subtileY = 120,
    };
    const AutomapWaypointLabelDefinition levelTwo{
        .stableId = 202U,
        .levelId = 2,
        .subtileX = 210,
        .subtileY = 220,
    };
    const AutomapWaypointLabelDefinition levelOneReplacement{
        .stableId = 103U,
        .levelId = 1,
        .subtileX = 130,
        .subtileY = 140,
    };
    const AutomapWaypointLabelDefinition overflowOwner{
        .stableId = 303U,
        .levelId = 3,
        .subtileX = 310,
        .subtileY = 320,
    };
    const AutomapWaypointLabelDefinition nextSeedLevelTwo{
        .stableId = 204U,
        .levelId = 2,
        .subtileX = 410,
        .subtileY = 420,
    };
    const auto one = [](const AutomapWaypointLabelDefinition& definition) {
        return std::span<const AutomapWaypointLabelDefinition>{&definition, 1U};
    };

    LayeredAutomapWaypointDefinitionCatalog<2U> catalog;
    const std::array externalBaseline{levelOne, levelTwo};
    CHECK(catalog.ReplaceExternal(externalBaseline));
    CHECK(catalog.Definitions().size() == 2U);

    CHECK(catalog.PublishNativeOwner(1, one(levelOneReplacement)));
    CHECK(catalog.Definitions().size() == 2U);
    CHECK(catalog.Definitions()[0].levelId == 2);
    CHECK(catalog.Definitions()[1].stableId == levelOneReplacement.stableId);
    CHECK(catalog.Definitions()[1].subtileX == 130);

    const std::array beforeOverflow{
        catalog.Definitions()[0],
        catalog.Definitions()[1],
    };
    CHECK(!catalog.PublishNativeOwner(3, one(overflowOwner)));
    CHECK(catalog.Definitions().size() == beforeOverflow.size());
    CHECK(catalog.Definitions()[0].stableId == beforeOverflow[0].stableId);
    CHECK(catalog.Definitions()[0].subtileX == beforeOverflow[0].subtileX);
    CHECK(catalog.Definitions()[1].stableId == beforeOverflow[1].stableId);
    CHECK(catalog.Definitions()[1].subtileX == beforeOverflow[1].subtileX);

    // Pending resolution performs no replacement and therefore preserves the
    // exact owner definition already proven by an earlier settlement pass.
    CHECK(catalog.Definitions()[1].stableId == levelOneReplacement.stableId);

    // An empty native observation is negative evidence only. It must not erase
    // either the native positive or the external baseline for that owner.
    CHECK(catalog.PublishNativeOwner(
        1,
        std::span<const AutomapWaypointLabelDefinition>{}));
    CHECK(catalog.Definitions().size() == 2U);
    CHECK(catalog.Definitions()[1].stableId
        == levelOneReplacement.stableId);

    // A refreshed external atlas does not erase positive native evidence, and
    // owners without native evidence move atomically to the new seed.
    const std::array nextExternal{levelOne, nextSeedLevelTwo};
    CHECK(catalog.ReplaceExternal(nextExternal));
    CHECK(catalog.Definitions().size() == 2U);
    CHECK(catalog.Definitions()[0].stableId == nextSeedLevelTwo.stableId);
    CHECK(catalog.Definitions()[1].stableId
        == levelOneReplacement.stableId);

    // Session reset drops all old-seed coordinates. Replay may republish the
    // same owner only from the newly materialized DRLG coordinates.
    catalog.Clear();
    CHECK(catalog.Definitions().empty());
    CHECK(catalog.ReplaceExternal(one(nextSeedLevelTwo)));
    CHECK(catalog.Definitions().size() == 1U);
    CHECK(catalog.Definitions()[0].stableId == nextSeedLevelTwo.stableId);
    CHECK(catalog.Definitions()[0].subtileX == 410);
    CHECK(catalog.Definitions()[0].subtileY == 420);
}

void CheckAutomapLevelCatalogContract() {
    using RuffnecKk::MapSense::AutomapLevelLabelDefinition;
    using RuffnecKk::MapSense::Detail::LayeredAutomapLevelDefinitionCatalog;

    const AutomapLevelLabelDefinition first{
        .stableId = 701U,
        .levelId = 79,
        .subtileX = 110,
        .subtileY = 120,
    };
    const AutomapLevelLabelDefinition replacement{
        .stableId = 702U,
        .levelId = 79,
        .subtileX = 130,
        .subtileY = 140,
    };
    const AutomapLevelLabelDefinition second{
        .stableId = 801U,
        .levelId = 80,
        .subtileX = 210,
        .subtileY = 220,
    };
    const auto one = [](const AutomapLevelLabelDefinition& definition) {
        return std::span<const AutomapLevelLabelDefinition>{&definition, 1U};
    };

    LayeredAutomapLevelDefinitionCatalog<2U> catalog;
    const std::array externalBaseline{first, second};
    CHECK(catalog.ReplaceExternal(externalBaseline));
    CHECK(catalog.Definitions().size() == 2U);
    CHECK(catalog.PublishNativeOwner(79, one(replacement)));
    CHECK(catalog.Definitions().size() == 2U);
    CHECK(catalog.Definitions()[0].levelId == 80);
    CHECK(catalog.Definitions()[1].stableId == replacement.stableId);
    CHECK(catalog.Definitions()[1].subtileX == 130);
    CHECK(catalog.PublishNativeOwner(
        79,
        std::span<const AutomapLevelLabelDefinition>{}));
    CHECK(catalog.Definitions().size() == 2U);
    CHECK(catalog.Definitions()[1].stableId == replacement.stableId);
    catalog.Clear();
    CHECK(catalog.Definitions().empty());
}

void CheckTownWaypointLabelPolicy() {
    using RuffnecKk::MapSense::Detail::AllowsWaypointLabelForLevel;

    CHECK(!AllowsWaypointLabelForLevel(1));
    CHECK(!AllowsWaypointLabelForLevel(40));
    CHECK(!AllowsWaypointLabelForLevel(75));
    CHECK(!AllowsWaypointLabelForLevel(103));
    CHECK(!AllowsWaypointLabelForLevel(109));

    // Adjacent outdoor/dungeon levels retain their labels. This also guards
    // against replacing the exact owner policy with a broad act or id range.
    CHECK(AllowsWaypointLabelForLevel(2));
    CHECK(AllowsWaypointLabelForLevel(39));
    CHECK(AllowsWaypointLabelForLevel(41));
    CHECK(AllowsWaypointLabelForLevel(74));
    CHECK(AllowsWaypointLabelForLevel(76));
    CHECK(AllowsWaypointLabelForLevel(102));
    CHECK(AllowsWaypointLabelForLevel(104));
    CHECK(AllowsWaypointLabelForLevel(108));
    CHECK(AllowsWaypointLabelForLevel(110));
}

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::MapSense;

    CHECK(UiLocalizationCatalogIsComplete());
    CHECK(DetectUiLanguageFromFingerprint("Defense: %d")
        == UiLanguage::English);
    CHECK(DetectUiLanguageFromFingerprint("防禦：%d")
        == UiLanguage::TraditionalChinese);
    CHECK(DetectUiLanguageFromFingerprint("Verteidigung: %d")
        == UiLanguage::German);
    CHECK(DetectUiLanguageFromFingerprint("Defensa: %d")
        == UiLanguage::Spanish);
    CHECK(DetectUiLanguageFromFingerprint("Défense : %d")
        == UiLanguage::French);
    CHECK(DetectUiLanguageFromFingerprint("Difesa: %d")
        == UiLanguage::Italian);
    CHECK(DetectUiLanguageFromFingerprint("방어력: %d")
        == UiLanguage::Korean);
    CHECK(DetectUiLanguageFromFingerprint("Obrona: %d")
        == UiLanguage::Polish);
    CHECK(DetectUiLanguageFromFingerprint("防御力: %d")
        == UiLanguage::Japanese);
    CHECK(DetectUiLanguageFromFingerprint("Defesa: %d")
        == UiLanguage::BrazilianPortuguese);
    CHECK(DetectUiLanguageFromFingerprint("Защита: %d")
        == UiLanguage::Russian);
    CHECK(DetectUiLanguageFromFingerprint("防御: %d")
        == UiLanguage::SimplifiedChinese);
    CHECK(std::string_view(UiText(
        UiTextId::RevealMap,
        UiLanguage::French)) == "Révéler la carte");
    CHECK(std::string_view(UiText(
        UiTextId::CustomTomlHint,
        UiLanguage::English))
        == "(to add more custom destinations, configure in TOML)");
    {
        const auto localizationContext = MakeCatalogContext(nullptr);
        ResetUiLanguage();
        CHECK(RefreshUiLanguage(&localizationContext));
        CHECK(CurrentUiLanguage() == UiLanguage::French);
        CHECK(std::string_view(UiText(UiTextId::Open)) == "Ouvrir");
        ResetUiLanguage();
    }

    CheckNavigationProjectionDiagnosticCacheContract();
    CheckMapSenseDataCatalogContract();
    CheckRevealPersistenceContract();
    CheckRevealReplayRequestPolicy();
    CheckProgressiveRevealGraphContract();
    CheckExternalAtlasTopologyContract();
    CheckExternalLabelProviderCoordinatorPolicy();
    CheckExternalAtlasGeometryContract();
    CheckAtlasProjectionContract();
    CheckNativeAutomapAtlasPolicy();
    if (argc >= 3) CheckAutomapSpritePackageContract(argv[2]);
    CheckExternalAtlasCacheContract();
    CheckStaticPoiRoomSelectionContract();
    CheckNativeUiPanelPolicy();
    CheckNavigationEngineContract();
    CheckNavigationLevelCatalogContract();
    CheckNavigationPolicyContract();
    CheckNavigationResolverHelpers();
    CheckAutomapWaypointCatalogContract();
    CheckAutomapLevelCatalogContract();
    CheckTownWaypointLabelPolicy();

    static_assert(CurrentConfigSchemaVersion == 16);
    static_assert(MenuThemes.size() == 10U);
    static_assert(DeriveDrlgStartSeed(1U) == 1'791'398'751U);
    static_assert(DeriveDrlgStartSeed(1'337U) == 2'802'456'439U);
    static_assert(DeriveDrlgStartSeed(0x12345678U) == 62'524'658U);
    static_assert(MaximumAutomapPoiSnapshots
        == MaximumAutomapExitLabels
            + MaximumAutomapWaypointLabels
            + MaximumAutomapLevelLabels
            + MaximumAutomapSpecialChestPresets
            + MaximumTrackedAutomapObjects * 2U);
    static_assert(OrientedAutomapExitLevel(1, 1, 2, 0U, 1U) == 2);
    static_assert(OrientedAutomapExitLevel(2, 1, 2, 1U, 0U) == 1);
    static_assert(OrientedAutomapExitLevel(2, 2, 3, 0U, 1U) == 3);
    static_assert(OrientedAutomapExitLevel(3, 2, 3, 1U, 0U) == 2);
    static_assert(OrientedAutomapExitLevel(1, 2, 3, 0xFFFFU, 0xFFFFU)
        == 3);
    static_assert(OrientedAutomapExitLevel(3, 2, 3, 0xFFFFU, 0xFFFFU)
        == 2);
    static_assert(ShouldProjectAutomapLevelLabel(80, 75, false, false));
    static_assert(!ShouldProjectAutomapLevelLabel(75, 75, false, false));
    static_assert(!ShouldProjectAutomapLevelLabel(80, 75, true, false));
    static_assert(!ShouldProjectAutomapLevelLabel(80, 75, false, true));
    static_assert(NativeAutomapLabelGap == 12.0F);
    static_assert(
        AutomapPoiCollectionBit(AutomapPoiCollection::WaypointLabels)
            == (1U << 6U));
    static_assert(AutomapLabelTopAboveIcon(
        100.0F,
        20.0F,
        NativeWaypointIconTopExtent,
        NativeWaypointLabelGap) == 54.0F);
    static_assert(AutomapLabelTopAboveIcon(
        100.0F,
        20.0F,
        NativeShrineIconTopExtent,
        NativeAutomapLabelGap) == 24.0F);
    static_assert(Detail::IsMonStatsLookupSafe(0U, 0U, 801, 802U));
    static_assert(!Detail::IsMonStatsLookupSafe(4U, 4U, 1, 802U));
    static_assert(!Detail::IsMonStatsLookupSafe(0U, 1U, 1, 802U));
    static_assert(!Detail::IsMonStatsLookupSafe(0U, 0U, -1, 802U));
    static_assert(!Detail::IsMonStatsLookupSafe(0U, 0U, 802, 802U));
    static_assert(!Detail::IsMonStatsLookupSafe(
        0U,
        0U,
        1,
        Detail::MaximumMonStatsRecordCount + 1U));
    static_assert(Detail::SquaredWorldSubtileDistance(0U, 0U, 3U, 4U)
        == 25U);
    static_assert(NativeMissileUnitType == 3U);
    static_assert(NativeClientUnitHashTypeStride == 0x400U);
    static_assert(NativeServerUnitHashTableOffsetFromClient == 0x1800U);
    static_assert(NativeMissileIdentityTableCapacity
        == MaximumNativeAutomapMissiles * 2U);
    static_assert(
        Detail::NativeClientUnitHashTableOffsetForType(
            NativeMissileUnitType) == 0xC00U);
    constexpr auto dualMissileIdentityContract = []() constexpr {
        Detail::NativeMissileIdentitySet<8U, 4U> identities{};
        if (identities.Size() != 0U) return false;
        if (identities.Insert(17U, 144, false)
                != Detail::NativeMissileIdentityResult::Inserted) {
            return false;
        }
        if (identities.Find(17U, 144)
                != Detail::NativeMissileIdentityResult::DuplicateClient) {
            return false;
        }
        if (identities.Insert(17U, 144, true)
                != Detail::NativeMissileIdentityResult::DuplicateClient) {
            return false;
        }
        if (identities.Find(17U, 145)
                != Detail::NativeMissileIdentityResult::ClassConflict) {
            return false;
        }
        if (identities.Insert(18U, 323, true)
                != Detail::NativeMissileIdentityResult::Inserted) {
            return false;
        }
        if (identities.Find(18U, 323)
                != Detail::NativeMissileIdentityResult::DuplicateServer) {
            return false;
        }
        if (identities.Insert(
                UINT32_MAX - 1U,
                (std::numeric_limits<std::int32_t>::max)(),
                true) != Detail::NativeMissileIdentityResult::Inserted) {
            return false;
        }
        if (identities.Find(
                UINT32_MAX - 1U,
                (std::numeric_limits<std::int32_t>::max)())
                != Detail::NativeMissileIdentityResult::DuplicateServer) {
            return false;
        }
        if (identities.Size() != 3U) return false;
        identities.Reset();
        if (identities.Size() != 0U
            || identities.Find(17U, 144)
                != Detail::NativeMissileIdentityResult::Missing) {
            return false;
        }
        constexpr std::array collidingIds{1U, 9U, 17U, 25U};
        for (const auto id : collidingIds) {
            if (identities.Insert(id, static_cast<std::int32_t>(id), false)
                    != Detail::NativeMissileIdentityResult::Inserted) {
                return false;
            }
        }
        return identities.Find(17U, 17)
                == Detail::NativeMissileIdentityResult::DuplicateClient
            && identities.Insert(33U, 33, false)
                == Detail::NativeMissileIdentityResult::CapacityExceeded
            && identities.Find(UINT32_MAX, 5)
                == Detail::NativeMissileIdentityResult::Invalid
            && identities.Find(6U, -1)
                == Detail::NativeMissileIdentityResult::Invalid;
    }();
    static_assert(dualMissileIdentityContract);
    static_assert(Detail::MayVisitNativeMissileUnit(0U, 0U));
    static_assert(Detail::MayVisitNativeMissileUnit(
        MaximumNativeAutomapMissiles - 1U,
        MaximumNativeMissilesPerBucket - 1U));
    static_assert(!Detail::MayVisitNativeMissileUnit(
        MaximumNativeAutomapMissiles,
        0U));
    static_assert(!Detail::MayVisitNativeMissileUnit(
        0U,
        MaximumNativeMissilesPerBucket));
    static_assert(Detail::IsNativeAutomapMissileSnapshotFresh(
        100U,
        350U,
        7U,
        7U));
    static_assert(!Detail::IsNativeAutomapMissileSnapshotFresh(
        100U,
        351U,
        7U,
        7U));
    static_assert(!Detail::IsNativeAutomapMissileSnapshotFresh(
        100U,
        99U,
        7U,
        7U));
    static_assert(!Detail::IsNativeAutomapMissileSnapshotFresh(
        100U,
        100U,
        7U,
        8U));
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(79U * 79U)
        == Detail::WorldSubtileDistanceBand::Through80);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(80U * 80U)
        == Detail::WorldSubtileDistanceBand::Through80);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(81U * 81U)
        == Detail::WorldSubtileDistanceBand::From81Through140);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(140U * 140U)
        == Detail::WorldSubtileDistanceBand::From81Through140);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(141U * 141U)
        == Detail::WorldSubtileDistanceBand::From141Through220);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(219U * 219U)
        == Detail::WorldSubtileDistanceBand::From141Through220);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(220U * 220U)
        == Detail::WorldSubtileDistanceBand::From141Through220);
    static_assert(Detail::ClassifyWorldSubtileDistanceSquared(221U * 221U)
        == Detail::WorldSubtileDistanceBand::Beyond220);
    static_assert(ShapeFor(MonsterRank::Normal) == MonsterShape::Circle);
    static_assert(ShapeFor(MonsterRank::Minion) == MonsterShape::Triangle);
    static_assert(ShapeFor(MonsterRank::Champion) == MonsterShape::Diamond);
    static_assert(ShapeFor(MonsterRank::Unique) == MonsterShape::Star);
    static_assert(ShapeFor(MonsterRank::SuperUnique) == MonsterShape::Hexagon);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x00) == MonsterRank::Normal);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x02) == MonsterRank::SuperUnique);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x04) == MonsterRank::Champion);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x10) == MonsterRank::Minion);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x08) == MonsterRank::Unique);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x0C) == MonsterRank::Champion);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x0A) == MonsterRank::SuperUnique);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x0E) == MonsterRank::SuperUnique);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x14) == MonsterRank::Champion);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x18) == MonsterRank::Unique);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x1E) == MonsterRank::SuperUnique);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x01) == MonsterRank::Normal);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x20) == MonsterRank::Normal);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x40) == MonsterRank::Normal);
    static_assert(
        Detail::ClassifyMonsterRankFlags(0x80) == MonsterRank::Normal);
    static_assert(
        MonsterMarkerRenderLayer(MonsterRank::Normal) == 0U);
    static_assert(
        MonsterMarkerRenderLayer(MonsterRank::Minion) == 1U);
    static_assert(
        MonsterMarkerRenderLayer(MonsterRank::Champion) == 2U);
    static_assert(
        MonsterMarkerRenderLayer(MonsterRank::Unique) == 3U);
    static_assert(
        MonsterMarkerRenderLayer(MonsterRank::SuperUnique) == 4U);
    static_assert(
        MonsterMarkerRenderLayer(MonsterRank::Normal, true) == 4U);
    static_assert(HasNamedBossDisplayIdentity(
        MonsterRank::SuperUnique, -1, false));
    static_assert(HasNamedBossDisplayIdentity(
        MonsterRank::Normal, 7, false));
    static_assert(HasNamedBossDisplayIdentity(
        MonsterRank::Normal, -1, true));
    static_assert(!HasNamedBossDisplayIdentity(
        MonsterRank::Normal, -1, false));
    static_assert(Detail::IsEnemyMarkerUnitEligible(0U));
    static_assert(!Detail::IsEnemyMarkerUnitEligible(
        Detail::UnitIsMercenaryFlag));
    static_assert(!Detail::IsEnemyMarkerUnitEligible(
        Detail::UnitIsAsyncFlag));
    static_assert(!Detail::IsEnemyMarkerUnitEligible(
        Detail::UnitIsMercenaryFlag | Detail::UnitIsAsyncFlag));
    static_assert(Detail::IsEnemyMarkerClassEligible(
        Detail::MonStatsKillableFlag));
    static_assert(!Detail::IsEnemyMarkerClassEligible(0U));
    static_assert(!Detail::IsEnemyMarkerClassEligible(
        Detail::MonStatsKillableFlag | Detail::MonStatsNpcFlag));
    static_assert(!Detail::IsEnemyMarkerClassEligible(
        Detail::MonStatsKillableFlag | Detail::MonStatsInteractFlag));
    static_assert(!Detail::IsEnemyMarkerClassEligible(
        Detail::MonStatsKillableFlag | Detail::MonStatsInTownFlag));
    static_assert(Detail::IsEnemyMarkerAlignmentEligible(
        Detail::EvilAlignment));
    static_assert(!Detail::IsEnemyMarkerAlignmentEligible(1));
    static_assert(!Detail::IsEnemyMarkerAlignmentEligible(2));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{99, 99, 99, 99, 99, 99}) == 0U);
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{100, 99, 99, 99, 99, 99})
        == ImmunityBit(MonsterImmunity::Physical));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{99, 100, 99, 99, 99, 99})
        == ImmunityBit(MonsterImmunity::Fire));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{99, 99, 100, 99, 99, 99})
        == ImmunityBit(MonsterImmunity::Cold));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{99, 99, 99, 100, 99, 99})
        == ImmunityBit(MonsterImmunity::Lightning));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{99, 99, 99, 99, 100, 99})
        == ImmunityBit(MonsterImmunity::Poison));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{99, 99, 99, 99, 99, 100})
        == ImmunityBit(MonsterImmunity::Magic));
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{100, 101, 102, 103, 104, 105})
        == 0x3FU);
    static_assert(Detail::BuildMonsterImmunityMask(
        std::array<std::int32_t, 6>{100, 99, 99, 100, 99, 100})
        == static_cast<std::uint8_t>(
            ImmunityBit(MonsterImmunity::Physical)
            | ImmunityBit(MonsterImmunity::Lightning)
            | ImmunityBit(MonsterImmunity::Magic)));
    static_assert(ColorFor(Element::Fire) == ScenePalette::Fire);
    static_assert(ColorFor(Element::Cold) == ScenePalette::Cold);
    static_assert(std::is_const_v<SceneSnapshotPtr::element_type>);

    const auto nativeLayout = nlohmann::json::parse(
        NativeSettingsPanelLayoutView.begin(),
        NativeSettingsPanelLayoutView.end());
    CHECK(nativeLayout.value("type", std::string{}) == "Panel");
    CHECK(nativeLayout.value("name", std::string{})
        == NativeSettingsPanelQualifiedName);

    NativeLayoutAudit layoutAudit{};
    AuditNativeLayoutNode(nativeLayout, layoutAudit);
    CHECK(layoutAudit.namesUnique);
    CHECK(layoutAudit.buttonCount == 5);
    CHECK(layoutAudit.clickCatcherCount == 1);
    CHECK(layoutAudit.toggleCount == 0);
    CHECK(layoutAudit.tabBarCount == 0);
    CHECK(layoutAudit.scrollViewCount == 0);
    CHECK(layoutAudit.scrollControllerCount == 0);
    CHECK(layoutAudit.tableCount == 0);
    CHECK(layoutAudit.textRectsExplicit);
    CHECK(layoutAudit.actionRects.size() == 4U);
    for (std::size_t left = 0; left < layoutAudit.actionRects.size(); ++left) {
        for (std::size_t right = left + 1U;
             right < layoutAudit.actionRects.size();
             ++right) {
            CHECK(!RectsOverlap(
                layoutAudit.actionRects[left],
                layoutAudit.actionRects[right]));
        }
    }

    const std::set<std::string> expectedMessages{
        "PanelManager:ClosePanel:ruffneckk-mapsense/MapSenseControls",
        "PanelManager:OpenPanel:RuffnecKkMapSenseRevealLevel",
        "PanelManager:OpenPanel:RuffnecKkMapSenseRevealAct",
        "PanelManager:OpenPanel:RuffnecKkMapSenseRevealAll",
        "PanelManager:OpenPanel:RuffnecKkMapSenseRevealOff",
    };
    CHECK(layoutAudit.messages == expectedMessages);

    auto lowerLayout = std::string(NativeSettingsPanelLayoutView);
    for (auto& character : lowerLayout) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    constexpr std::array forbiddenCopy{
        "preview",
        "unsaved",
        "draft",
        "diagnostic",
        "runtime",
        "planned",
        "prototype",
        "automap addition",
        "apply",
        "default",
        "discard",
    };
    for (const auto* text : forbiddenCopy) {
        CHECK(lowerLayout.find(text) == std::string::npos);
    }

    CHECK(ClassifyNativeSettingsMessage(
        "PanelManager",
        "OpenPanel",
        "RuffnecKkMapSenseRevealLevel")
        == NativeSettingsAction::RevealLevel);
    CHECK(ClassifyNativeSettingsMessage(
        "PanelManager",
        "OpenPanel",
        "RuffnecKkMapSenseRevealAct")
        == NativeSettingsAction::RevealAct);
    CHECK(ClassifyNativeSettingsMessage(
        "PanelManager",
        "OpenPanel",
        "RuffnecKkMapSenseRevealAll")
        == NativeSettingsAction::ToggleRevealAll);
    CHECK(ClassifyNativeSettingsMessage(
        "PanelManager",
        "OpenPanel",
        "RuffnecKkMapSenseRevealOff")
        == NativeSettingsAction::DisableRevealAll);
    CHECK(!ClassifyNativeSettingsMessage(
        "PanelManager",
        "ClosePanel",
        "RuffnecKkMapSenseRevealLevel").has_value());
    CHECK(!ClassifyNativeSettingsMessage(
        "PanelManager",
        "OpenPanel",
        "RuffnecKkMapSenseRevealLevelExtra").has_value());
    CHECK(!ClassifyNativeSettingsMessage(
        "PanelManager",
        "OpenPanel",
        "ruffneckkMapSenseRevealLevel").has_value());
    CHECK(!ClassifyNativeSettingsMessage(
        "OtherTarget",
        "OpenPanel",
        "RuffnecKkMapSenseRevealLevel").has_value());

    const auto configured = ParseConfig(R"toml(
    schema_version = 10
[general]
enabled = false
[overlay]
opacity = 0.75
scale = 1.25
frame_rate = 90
[monsters]
detection_radius = 142
[monsters.normal]
shape = "x"
color = "#EDEDEDFF"
size = 16
thickness = 1.50
[monsters.minion]
shape = "player_cross"
color = "#FFD43BCC"
size = 17
thickness = 2.50
[monsters.champion]
shape = "dot"
color = "#3D8BFFFF"
size = 19
[monsters.unique]
shape = "player_cross"
color = "#FF8A24FF"
size = 23
thickness = 4.00
[monsters.super_unique_boss]
shape = "dot"
color = "#FF3B30FF"
size = 27
show_names = false
name_color = "#12345678"
name_size = 18
[immunities]
enabled = true
style = "split_halo"
indicator_size = 19
halo_thickness = 3.5
physical = "#AABBCCDD"
fire = "#FF2200CC"
[objects]
enabled = true
[objects.exit_labels]
enabled = false
color = "#ABC123EF"
size = 11
[objects.shrine_labels]
enabled = true
color = "#11223344"
size = 24
[objects.chests]
enabled = false
color = "#13579BDF"
locked_color = "#2468ACE0"
trapped_color = "#FEDCBA98"
size = 7
[objects.super_chests]
enabled = true
color = "#10203040"
size = 40
stars_enabled = false
stars_color = "#50607080"
stars_size = 32
[objects.armor_racks]
enabled = false
color = "#90A0B0C0"
size = 6
[objects.weapon_racks]
enabled = true
color = "#D0E0F0FF"
size = 39
[navigation]
line_thickness = 3.25
[navigation.waypoint]
enabled = false
color = "#1020F0CC"
[navigation.progression]
enabled = true
color = "#20F040DD"
[navigation.quests]
enabled = false
color = "#F02030EE"
[navigation.custom_levels]
enabled = true
color = "#A040F0FF"
targets = [
  { level_id = 12 },
  { level_name = "Mausoleum" },
  { level_name = "Ancient Tunnels" },
  { level_id = 119 },
]
[hud]
session_timer = true
[menu]
show_launcher = false
start_expanded = true
remember_position = false
position_x = 0.23
position_y = 0.81
[diagnostics]
enabled = true
)toml");
    CHECK(!configured.enabled);
    CHECK(configured.diagnostics);
    CHECK(configured.overlay.opacity == 0.75F);
    CHECK(configured.overlay.scale == 1.25F);
    CHECK(configured.overlay.frameRate == 90);
    CHECK(configured.monsters.normal.shape == MonsterMarkerShape::X);
    CHECK(configured.monsters.minion.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(configured.monsters.champion.shape == MonsterMarkerShape::Dot);
    CHECK(configured.monsters.unique.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(configured.monsters.superUniqueBoss.shape
        == MonsterMarkerShape::Dot);
    CHECK(configured.monsters.normal.size == 16.0F);
    CHECK(configured.monsters.minion.size == 17.0F);
    CHECK(configured.monsters.champion.size == 19.0F);
    CHECK(configured.monsters.unique.size == 23.0F);
    CHECK(configured.monsters.superUniqueBoss.size == 27.0F);
    CHECK(configured.monsters.normal.thickness == 1.50F);
    CHECK(configured.monsters.minion.thickness == 2.50F);
    CHECK(configured.monsters.champion.thickness == 2.0F);
    CHECK(configured.monsters.unique.thickness == 4.0F);
    CHECK(configured.monsters.superUniqueBoss.thickness == 2.0F);
    CHECK(ColorToHex(configured.monsters.normal.color) == "#EDEDEDFF");
    CHECK(ColorToHex(configured.monsters.minion.color) == "#FFD43BCC");
    CHECK(ColorToHex(configured.monsters.champion.color) == "#3D8BFFFF");
    CHECK(ColorToHex(configured.monsters.unique.color) == "#FF8A24FF");
    CHECK(ColorToHex(configured.monsters.superUniqueBoss.color)
        == "#FF3B30FF");
    CHECK(!configured.monsters.superUniqueBoss.showNames);
    CHECK(ColorToHex(configured.monsters.superUniqueBoss.nameColor)
        == "#12345678");
    CHECK(configured.monsters.superUniqueBoss.nameSize == 18.0F);
    CHECK(configured.immunities.enabled);
    CHECK(configured.immunities.style == ImmunityDisplayStyle::SplitHalo);
    CHECK(configured.immunities.indicatorSize == 19.0F);
    CHECK(configured.immunities.haloThickness == 3.5F);
    CHECK(ColorToHex(configured.immunities.physical) == "#AABBCCDD");
    CHECK(configured.immunities.fire.red == 1.0F);
    CHECK(configured.immunities.fire.alpha > 0.79F);
    CHECK(configured.objects.enabled);
    CHECK(!configured.objects.exitLabels.enabled);
    CHECK(ColorToHex(configured.objects.exitLabels.color) == "#ABC123EF");
    CHECK(configured.objects.exitLabels.size == 11.0F);
    CHECK(configured.objects.waypointLabels.enabled);
    CHECK(ColorToHex(configured.objects.waypointLabels.color)
        == "#FFD33DFF");
    CHECK(configured.objects.waypointLabels.size
        == DefaultAutomapLabelSize);
    CHECK(configured.objects.shrineLabels.enabled);
    CHECK(ColorToHex(configured.objects.shrineLabels.color) == "#11223344");
    CHECK(configured.objects.shrineLabels.size == 24.0F);
    CHECK(!configured.objects.chests.enabled);
    CHECK(ColorToHex(configured.objects.chests.outlineColor) == "#1450ADFF");
    CHECK(ColorToHex(configured.objects.chests.interiorColor) == "#13579BDF");
    CHECK(ColorToHex(configured.objects.chests.lockedAccentColor) == "#2468ACE0");
    CHECK(ColorToHex(configured.objects.chests.trappedAccentColor) == "#FEDCBA98");
    CHECK(configured.objects.chests.size == 7.0F);
    CHECK(configured.objects.superChests.enabled);
    CHECK(!configured.objects.superChests.starsEnabled);
    CHECK(ColorToHex(configured.objects.superChests.starsColor) == "#50607080");
    CHECK(configured.objects.superChests.starsSize == 32.0F);
    CHECK(!configured.objects.armorRacks.enabled);
    CHECK(ColorToHex(configured.objects.armorRacks.color) == "#90A0B0C0");
    CHECK(configured.objects.armorRacks.size == 6.0F);
    CHECK(configured.objects.weaponRacks.enabled);
    CHECK(ColorToHex(configured.objects.weaponRacks.color) == "#D0E0F0FF");
    CHECK(configured.objects.weaponRacks.size == 39.0F);
    CHECK(configured.navigation.lineThickness == 3.25F);
    CHECK(!configured.navigation.waypoint.enabled);
    CHECK(ColorToHex(configured.navigation.waypoint.color) == "#1020F0CC");
    CHECK(configured.navigation.progression.enabled);
    CHECK(ColorToHex(configured.navigation.progression.color) == "#20F040DD");
    CHECK(!configured.navigation.quests.enabled);
    CHECK(ColorToHex(configured.navigation.quests.color) == "#F02030EE");
    CHECK(configured.navigation.customLevels.enabled);
    CHECK(ColorToHex(configured.navigation.customLevels.color) == "#A040F0FF");
    CHECK(configured.navigation.customLevels.targets.size() == 4U);
    CHECK(std::get<std::int32_t>(
        configured.navigation.customLevels.targets[0]) == 12);
    CHECK(std::get<std::string>(
        configured.navigation.customLevels.targets[1]) == "Mausoleum");
    CHECK(std::get<std::string>(
        configured.navigation.customLevels.targets[2])
        == "Ancient Tunnels");
    CHECK(std::get<std::int32_t>(
        configured.navigation.customLevels.targets[3]) == 119);
    CHECK(configured.hud.sessionTimer);
    CHECK(!configured.menu.showLauncher);
    CHECK(configured.menu.startExpanded);
    CHECK(!configured.menu.rememberPosition);
    CHECK(configured.menu.positionX == 0.23F);
    CHECK(configured.menu.positionY == 0.81F);

    const auto schema12FeaturesAndChestPalette = ParseConfig(R"toml(
schema_version = 12
[general]
enabled = true
features_enabled = false
[objects.chests]
enabled = true
outline_color = "#12345678"
interior_color = "#23456789"
locked_accent_color = "#3456789A"
trapped_accent_color = "#456789AB"
size = 42
[objects.super_chests]
enabled = false
stars_enabled = true
stars_color = "#56789ABC"
stars_size = 33
)toml");
    CHECK(!schema12FeaturesAndChestPalette.enabled);
    CHECK(ColorToHex(
        schema12FeaturesAndChestPalette.objects.chests.outlineColor)
        == "#12345678");
    CHECK(ColorToHex(
        schema12FeaturesAndChestPalette.objects.chests.interiorColor)
        == "#23456789");
    CHECK(ColorToHex(
        schema12FeaturesAndChestPalette.objects.chests.lockedAccentColor)
        == "#3456789A");
    CHECK(ColorToHex(
        schema12FeaturesAndChestPalette.objects.chests.trappedAccentColor)
        == "#456789AB");
    CHECK(schema12FeaturesAndChestPalette.objects.chests.size == 42.0F);
    CHECK(!schema12FeaturesAndChestPalette.objects.superChests.enabled);
    CHECK(ColorToHex(
        schema12FeaturesAndChestPalette.objects.superChests.starsColor)
        == "#56789ABC");
    CHECK(schema12FeaturesAndChestPalette.objects.superChests.starsSize
        == 33.0F);
    CHECK(schema12FeaturesAndChestPalette.objects.waypointLabels.enabled);
    CHECK(ColorToHex(
        schema12FeaturesAndChestPalette.objects.waypointLabels.color)
        == "#FFD33DFF");
    CHECK(schema12FeaturesAndChestPalette.objects.waypointLabels.size
        == DefaultAutomapLabelSize);

    const auto schema13WaypointLabels = ParseConfig(R"toml(
schema_version = 13
[objects.waypoint_labels]
enabled = false
color = "#12345678"
size = 31
)toml");
    CHECK(!schema13WaypointLabels.objects.waypointLabels.enabled);
    CHECK(ColorToHex(schema13WaypointLabels.objects.waypointLabels.color)
        == "#12345678");
    CHECK(schema13WaypointLabels.objects.waypointLabels.size == 31.0F);
    const auto serializedWaypointLabels = SerializeConfig(
        schema13WaypointLabels);
    CHECK(serializedWaypointLabels.find(
        "[objects.waypoint_labels]\nenabled = false\n"
        "color = \"#12345678\"\nsize = 31.00")
        != std::string::npos);
    const auto roundTripWaypointLabels = ParseConfig(
        serializedWaypointLabels);
    CHECK(SameAutomapLabelOptions(
        roundTripWaypointLabels.objects.waypointLabels,
        schema13WaypointLabels.objects.waypointLabels));

    const auto schema15Missiles = ParseConfig(R"toml(
schema_version = 15
[missiles]
enabled = false
[missiles.fire]
color = "#01020304"
size = 1
[missiles.cold]
color = "#11121314"
size = 2
[missiles.lightning]
color = "#21222324"
size = 3
[missiles.poison]
color = "#31323334"
size = 4
[missiles.physical]
color = "#41424344"
size = 15
[missiles.magic]
color = "#51525354"
size = 16
)toml");
    CHECK(!schema15Missiles.missiles.enabled);
    CHECK(ColorToHex(schema15Missiles.missiles.fire.color) == "#01020304");
    CHECK(ColorToHex(schema15Missiles.missiles.cold.color) == "#11121314");
    CHECK(ColorToHex(schema15Missiles.missiles.lightning.color)
        == "#21222324");
    CHECK(ColorToHex(schema15Missiles.missiles.poison.color)
        == "#31323334");
    CHECK(ColorToHex(schema15Missiles.missiles.physical.color)
        == "#41424344");
    CHECK(ColorToHex(schema15Missiles.missiles.magic.color) == "#51525354");
    CHECK(schema15Missiles.missiles.fire.size == 1.0F);
    CHECK(schema15Missiles.missiles.magic.size == 16.0F);
    const auto serializedMissiles = SerializeConfig(schema15Missiles);
    CHECK(serializedMissiles.find("[missiles]\nenabled = false")
        != std::string::npos);
    CHECK(serializedMissiles.find(
        "[missiles.fire]\ncolor = \"#01020304\"\nsize = 1.00")
        != std::string::npos);
    const auto roundTripMissiles = ParseConfig(serializedMissiles);
    CHECK(SameMissileOptions(
        roundTripMissiles.missiles,
        schema15Missiles.missiles));

    const auto schema16FeatureMastersAndTheme = ParseConfig(R"toml(
schema_version = 16
[general]
enabled = true
[monsters]
enabled = false
[menu]
theme = "arcane_sanctuary"
)toml");
    CHECK(schema16FeatureMastersAndTheme.enabled);
    CHECK(!schema16FeatureMastersAndTheme.monsters.enabled);
    CHECK(schema16FeatureMastersAndTheme.menu.theme
        == MenuTheme::ArcaneSanctuary);
    const auto serializedSchema16 = SerializeConfig(
        schema16FeatureMastersAndTheme);
    CHECK(serializedSchema16.find("features_enabled") == std::string::npos);
    CHECK(serializedSchema16.find("[overlay]\nenabled")
        == std::string::npos);
    CHECK(serializedSchema16.find("[monsters]\nenabled = false")
        != std::string::npos);
    CHECK(serializedSchema16.find(
        "[menu]\ntheme = \"arcane_sanctuary\"")
        != std::string::npos);
    const auto roundTripSchema16 = ParseConfig(serializedSchema16);
    CHECK(roundTripSchema16.enabled);
    CHECK(!roundTripSchema16.monsters.enabled);
    CHECK(roundTripSchema16.menu.theme == MenuTheme::ArcaneSanctuary);

    const auto legacyOverlayDisabled = ParseConfig(R"toml(
schema_version = 15
[overlay]
enabled = false
[monsters]
[immunities]
enabled = true
[missiles]
enabled = true
[objects]
enabled = true
[navigation.waypoint]
enabled = true
[navigation.progression]
enabled = true
[navigation.quests]
enabled = true
[navigation.custom_levels]
enabled = true
)toml");
    CHECK(legacyOverlayDisabled.enabled);
    CHECK(!legacyOverlayDisabled.monsters.enabled);
    CHECK(!legacyOverlayDisabled.immunities.enabled);
    CHECK(!legacyOverlayDisabled.missiles.enabled);
    CHECK(!legacyOverlayDisabled.objects.enabled);
    CHECK(!legacyOverlayDisabled.navigation.waypoint.enabled);
    CHECK(!legacyOverlayDisabled.navigation.progression.enabled);
    CHECK(!legacyOverlayDisabled.navigation.quests.enabled);
    CHECK(!legacyOverlayDisabled.navigation.customLevels.enabled);

    const auto serialized = SerializeConfig(configured);
    CHECK(serialized.find("[hud]") == std::string::npos);
    CHECK(serialized.find("start_menu_open") == std::string::npos);
    CHECK(serialized.find("marker_size") == std::string::npos);
    CHECK(serialized.find("features_enabled") == std::string::npos);
    CHECK(serialized.find("[overlay]\nenabled") == std::string::npos);
    CHECK(serialized.find("outline_color = \"#1450ADFF\"")
        != std::string::npos);
    CHECK(serialized.find("interior_color = \"#13579BDF\"")
        != std::string::npos);
    CHECK(serialized.find("locked_accent_color = \"#2468ACE0\"")
        != std::string::npos);
    CHECK(serialized.find("trapped_accent_color = \"#FEDCBA98\"")
        != std::string::npos);
    CHECK(serialized.find("locked_color") == std::string::npos);
    constexpr std::array legacyMonsterToggleKeys{
        "\nnormal = ",
        "\nminion = ",
        "\nchampion = ",
        "\nunique = ",
        "\nsuper_unique = ",
    };
    for (const auto* key : legacyMonsterToggleKeys) {
        CHECK(serialized.find(key) == std::string::npos);
    }
    CHECK(serialized.find("[monsters.normal]\nshape = \"x\"")
        != std::string::npos);
    CHECK(serialized.find(
        "[monsters.minion]\nshape = \"player_cross\"")
        != std::string::npos);
    CHECK(serialized.find("[monsters.champion]\nshape = \"dot\"")
        != std::string::npos);
    CHECK(serialized.find(
        "[monsters.unique]\nshape = \"player_cross\"")
        != std::string::npos);
    CHECK(serialized.find(
        "[monsters.super_unique_boss]\nshape = \"dot\"")
        != std::string::npos);
    CHECK(serialized.find("[immunities]\nenabled = true")
        != std::string::npos);
    CHECK(serialized.find("style = \"split_halo\"")
        != std::string::npos);
    CHECK(serialized.find("indicator_size = 19.00")
        != std::string::npos);
    CHECK(serialized.find("detection_radius") == std::string::npos);
    CHECK(serialized.find("marker_thickness") == std::string::npos);
    CHECK(serialized.find("size = 16.00\nthickness = 1.50")
        != std::string::npos);
    CHECK(serialized.find("size = 17.00\nthickness = 2.50")
        != std::string::npos);
    CHECK(serialized.find(
        "[monsters.champion]\nshape = \"dot\"\n"
        "color = \"#3D8BFFFF\"\nsize = 19.00\n\n")
        != std::string::npos);
    CHECK(serialized.find("size = 23.00\nthickness = 4.00")
        != std::string::npos);
    CHECK(serialized.find(
        "[monsters.super_unique_boss]\nshape = \"dot\"\n"
        "color = \"#FF3B30FF\"\nsize = 27.00\n"
        "show_names = false\nname_color = \"#12345678\"\n"
        "name_size = 18.00\n\n")
        != std::string::npos);
    CHECK(serialized.find("halo_thickness = 3.50")
        != std::string::npos);
    CHECK(serialized.find("[objects]\nenabled = true")
        != std::string::npos);
    CHECK(serialized.find("[objects.exit_labels]\nenabled = false")
        != std::string::npos);
    CHECK(serialized.find(
        "[objects.waypoint_labels]\nenabled = true\n"
        "color = \"#FFD33DFF\"\nsize = 28.00")
        != std::string::npos);
    CHECK(serialized.find("[objects.super_chests]\nenabled = true")
        != std::string::npos);
    CHECK(serialized.find("stars_enabled = false")
        != std::string::npos);
    CHECK(serialized.find("stars_color = \"#50607080\"")
        != std::string::npos);
    CHECK(serialized.find("stars_size = 32.00")
        != std::string::npos);
    CHECK(serialized.find("[navigation]\nline_thickness = 3.25")
        != std::string::npos);
    CHECK(serialized.find("[navigation.waypoint]\nenabled = false")
        != std::string::npos);
    CHECK(serialized.find("[navigation.progression]\nenabled = true")
        != std::string::npos);
    CHECK(serialized.find("[navigation.quests]\nenabled = false")
        != std::string::npos);
    CHECK(serialized.find("[navigation.custom_levels]\nenabled = true")
        != std::string::npos);
    CHECK(serialized.find("{ level_id = 12 }") != std::string::npos);
    CHECK(serialized.find("{ level_name = \"Mausoleum\" }")
        != std::string::npos);
    CHECK(serialized.find("{ level_name = \"Ancient Tunnels\" }")
        != std::string::npos);
    CHECK(serialized.find("{ level_id = 119 }") != std::string::npos);
    CHECK(serialized.find("[menu]") != std::string::npos);
    const auto roundTrip = ParseConfig(serialized);
    CHECK(!roundTrip.enabled);
    CHECK(roundTrip.overlay.frameRate == 90);
    CHECK(roundTrip.monsters.normal.shape == MonsterMarkerShape::X);
    CHECK(roundTrip.monsters.minion.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(roundTrip.monsters.champion.shape == MonsterMarkerShape::Dot);
    CHECK(roundTrip.monsters.unique.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(roundTrip.monsters.superUniqueBoss.shape
        == MonsterMarkerShape::Dot);
    CHECK(roundTrip.monsters.normal.size == 16.0F);
    CHECK(roundTrip.monsters.minion.size == 17.0F);
    CHECK(roundTrip.monsters.champion.size == 19.0F);
    CHECK(roundTrip.monsters.unique.size == 23.0F);
    CHECK(roundTrip.monsters.superUniqueBoss.size == 27.0F);
    CHECK(roundTrip.monsters.normal.thickness == 1.50F);
    CHECK(roundTrip.monsters.minion.thickness == 2.50F);
    CHECK(roundTrip.monsters.champion.thickness == 2.0F);
    CHECK(roundTrip.monsters.unique.thickness == 4.0F);
    CHECK(roundTrip.monsters.superUniqueBoss.thickness == 2.0F);
    CHECK(ColorToHex(roundTrip.monsters.normal.color) == "#EDEDEDFF");
    CHECK(ColorToHex(roundTrip.monsters.minion.color) == "#FFD43BCC");
    CHECK(ColorToHex(roundTrip.monsters.champion.color) == "#3D8BFFFF");
    CHECK(ColorToHex(roundTrip.monsters.unique.color) == "#FF8A24FF");
    CHECK(ColorToHex(roundTrip.monsters.superUniqueBoss.color)
        == "#FF3B30FF");
    CHECK(!roundTrip.monsters.superUniqueBoss.showNames);
    CHECK(ColorToHex(roundTrip.monsters.superUniqueBoss.nameColor)
        == "#12345678");
    CHECK(roundTrip.monsters.superUniqueBoss.nameSize == 18.0F);
    CHECK(roundTrip.immunities.enabled);
    CHECK(roundTrip.immunities.style == ImmunityDisplayStyle::SplitHalo);
    CHECK(roundTrip.immunities.indicatorSize == 19.0F);
    CHECK(roundTrip.immunities.haloThickness == 3.5F);
    CHECK(ColorToHex(roundTrip.immunities.physical) == "#AABBCCDD");
    CHECK(ColorToHex(roundTrip.immunities.fire) == "#FF2200CC");
    CHECK(SameMissileOptions(roundTrip.missiles, configured.missiles));
    CHECK(SameObjectsOptions(roundTrip.objects, configured.objects));
    CHECK(roundTrip.navigation.lineThickness == 3.25F);
    CHECK(!roundTrip.navigation.waypoint.enabled);
    CHECK(ColorToHex(roundTrip.navigation.waypoint.color) == "#1020F0CC");
    CHECK(roundTrip.navigation.progression.enabled);
    CHECK(ColorToHex(roundTrip.navigation.progression.color) == "#20F040DD");
    CHECK(!roundTrip.navigation.quests.enabled);
    CHECK(ColorToHex(roundTrip.navigation.quests.color) == "#F02030EE");
    CHECK(roundTrip.navigation.customLevels.enabled);
    CHECK(ColorToHex(roundTrip.navigation.customLevels.color) == "#A040F0FF");
    CHECK(roundTrip.navigation.customLevels.targets
        == configured.navigation.customLevels.targets);
    CHECK(!roundTrip.hud.mercenaryHealth);
    CHECK(!roundTrip.hud.sessionTimer);
    CHECK(!roundTrip.hud.experienceTracker);
    CHECK(!roundTrip.menu.showLauncher);
    CHECK(roundTrip.menu.startExpanded);
    CHECK(!roundTrip.menu.rememberPosition);
    CHECK(roundTrip.menu.positionX == 0.23F);
    CHECK(roundTrip.menu.positionY == 0.81F);

    const auto schema7QuestMigration = ParseConfig(R"toml(
schema_version = 7
[navigation.quests]
enabled = false
color = "#F02030EE"
)toml");
    CHECK(schema7QuestMigration.navigation.quests.enabled);
    CHECK(ColorToHex(schema7QuestMigration.navigation.quests.color)
        == "#F02030EE");

    const auto defaults = ParseConfig("schema_version = 4");
    CHECK(defaults.overlay.opacity == 1.0F);
    CHECK(defaults.monsters.normal.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(defaults.monsters.minion.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(defaults.monsters.champion.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(defaults.monsters.unique.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(defaults.monsters.superUniqueBoss.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(defaults.monsters.normal.size == 18.0F);
    CHECK(defaults.monsters.minion.size == 18.0F);
    CHECK(defaults.monsters.champion.size == 20.0F);
    CHECK(defaults.monsters.unique.size == 22.0F);
    CHECK(defaults.monsters.superUniqueBoss.size == 24.0F);
    CHECK(defaults.monsters.normal.thickness == 2.0F);
    CHECK(defaults.monsters.minion.thickness == 2.0F);
    CHECK(defaults.monsters.champion.thickness == 2.0F);
    CHECK(defaults.monsters.unique.thickness == 2.0F);
    CHECK(defaults.monsters.superUniqueBoss.thickness == 2.0F);
    CHECK(defaults.monsters.superUniqueBoss.showNames);
    CHECK(ColorToHex(defaults.monsters.superUniqueBoss.nameColor)
        == "#FFD33DFF");
    CHECK(defaults.monsters.superUniqueBoss.nameSize
        == DefaultAutomapLabelSize);
    CHECK(ColorToHex(defaults.monsters.normal.color) == "#FFFFFFFF");
    CHECK(ColorToHex(defaults.monsters.minion.color) == "#FFD43BFF");
    CHECK(ColorToHex(defaults.monsters.champion.color) == "#3D8BFFFF");
    CHECK(ColorToHex(defaults.monsters.unique.color) == "#FF8A24FF");
    CHECK(ColorToHex(defaults.monsters.superUniqueBoss.color) == "#FF3B30FF");
    CHECK(defaults.immunities.enabled);
    CHECK(defaults.immunities.style == ImmunityDisplayStyle::ColoredI);
    CHECK(defaults.immunities.indicatorSize
        == DefaultImmunityIndicatorSize);
    CHECK(defaults.immunities.haloThickness
        == DefaultImmunityHaloThickness);
    CHECK(ColorToHex(defaults.immunities.physical) == "#D8C39AFF");
    CHECK(defaults.missiles.enabled);
    CHECK(ColorToHex(defaults.missiles.fire.color) == "#FF00007F");
    CHECK(ColorToHex(defaults.missiles.cold.color) == "#00D0FF7F");
    CHECK(ColorToHex(defaults.missiles.lightning.color) == "#FFFF0046");
    CHECK(ColorToHex(defaults.missiles.poison.color) == "#32CD327F");
    CHECK(ColorToHex(defaults.missiles.physical.color) == "#CD853F7F");
    CHECK(ColorToHex(defaults.missiles.magic.color) == "#FF88007F");
    CHECK(defaults.missiles.fire.size == DefaultMissileMarkerSize);
    CHECK(defaults.objects.enabled);
    CHECK(defaults.objects.exitLabels.enabled);
    CHECK(ColorToHex(defaults.objects.exitLabels.color) == "#FFD33DFF");
    CHECK(defaults.objects.exitLabels.size == DefaultAutomapLabelSize);
    CHECK(defaults.objects.shrineLabels.enabled);
    CHECK(ColorToHex(defaults.objects.shrineLabels.color) == "#FFD33DFF");
    CHECK(defaults.objects.shrineLabels.size == DefaultAutomapLabelSize);
    CHECK(defaults.objects.chests.enabled);
    CHECK(ColorToHex(defaults.objects.chests.outlineColor) == "#1450ADFF");
    CHECK(ColorToHex(defaults.objects.chests.interiorColor) == "#B88A2AB0");
    CHECK(ColorToHex(defaults.objects.chests.lockedAccentColor) == "#00FFFFFF");
    CHECK(ColorToHex(defaults.objects.chests.trappedAccentColor) == "#FF3B30FF");
    CHECK(defaults.objects.chests.size == DefaultAutomapObjectSize);
    CHECK(defaults.objects.superChests.enabled);
    CHECK(defaults.objects.superChests.starsEnabled);
    CHECK(ColorToHex(defaults.objects.superChests.starsColor)
        == "#FFD33DFF");
    CHECK(defaults.objects.superChests.starsSize == DefaultAutomapLabelSize);
    CHECK(defaults.objects.armorRacks.enabled);
    CHECK(ColorToHex(defaults.objects.armorRacks.color) == "#3D8BFFFF");
    CHECK(defaults.objects.armorRacks.size == DefaultAutomapObjectSize);
    CHECK(defaults.objects.weaponRacks.enabled);
    CHECK(ColorToHex(defaults.objects.weaponRacks.color) == "#FF8A24FF");
    CHECK(defaults.objects.weaponRacks.size == DefaultAutomapObjectSize);
    CHECK(defaults.navigation.lineThickness
        == DefaultNavigationLineThickness);
    CHECK(defaults.navigation.waypoint.enabled);
    CHECK(ColorToHex(defaults.navigation.waypoint.color) == "#3D8BFFFF");
    CHECK(defaults.navigation.progression.enabled);
    CHECK(ColorToHex(defaults.navigation.progression.color) == "#57E03DFF");
    CHECK(defaults.navigation.quests.enabled);
    CHECK(ColorToHex(defaults.navigation.quests.color) == "#FF3B30FF");
    CHECK(!defaults.navigation.customLevels.enabled);
    CHECK(ColorToHex(defaults.navigation.customLevels.color) == "#C75CFFFF");
    CHECK(defaults.navigation.customLevels.targets.empty());
    CHECK(!defaults.hud.mercenaryHealth);
    CHECK(!defaults.hud.sessionTimer);
    CHECK(!defaults.hud.experienceTracker);
    CHECK(defaults.menu.showLauncher);
    CHECK(!defaults.menu.startExpanded);
    CHECK(defaults.menu.rememberPosition);
    CHECK(defaults.menu.positionX == 0.86F);
    CHECK(defaults.menu.positionY == 0.04F);

    const auto schema10ObjectPaletteMigration = ParseConfig(R"toml(
schema_version = 10
[objects.shrine_labels]
color = "#FFBF1FFF"
[objects.chests]
color = "#D8C39AFF"
locked_color = "#57E03DFF"
)toml");
    CHECK(ColorToHex(
        schema10ObjectPaletteMigration.objects.shrineLabels.color)
        == "#FFD33DFF");
    CHECK(ColorToHex(
        schema10ObjectPaletteMigration.objects.chests.interiorColor)
        == "#B88A2AB0");
    CHECK(ColorToHex(
        schema10ObjectPaletteMigration.objects.chests.lockedAccentColor)
        == "#00FFFFFF");

    const auto schema13LockedStateMigration = ParseConfig(R"toml(
schema_version = 13
[objects.chests]
locked_accent_color = "#D89B2BFF"
)toml");
    CHECK(ColorToHex(
        schema13LockedStateMigration.objects.chests.lockedAccentColor)
        == "#00FFFFFF");

    const auto schema11ChestVisualMigration = ParseConfig(R"toml(
schema_version = 11
[objects.chests]
color = "#B88A2AFF"
[objects.super_chests]
stars_color = "#FFFFFFFF"
)toml");
    CHECK(ColorToHex(
        schema11ChestVisualMigration.objects.chests.interiorColor)
        == "#B88A2AB0");
    CHECK(ColorToHex(
        schema11ChestVisualMigration.objects.superChests.starsColor)
        == "#FFD33DFF");

    const auto schema10CustomObjectPalette = ParseConfig(R"toml(
schema_version = 10
[objects.shrine_labels]
color = "#12345678"
[objects.chests]
color = "#23456789"
locked_color = "#3456789A"
)toml");
    CHECK(ColorToHex(schema10CustomObjectPalette.objects.shrineLabels.color)
        == "#12345678");
    CHECK(ColorToHex(schema10CustomObjectPalette.objects.chests.interiorColor)
        == "#23456789");
    CHECK(ColorToHex(
        schema10CustomObjectPalette.objects.chests.lockedAccentColor)
        == "#3456789A");

    const auto schema4GreyMigration = ParseConfig(R"toml(
schema_version = 4
[immunities]
enabled = false
physical = "#C7C7C7FF"
)toml");
    CHECK(!schema4GreyMigration.immunities.enabled);
    CHECK(schema4GreyMigration.immunities.style
        == ImmunityDisplayStyle::ColoredI);
    CHECK(ColorToHex(schema4GreyMigration.immunities.physical)
        == "#D8C39AFF");

    const auto schema4CustomPhysical = ParseConfig(R"toml(
schema_version = 4
[immunities]
physical = "#ABCDEFCC"
)toml");
    CHECK(ColorToHex(schema4CustomPhysical.immunities.physical)
        == "#ABCDEFCC");

    const auto schema3Migration = ParseConfig(R"toml(
schema_version = 3
[general]
enabled = false
[overlay]
opacity = 0.75
scale = 1.25
frame_rate = 90
[monsters]
detection_radius = 420
[monsters.normal]
color = "#EDEDEDFF"
size = 16
[monsters.minion]
color = "#FFD43BCC"
size = 17
[monsters.champion]
color = "#3D8BFFFF"
size = 19
[monsters.unique]
color = "#FF8A24FF"
size = 23
[monsters.super_unique_boss]
color = "#FF3B30FF"
size = 27
[immunities]
fire = "#FF2200CC"
[hud]
session_timer = true
[menu]
show_launcher = false
start_expanded = true
remember_position = false
position_x = 0.23
position_y = 0.81
[diagnostics]
enabled = true
)toml");
    CHECK(!schema3Migration.enabled);
    CHECK(schema3Migration.diagnostics);
    CHECK(schema3Migration.overlay.opacity == 0.75F);
    CHECK(schema3Migration.overlay.scale == 1.25F);
    CHECK(schema3Migration.overlay.frameRate == 90);
    CHECK(schema3Migration.monsters.normal.shape == MonsterMarkerShape::X);
    CHECK(schema3Migration.monsters.minion.shape == MonsterMarkerShape::X);
    CHECK(schema3Migration.monsters.champion.shape == MonsterMarkerShape::X);
    CHECK(schema3Migration.monsters.unique.shape == MonsterMarkerShape::X);
    CHECK(schema3Migration.monsters.superUniqueBoss.shape
        == MonsterMarkerShape::X);
    CHECK(schema3Migration.monsters.normal.size == 16.0F);
    CHECK(schema3Migration.monsters.minion.size == 17.0F);
    CHECK(schema3Migration.monsters.champion.size == 19.0F);
    CHECK(schema3Migration.monsters.unique.size == 23.0F);
    CHECK(schema3Migration.monsters.superUniqueBoss.size == 27.0F);
    CHECK(schema3Migration.monsters.normal.thickness == 2.0F);
    CHECK(schema3Migration.monsters.minion.thickness == 2.0F);
    CHECK(schema3Migration.monsters.champion.thickness == 2.0F);
    CHECK(schema3Migration.monsters.unique.thickness == 2.0F);
    CHECK(schema3Migration.monsters.superUniqueBoss.thickness == 2.0F);
    CHECK(ColorToHex(schema3Migration.monsters.normal.color) == "#EDEDEDFF");
    CHECK(ColorToHex(schema3Migration.monsters.minion.color) == "#FFD43BCC");
    CHECK(ColorToHex(schema3Migration.monsters.champion.color)
        == "#3D8BFFFF");
    CHECK(ColorToHex(schema3Migration.monsters.unique.color) == "#FF8A24FF");
    CHECK(ColorToHex(schema3Migration.monsters.superUniqueBoss.color)
        == "#FF3B30FF");
    CHECK(schema3Migration.immunities.enabled);
    CHECK(schema3Migration.immunities.style
        == ImmunityDisplayStyle::ColoredI);
    CHECK(ColorToHex(schema3Migration.immunities.physical)
        == "#D8C39AFF");
    CHECK(ColorToHex(schema3Migration.immunities.fire) == "#FF2200CC");
    CHECK(schema3Migration.hud.sessionTimer);
    CHECK(!schema3Migration.menu.showLauncher);
    CHECK(schema3Migration.menu.startExpanded);
    CHECK(!schema3Migration.menu.rememberPosition);
    CHECK(schema3Migration.menu.positionX == 0.23F);
    CHECK(schema3Migration.menu.positionY == 0.81F);

    const auto legacySchema1 = ParseConfig(R"toml(
schema_version = 1
[monsters]
normal = false
minion = false
champion = false
unique = false
super_unique = false
marker_size = 13
[immunities]
enabled = false
physical = "#C7C7C7FF"
)toml");
    CHECK(legacySchema1.monsters.normal.shape == MonsterMarkerShape::X);
    CHECK(legacySchema1.monsters.minion.shape == MonsterMarkerShape::X);
    CHECK(legacySchema1.monsters.champion.shape == MonsterMarkerShape::X);
    CHECK(legacySchema1.monsters.unique.shape == MonsterMarkerShape::X);
    CHECK(legacySchema1.monsters.superUniqueBoss.shape == MonsterMarkerShape::X);
    CHECK(legacySchema1.monsters.normal.size == 13.0F);
    CHECK(legacySchema1.monsters.minion.size == 13.0F);
    CHECK(legacySchema1.monsters.champion.size == 13.0F);
    CHECK(legacySchema1.monsters.unique.size == 13.0F);
    CHECK(legacySchema1.monsters.superUniqueBoss.size == 13.0F);
    CHECK(!legacySchema1.immunities.enabled);
    CHECK(legacySchema1.immunities.style
        == ImmunityDisplayStyle::ColoredI);
    CHECK(ColorToHex(legacySchema1.immunities.physical) == "#D8C39AFF");

    const auto legacyRuntime = ParseConfig(R"toml(
schema_version = 2
[overlay]
start_menu_open = true
opacity = 0.90
[monsters]
normal = false
minion = false
champion = false
unique = false
super_unique = false
marker_size = 14
[hud]
mercenary_health = true
session_timer = true
experience_tracker = true
show_with_automap_only = true
)toml");
    CHECK(legacyRuntime.overlay.startMenuOpen);
    CHECK(legacyRuntime.overlay.opacity == 0.90F);
    CHECK(legacyRuntime.monsters.normal.shape == MonsterMarkerShape::X);
    CHECK(legacyRuntime.monsters.minion.shape == MonsterMarkerShape::X);
    CHECK(legacyRuntime.monsters.champion.shape == MonsterMarkerShape::X);
    CHECK(legacyRuntime.monsters.unique.shape == MonsterMarkerShape::X);
    CHECK(legacyRuntime.monsters.superUniqueBoss.shape
        == MonsterMarkerShape::X);
    CHECK(legacyRuntime.monsters.normal.size == 14.0F);
    CHECK(legacyRuntime.monsters.minion.size == 14.0F);
    CHECK(legacyRuntime.monsters.champion.size == 14.0F);
    CHECK(legacyRuntime.monsters.unique.size == 14.0F);
    CHECK(legacyRuntime.monsters.superUniqueBoss.size == 14.0F);
    CHECK(legacyRuntime.hud.mercenaryHealth);
    CHECK(legacyRuntime.hud.sessionTimer);
    CHECK(legacyRuntime.hud.experienceTracker);
    CHECK(legacyRuntime.hud.showWithAutomapOnly);
    CHECK(legacyRuntime.immunities.style
        == ImmunityDisplayStyle::ColoredI);
    CHECK(ColorToHex(legacyRuntime.immunities.physical) == "#D8C39AFF");

    for (const auto theme : MenuThemes) {
        CHECK(ParseMenuTheme(MenuThemeToString(theme)) == theme);
    }

    CHECK(Throws([] { ParseConfig(""); }));
    CHECK(Throws([] { ParseConfig("schema_version = 17"); }));
    CHECK(Throws([] { ParseConfig("schema_version = true"); }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 16\n[general]\nfeatures_enabled = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 16\n[overlay]\nenabled = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 15\n[monsters]\nenabled = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 15\n[menu]\ntheme = \"hellfire\"");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 16\n[menu]\ntheme = \"unknown\"");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 16\n[menu]\ntheme = 1");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 5\n[navigation]\nline_thickness = 2");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 1\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 1\n[general]\nenabled = 1");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 1\ngeneral = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 1\n[diagnostics]\nverbose = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 9\n[objects]\nenabled = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 14\n[missiles]\nenabled = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 15\n[missiles]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 15\n[missiles.fire]\nsize = 0.5");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 15\n[missiles.magic]\nsize = 16.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 9\n[monsters.super_unique_boss]\n"
            "show_names = false");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 10\n[objects]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.exit_labels]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.shrine_labels]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 12\n[objects.waypoint_labels]\n"
            "enabled = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 13\n[objects.waypoint_labels]\n"
            "unknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.chests]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.super_chests]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.armor_racks]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.weapon_racks]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.exit_labels]\nsize = 7.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.shrine_labels]\nsize = 72.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 13\n[objects.waypoint_labels]\nsize = 7.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 13\n[objects.waypoint_labels]\nsize = 72.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.chests]\nsize = 5.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.super_chests]\nsize = 80.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.super_chests]\nstars_size = 7.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.super_chests]\nstars_size = 72.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.armor_racks]\nsize = 5.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.weapon_racks]\nsize = 80.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.chests]\nlocked_color = \"green\"");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[objects.super_chests]\n"
            "stars_enabled = 1");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 12\n[objects.chests]\n"
            "color = \"#FFFFFFFF\"");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 12\n[objects.super_chests]\n"
            "size = 36");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[monsters.super_unique_boss]\n"
            "name_size = 7.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 10\n[monsters.super_unique_boss]\n"
            "name_size = 72.5");
    }));
    const auto objectRangeBoundaries = ParseConfig(R"toml(
schema_version = 10
[objects.exit_labels]
size = 8
[objects.shrine_labels]
size = 72
[objects.chests]
size = 6
[objects.super_chests]
size = 80
stars_size = 8
[objects.armor_racks]
size = 80
[objects.weapon_racks]
size = 6
[monsters.super_unique_boss]
name_size = 72
)toml");
    CHECK(objectRangeBoundaries.objects.exitLabels.size == 8.0F);
    CHECK(objectRangeBoundaries.objects.shrineLabels.size == 72.0F);
    CHECK(objectRangeBoundaries.objects.chests.size == 6.0F);
    CHECK(objectRangeBoundaries.objects.superChests.starsSize == 8.0F);
    CHECK(objectRangeBoundaries.objects.armorRacks.size == 80.0F);
    CHECK(objectRangeBoundaries.objects.weaponRacks.size == 6.0F);
    CHECK(objectRangeBoundaries.monsters.superUniqueBoss.nameSize == 72.0F);
    const auto waypointLabelRangeBoundaries = ParseConfig(R"toml(
schema_version = 13
[objects.waypoint_labels]
size = 8
)toml");
    CHECK(waypointLabelRangeBoundaries.objects.waypointLabels.size == 8.0F);
    const auto waypointLabelMaximumBoundary = ParseConfig(R"toml(
schema_version = 13
[objects.waypoint_labels]
size = 72
)toml");
    CHECK(waypointLabelMaximumBoundary.objects.waypointLabels.size == 72.0F);
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[overlay]\nopacity = 1.5");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[overlay]\nframe_rate = 12");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[immunities]\nfire = \"red\"");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 5\n[immunities]\nstyle = \"unknown\"");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 5\n[immunities]\nstyle = 1");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 5\n[immunities]\nindicator_size = 7");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 5\n[immunities]\nindicator_size = 33");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 5\n[immunities]\nhalo_thickness = 0.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 5\n[immunities]\nhalo_thickness = 6.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation]\nline_thickness = 0.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation]\nline_thickness = 8.5");
    }));
    const auto minimumNavigationThickness = ParseConfig(
        "schema_version = 6\n[navigation]\nline_thickness = 1");
    CHECK(minimumNavigationThickness.navigation.lineThickness == 1.0F);
    const auto maximumNavigationThickness = ParseConfig(
        "schema_version = 6\n[navigation]\nline_thickness = 8");
    CHECK(maximumNavigationThickness.navigation.lineThickness == 8.0F);
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.waypoint]\nunknown = true");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = 12");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [12]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{}]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_id = 12, level_name = \"Pit Level 1\" }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_id = \"12\" }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_id = 0 }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_id = 2147483648 }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_id = 65536 }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_name = 12 }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_name = \"\" }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_name = \" Mausoleum\" }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_name = \"Mausoleum\\n\" }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_name = \"Mausoleum\", extra = true }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_id = 12 }, { level_id = 12 }]");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 6\n[navigation.custom_levels]\n"
            "targets = [{ level_name = \"Mausoleum\" }, "
            "{ level_name = \"Mausoleum\" }]");
    }));
    const auto minimumImmunityControls = ParseConfig(
        "schema_version = 5\n[immunities]\n"
        "indicator_size = 8\nhalo_thickness = 1");
    CHECK(minimumImmunityControls.immunities.indicatorSize == 8.0F);
    CHECK(minimumImmunityControls.immunities.haloThickness == 1.0F);
    const auto maximumImmunityControls = ParseConfig(
        "schema_version = 5\n[immunities]\n"
        "indicator_size = 32\nhalo_thickness = 6");
    CHECK(maximumImmunityControls.immunities.indicatorSize == 32.0F);
    CHECK(maximumImmunityControls.immunities.haloThickness == 6.0F);
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[menu]\nshow_launcher = 1");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[menu]\nposition_x = -0.01");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[menu]\nposition_y = 1.01");
    }));
    CHECK(Throws([] {
        ParseConfig("schema_version = 3\n[menu]\nposition = 0.5");
    }));
    // detection_radius was never a scan radius: it only filtered monsters
    // after D2R's complete client table had already been traversed. Preserve
    // old files by accepting the retired key regardless of its former units
    // or TOML type, then prove that the next save removes it.
    for (const auto* retiredRadius : {
            "schema_version = 3\n[monsters]\ndetection_radius = 59",
            "schema_version = 4\n[monsters]\ndetection_radius = 2501",
            "schema_version = 7\n[monsters]\ndetection_radius = 221",
            "schema_version = 8\n[monsters]\ndetection_radius = \"retired\""}) {
        const auto migratedRadius = ParseConfig(retiredRadius);
        CHECK(SerializeConfig(migratedRadius).find("detection_radius")
            == std::string::npos);
    }
    for (const auto* removedMasterThickness : {
            "schema_version = 3\n[monsters]\nmarker_thickness = 2",
            "schema_version = 8\n[monsters]\nmarker_thickness = 2",
            "schema_version = 9\n[monsters]\nmarker_thickness = 2"}) {
        CHECK(Throws([removedMasterThickness] {
            ParseConfig(removedMasterThickness);
        }));
    }
    const auto markerThicknessBoundaries = ParseConfig(R"toml(
schema_version = 9
[monsters.normal]
shape = "x"
thickness = 1
[monsters.unique]
shape = "player_cross"
thickness = 5
)toml");
    CHECK(markerThicknessBoundaries.monsters.normal.thickness == 1.0F);
    CHECK(markerThicknessBoundaries.monsters.unique.thickness == 5.0F);
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 9\n[monsters.normal]\n"
            "shape = \"x\"\nthickness = 0.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 9\n[monsters.normal]\n"
            "shape = \"player_cross\"\nthickness = 5.5");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 9\n[monsters.normal]\n"
            "shape = \"dot\"\nthickness = 2");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 3\n[monsters.normal]\nsize = 41");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 4\n[monsters]\nnormal = false");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 4\n[monsters.normal]\nshape = \"unknown\"");
    }));
    CHECK(Throws([] {
        ParseConfig(
            "schema_version = 4\n[monsters.normal]\nshape = 1");
    }));

    const auto normal = MakeMonsterMarker(MonsterRank::Normal, Vec2{10.0F, 20.0F});
    const auto minion = MakeMonsterMarker(MonsterRank::Minion, Vec2{10.0F, 20.0F});
    const auto champion = MakeMonsterMarker(MonsterRank::Champion, Vec2{10.0F, 20.0F});
    const auto unique = MakeMonsterMarker(MonsterRank::Unique, Vec2{10.0F, 20.0F});
    const auto superUnique = MakeMonsterMarker(MonsterRank::SuperUnique, Vec2{10.0F, 20.0F});
    CHECK(BuildMonsterOutline(normal).size() == 24U);
    CHECK(BuildMonsterOutline(minion).size() == 3U);
    CHECK(BuildMonsterOutline(champion).size() == 4U);
    CHECK(BuildMonsterOutline(unique).size() == 10U);
    CHECK(BuildMonsterOutline(superUnique).size() == 6U);
    CHECK(normal.stroke == ScenePalette::MonsterNormal);
    CHECK(superUnique.stroke == ScenePalette::MonsterSuperUnique);
    CHECK(normal.fill.alpha == 64U);

    const Vec2 crossCenter{10.0F, 20.0F};
    const auto cross = BuildCrossOutline(crossCenter, 8.0F);
    static_assert(std::tuple_size_v<decltype(cross)>
        == CrossOutlinePointCount);
    CHECK(cross.size() == 13U);
    CHECK(cross.front() == cross.back());
    for (std::size_t index = 1U; index < cross.size(); ++index) {
        CHECK(cross[index - 1U] != cross[index]);
        CHECK(std::isfinite(cross[index].x));
        CHECK(std::isfinite(cross[index].y));
    }
    for (std::size_t index = 0U; index < 6U; ++index) {
        const auto& opposite = cross[index + 6U];
        CHECK(std::abs((cross[index].x + opposite.x)
            - (2.0F * crossCenter.x)) < 0.0001F);
        CHECK(std::abs((cross[index].y + opposite.y)
            - (2.0F * crossCenter.y)) < 0.0001F);
    }

    const auto playerCross = BuildPlayerCrossOutline(crossCenter, 20.0F);
    static_assert(std::tuple_size_v<decltype(playerCross)>
        == CrossOutlinePointCount);
    constexpr CrossOutline expectedPlayerCross{{
        Vec2{0.0F, 18.0F},
        Vec2{6.0F, 15.0F},
        Vec2{10.0F, 18.0F},
        Vec2{14.0F, 15.0F},
        Vec2{20.0F, 18.0F},
        Vec2{14.0F, 20.0F},
        Vec2{20.0F, 22.0F},
        Vec2{14.0F, 25.0F},
        Vec2{10.0F, 22.0F},
        Vec2{6.0F, 25.0F},
        Vec2{0.0F, 22.0F},
        Vec2{6.0F, 20.0F},
        Vec2{0.0F, 18.0F},
    }};
    CHECK(playerCross == expectedPlayerCross);

    constexpr std::array immunities{
        Element::Physical,
        Element::Fire,
        Element::Cold,
        Element::Lightning,
    };
    const auto immunityRing = MakeImmunityRing(
        Vec2{50.0F, 75.0F},
        8.0F,
        11.0F,
        immunities,
        0.1F);
    CHECK(immunityRing.arcs.size() == immunities.size());
    CHECK(immunityRing.arcs[0].color == ScenePalette::Physical);
    CHECK(immunityRing.arcs[1].color == ScenePalette::Fire);
    CHECK(immunityRing.arcs[2].color == ScenePalette::Cold);
    CHECK(immunityRing.arcs[3].color == ScenePalette::Lightning);
    CHECK(immunityRing.arcs[0].endRadians > immunityRing.arcs[0].startRadians);

    const auto fireMissile = MakeMissileMarker(
        Element::Fire,
        Vec2{20.0F, 30.0F},
        Vec2{1.0F, -0.25F},
        18.0F,
        3.0F);
    CHECK(fireMissile.color == ScenePalette::Fire);
    CHECK(fireMissile.length == 18.0F);
    CHECK(fireMissile.radius == 3.0F);

    const auto preview = BuildDiagnosticPreview(1280U, 720U, 42U);
    CHECK(preview.sequence == 42U);
    CHECK(preview.viewportWidth == 1280U);
    CHECK(preview.viewportHeight == 720U);
    CHECK(preview.primitives.size() == 13U);
    std::array<std::size_t, std::variant_size_v<Primitive>> primitiveCounts{};
    for (const auto& primitive : preview.primitives) {
        ++primitiveCounts[primitive.index()];
        Validate(primitive);
    }
    CHECK(primitiveCounts[0] == 1U); // line
    CHECK(primitiveCounts[1] == 1U); // circle
    CHECK(primitiveCounts[2] == 1U); // polygon
    CHECK(primitiveCounts[3] == 5U); // all monster ranks
    CHECK(primitiveCounts[4] == 1U); // segmented immunity ring
    CHECK(primitiveCounts[5] == 2U); // missiles
    CHECK(primitiveCounts[6] == 2U); // labels

    SceneExchange exchange;
    CHECK(!exchange.Acquire());
    const auto published = exchange.Publish(preview);
    CHECK(published);
    CHECK(published->sequence == 42U);
    CHECK(exchange.Acquire() == published);
    const auto next = exchange.Publish(BuildDiagnosticPreview(1920U, 1080U, 43U));
    CHECK(next->sequence == 43U);
    CHECK(exchange.Acquire() == next);
    CHECK(published->sequence == 42U);
    exchange.Clear();
    CHECK(!exchange.Acquire());

    CHECK(Throws([] { static_cast<void>(BuildDiagnosticPreview(0U, 720U)); }));
    CHECK(Throws([] {
        static_cast<void>(BuildCrossOutline(Vec2{}, 0.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(BuildCrossOutline(
            Vec2{std::numeric_limits<float>::infinity(), 0.0F},
            5.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(BuildPlayerCrossOutline(Vec2{}, 0.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(BuildPlayerCrossOutline(
            Vec2{},
            std::numeric_limits<float>::infinity()));
    }));
    CHECK(Throws([] {
        static_cast<void>(BuildPlayerCrossOutline(
            Vec2{},
            std::numeric_limits<float>::quiet_NaN()));
    }));
    CHECK(Throws([] {
        static_cast<void>(BuildPlayerCrossOutline(
            Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0F},
            20.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(MakeMonsterMarker(MonsterRank::Normal, Vec2{}, 0.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(MakeMissileMarker(Element::Magic, Vec2{}, Vec2{}));
    }));
    CHECK(Throws([] {
        static_cast<void>(MakeMonsterMarker(
            static_cast<MonsterRank>(255U),
            Vec2{},
            5.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(MakeMissileMarker(
            static_cast<Element>(255U),
            Vec2{},
            Vec2{1.0F, 0.0F}));
    }));
    CHECK(Throws([] {
        constexpr std::array duplicate{Element::Fire, Element::Fire};
        static_cast<void>(MakeImmunityRing(Vec2{}, 8.0F, 10.0F, duplicate));
    }));
    CHECK(Throws([] {
        constexpr std::array one{Element::Poison};
        static_cast<void>(MakeImmunityRing(Vec2{}, 10.0F, 8.0F, one));
    }));
    CHECK(Throws([] {
        SceneSnapshot invalid{
            .sequence = 1U,
            .viewportWidth = 800U,
            .viewportHeight = 600U,
            .primitives = {LinePrimitive{
                .start = Vec2{},
                .end = Vec2{std::numeric_limits<float>::infinity(), 0.0F},
            }},
        };
        Validate(invalid);
    }));

    static_assert(NativeSettingsTabs.size() == 5);
    static_assert(!MayTriggerMapSenseActionForVirtualKey(
        NativeAutomapVirtualKey));
    static_assert(MayTriggerMapSenseActionForVirtualKey(0x24U));
    CHECK(!MayTriggerMapSenseActionForVirtualKey(0x09U));
    CHECK(MayTriggerMapSenseActionForVirtualKey(0x24U));

    CHECK(std::abs(ComputeColoredImmunityIndicatorAdvance(8.0F) - 1.5F)
        < 0.0001F);
    CHECK(std::abs(ComputeColoredImmunityIndicatorAdvance(25.0F) - 3.0F)
        < 0.0001F);
    CHECK(std::abs(ComputeColoredImmunityIndicatorAdvance(32.0F) - 3.84F)
        < 0.0001F);
    CHECK(Throws([] {
        static_cast<void>(ComputeColoredImmunityIndicatorAdvance(0.0F));
    }));
    CHECK(Throws([] {
        static_cast<void>(ComputeColoredImmunityIndicatorAdvance(
            std::numeric_limits<float>::infinity()));
    }));

    static_assert(!ShouldForwardWin32MessageToImGui(WM_KEYDOWN, VK_TAB));
    static_assert(!ShouldForwardWin32MessageToImGui(WM_KEYUP, VK_TAB));
    static_assert(!ShouldForwardWin32MessageToImGui(WM_SYSKEYDOWN, VK_TAB));
    static_assert(!ShouldForwardWin32MessageToImGui(WM_SYSKEYUP, VK_TAB));
    static_assert(!ShouldForwardWin32MessageToImGui(WM_CHAR, L'\t'));
    static_assert(!ShouldForwardWin32MessageToImGui(WM_SYSCHAR, L'\t'));
    CHECK(!ShouldForwardWin32MessageToImGui(WM_KEYDOWN, VK_TAB));
    CHECK(!ShouldForwardWin32MessageToImGui(WM_KEYUP, VK_TAB));
    CHECK(!ShouldForwardWin32MessageToImGui(WM_SYSKEYDOWN, VK_TAB));
    CHECK(!ShouldForwardWin32MessageToImGui(WM_SYSKEYUP, VK_TAB));
    CHECK(!ShouldForwardWin32MessageToImGui(WM_CHAR, L'\t'));
    CHECK(!ShouldForwardWin32MessageToImGui(WM_SYSCHAR, L'\t'));
    CHECK(ShouldForwardWin32MessageToImGui(WM_KEYDOWN, VK_HOME));
    CHECK(ShouldForwardWin32MessageToImGui(WM_CHAR, L'x'));
    CHECK(ShouldForwardWin32MessageToImGui(WM_MOUSEMOVE, 0U));
    constexpr auto RepeatedKeyState = static_cast<LPARAM>(
        std::uint64_t{1} << 30U);
    static_assert(IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_TAB, 0));
    static_assert(IsInitialOwnedOverlayDismissalMessage(
        WM_SYSKEYDOWN, VK_TAB, 0));
    static_assert(IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_ESCAPE, 0));
    static_assert(IsInitialOwnedOverlayDismissalMessage(
        WM_SYSKEYDOWN, VK_ESCAPE, 0));
    static_assert(!IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_TAB, RepeatedKeyState));
    static_assert(!IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_ESCAPE, RepeatedKeyState));
    static_assert(!IsInitialOwnedOverlayDismissalMessage(
        WM_KEYUP, VK_TAB, 0));
    static_assert(!IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_HOME, 0));
    CHECK(IsInitialOwnedOverlayDismissalMessage(WM_KEYDOWN, VK_TAB, 0));
    CHECK(IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_ESCAPE, 0));
    CHECK(!IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_TAB, RepeatedKeyState));
    CHECK(!IsInitialOwnedOverlayDismissalMessage(
        WM_KEYDOWN, VK_ESCAPE, RepeatedKeyState));
    CHECK(!IsInitialOwnedOverlayDismissalMessage(WM_KEYUP, VK_TAB, 0));
    static_assert(!ShouldSubmitD3D12DrawData(0, 0));
    static_assert(!ShouldSubmitD3D12DrawData(1, 0));
    static_assert(!ShouldSubmitD3D12DrawData(0, 3));
    static_assert(ShouldSubmitD3D12DrawData(1, 3));
    CHECK(!ShouldSubmitD3D12DrawData(0, 0));
    CHECK(!ShouldSubmitD3D12DrawData(1, 0));
    CHECK(ShouldSubmitD3D12DrawData(1, 3));
    constexpr auto regularChestPlacement =
        ComputePrimeMhChestImagePlacement(100.0F, 200.0F, 58.0F, false);
    static_assert(regularChestPlacement.left == 71.0F);
    static_assert(regularChestPlacement.top == 175.0F);
    static_assert(regularChestPlacement.right == 129.0F);
    static_assert(regularChestPlacement.bottom == 225.0F);
    constexpr auto specialChestPlacement =
        ComputePrimeMhChestImagePlacement(100.0F, 200.0F, 58.0F, true);
    static_assert(specialChestPlacement.left == 64.0F);
    static_assert(specialChestPlacement.top == 115.0F);
    static_assert(specialChestPlacement.right == 133.0F);
    static_assert(specialChestPlacement.bottom == 225.0F);
    CHECK(regularChestPlacement.bottom == specialChestPlacement.bottom);
    static_assert(NativeSettingsTabs[0].tab == NativeSettingsTab::Map);
    static_assert(NativeSettingsTabs[1].tab == NativeSettingsTab::Monsters);
    static_assert(NativeSettingsTabs[2].tab == NativeSettingsTab::Navigation);
    static_assert(NativeSettingsTabs[3].tab == NativeSettingsTab::Projectiles);
    static_assert(NativeSettingsTabs[4].tab == NativeSettingsTab::System);
    CHECK(NativeSettingsTabs[0].label == "Map");
    CHECK(NativeSettingsTabs[2].label == "Navigation");
    CHECK(NativeSettingsTabs[3].label == "Projectiles");
    CHECK(NativeSettingsTabs[0].hasPersistentSettings);
    CHECK(NativeSettingsTabs[1].hasPersistentSettings);
    CHECK(!NativeSettingsTabs[2].hasPersistentSettings);
    CHECK(!NativeSettingsTabs[3].hasPersistentSettings);
    CHECK(NativeSettingsTabs[4].hasPersistentSettings);

    Config policyConfig{};
    auto dotStyleWithHiddenThickness = policyConfig.monsters.normal;
    dotStyleWithHiddenThickness.shape = MonsterMarkerShape::Dot;
    auto sameDotStyle = dotStyleWithHiddenThickness;
    sameDotStyle.thickness = MaximumMonsterMarkerThickness;
    CHECK(SameMonsterMarkerStyle(dotStyleWithHiddenThickness, sameDotStyle));
    sameDotStyle.showNames = !dotStyleWithHiddenThickness.showNames;
    CHECK(!SameMonsterMarkerStyle(dotStyleWithHiddenThickness, sameDotStyle));
    sameDotStyle.showNames = dotStyleWithHiddenThickness.showNames;
    sameDotStyle.nameColor = ParseColor("#12345678");
    CHECK(!SameMonsterMarkerStyle(dotStyleWithHiddenThickness, sameDotStyle));
    sameDotStyle.nameColor = dotStyleWithHiddenThickness.nameColor;
    sameDotStyle.nameSize += 1.0F;
    CHECK(!SameMonsterMarkerStyle(dotStyleWithHiddenThickness, sameDotStyle));
    sameDotStyle.nameSize = dotStyleWithHiddenThickness.nameSize;
    dotStyleWithHiddenThickness.shape = MonsterMarkerShape::X;
    sameDotStyle.shape = MonsterMarkerShape::X;
    CHECK(!SameMonsterMarkerStyle(dotStyleWithHiddenThickness, sameDotStyle));
    policyConfig.overlay.followNativeAutomap = false;
    for (const auto key : NativeSettingsToggleKeys) {
        WriteToggle(policyConfig, key, false);
        CHECK(!ReadToggle(policyConfig, key));
        WriteToggle(policyConfig, key, true);
        CHECK(ReadToggle(policyConfig, key));
        CHECK(!ToggleLabel(key).empty());
    }
    CHECK(TabForToggle(ToggleKey::MapSenseEnabled) == NativeSettingsTab::Map);
    CHECK(TabForToggle(ToggleKey::MonstersEnabled)
        == NativeSettingsTab::Monsters);
    CHECK(TabForToggle(ToggleKey::ImmunitiesEnabled)
        == NativeSettingsTab::Monsters);
    CHECK(TabForToggle(ToggleKey::DiagnosticPreview)
        == NativeSettingsTab::System);
    CHECK(TabForToggle(ToggleKey::DiagnosticsEnabled) == NativeSettingsTab::System);
    CHECK(!policyConfig.overlay.followNativeAutomap);

    SetOverlayOpacity(policyConfig, OverlayOpacityChoice::Low);
    CHECK(policyConfig.overlay.opacity == 0.25F);
    SetOverlayOpacity(policyConfig, OverlayOpacityChoice::Opaque);
    CHECK(policyConfig.overlay.opacity == 1.00F);
    CHECK(NearestOverlayOpacityChoice(0.88F)
        == OverlayOpacityChoice::NearOpaque);
    CHECK(NearestOverlayOpacityChoice(0.625F)
        == OverlayOpacityChoice::Medium);
    CHECK(NearestOverlayOpacityChoice(
        std::numeric_limits<float>::infinity())
        == OverlayOpacityChoice::NearOpaque);
    CHECK(OverlayOpacityLabel(OverlayOpacityChoice::NearOpaque) == "90%");

    static_assert(ImmunityDisplayModes.size() == 3);
    WriteImmunityDisplayMode(
        policyConfig,
        ImmunityDisplayMode::SplitHalo);
    CHECK(policyConfig.immunities.enabled);
    CHECK(policyConfig.immunities.style
        == ImmunityDisplayStyle::SplitHalo);
    CHECK(ReadImmunityDisplayMode(policyConfig)
        == ImmunityDisplayMode::SplitHalo);
    WriteImmunityDisplayMode(policyConfig, ImmunityDisplayMode::Off);
    CHECK(!policyConfig.immunities.enabled);
    CHECK(policyConfig.immunities.style
        == ImmunityDisplayStyle::SplitHalo);
    CHECK(ReadImmunityDisplayMode(policyConfig) == ImmunityDisplayMode::Off);
    WriteImmunityDisplayMode(
        policyConfig,
        ImmunityDisplayMode::ColoredI);
    CHECK(policyConfig.immunities.enabled);
    CHECK(policyConfig.immunities.style
        == ImmunityDisplayStyle::ColoredI);
    CHECK(ReadImmunityDisplayMode(policyConfig)
        == ImmunityDisplayMode::ColoredI);
    CHECK(ImmunityDisplayModeLabel(ImmunityDisplayMode::ColoredI)
        == "Colored i");
    CHECK(ImmunityDisplayModeLabel(ImmunityDisplayMode::SplitHalo)
        == "Split halo");

    ApplyImmunityPalette(policyConfig, ImmunityPalette::Classic);
    CHECK(DetectImmunityPalette(policyConfig) == ImmunityPalette::Classic);
    CHECK(ColorToHex(policyConfig.immunities.physical) == "#D8C39AFF");
    const auto classicFire = policyConfig.immunities.fire;
    ApplyImmunityPalette(policyConfig, ImmunityPalette::HighContrast);
    CHECK(DetectImmunityPalette(policyConfig)
        == ImmunityPalette::HighContrast);
    CHECK(!SameColor(policyConfig.immunities.fire, classicFire));
    ApplyImmunityPalette(policyConfig, ImmunityPalette::ColorBlindSafe);
    CHECK(DetectImmunityPalette(policyConfig)
        == ImmunityPalette::ColorBlindSafe);
    CHECK(ImmunityPaletteLabel(ImmunityPalette::ColorBlindSafe)
        == "Color-blind safe");
    policyConfig.immunities.magic.red = 0.123F;
    CHECK(!DetectImmunityPalette(policyConfig).has_value());

    const auto persistedDefaults = ParseConfig(SerializeConfig(Config{}));
    CHECK(DetectImmunityPalette(persistedDefaults)
        == ImmunityPalette::Classic);

    Config appliedSettings{};
    appliedSettings.enabled = false;
    appliedSettings.diagnostics = true;
    appliedSettings.overlay.diagnosticPreview = true;
    appliedSettings.overlay.opacity = 0.50F;
    appliedSettings.overlay.scale = 1.25F;
    appliedSettings.overlay.frameRate = 37;
    appliedSettings.overlay.followNativeAutomap = false;
    appliedSettings.monsters.enabled = false;
    appliedSettings.monsters.normal.thickness = 3.0F;
    appliedSettings.monsters.normal.color = ParseColor("#ABCDEFCC");
    appliedSettings.monsters.normal.size = 14.0F;
    appliedSettings.monsters.superUniqueBoss.showNames = false;
    appliedSettings.monsters.superUniqueBoss.nameColor = ParseColor("#12345678");
    appliedSettings.monsters.superUniqueBoss.nameSize = 21.0F;
    appliedSettings.missiles.enabled = false;
    appliedSettings.missiles.fire.color = ParseColor("#10203040");
    appliedSettings.missiles.fire.size = 9.0F;
    appliedSettings.objects.enabled = false;
    appliedSettings.objects.exitLabels.enabled = false;
    appliedSettings.objects.exitLabels.color = ParseColor("#ABC123EF");
    appliedSettings.objects.exitLabels.size = 11.0F;
    appliedSettings.objects.waypointLabels.enabled = false;
    appliedSettings.objects.waypointLabels.color = ParseColor("#11223344");
    appliedSettings.objects.waypointLabels.size = 19.0F;
    appliedSettings.objects.shrineLabels.enabled = false;
    appliedSettings.objects.chests.lockedAccentColor =
        ParseColor("#2468ACE0");
    appliedSettings.objects.superChests.starsEnabled = false;
    appliedSettings.objects.superChests.starsColor = ParseColor("#50607080");
    appliedSettings.objects.superChests.starsSize = 31.0F;
    appliedSettings.objects.armorRacks.size = 8.0F;
    appliedSettings.objects.weaponRacks.color = ParseColor("#D0E0F0FF");
    appliedSettings.hud.sessionTimer = true;
    appliedSettings.menu.showLauncher = false;
    ApplyImmunityPalette(appliedSettings, ImmunityPalette::HighContrast);

    NativeSettingsDraft draftModel(appliedSettings);
    CHECK(!draftModel.Dirty());
    WriteToggle(
        draftModel.Draft(),
        ToggleKey::MapSenseEnabled,
        true);
    SetOverlayOpacity(
        draftModel.Draft(),
        OverlayOpacityChoice::Opaque);
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    CHECK(!draftModel.Dirty());
    CHECK(!draftModel.Draft().enabled);
    CHECK(!draftModel.Draft().monsters.enabled);
    CHECK(draftModel.Draft().overlay.opacity == 0.50F);
    CHECK(draftModel.Draft().monsters.normal.thickness == 3.0F);
    CHECK(draftModel.Draft().monsters.normal.size == 14.0F);
    CHECK(ColorToHex(draftModel.Draft().monsters.normal.color)
        == "#ABCDEFCC");
    CHECK(!draftModel.Draft().monsters.superUniqueBoss.showNames);
    CHECK(ColorToHex(draftModel.Draft().monsters.superUniqueBoss.nameColor)
        == "#12345678");
    CHECK(draftModel.Draft().monsters.superUniqueBoss.nameSize == 21.0F);
    CHECK(!draftModel.Draft().missiles.enabled);
    CHECK(ColorToHex(draftModel.Draft().missiles.fire.color)
        == "#10203040");
    CHECK(draftModel.Draft().missiles.fire.size == 9.0F);
    CHECK(!draftModel.Draft().objects.enabled);
    CHECK(!draftModel.Draft().objects.exitLabels.enabled);
    CHECK(ColorToHex(draftModel.Draft().objects.exitLabels.color)
        == "#ABC123EF");
    CHECK(!draftModel.Draft().objects.waypointLabels.enabled);
    CHECK(ColorToHex(draftModel.Draft().objects.waypointLabels.color)
        == "#11223344");
    CHECK(draftModel.Draft().objects.waypointLabels.size == 19.0F);
    CHECK(!draftModel.Draft().objects.superChests.starsEnabled);
    CHECK(ColorToHex(draftModel.Draft().objects.superChests.starsColor)
        == "#50607080");

    draftModel.Draft().immunities.style = ImmunityDisplayStyle::SplitHalo;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    CHECK(!draftModel.Dirty());
    draftModel.Draft().immunities.indicatorSize = 21.0F;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().immunities.haloThickness = 4.0F;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().monsters.superUniqueBoss.showNames = true;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().missiles.enabled = true;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().missiles.lightning.size = 8.0F;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().objects.enabled = true;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().objects.exitLabels.size = 17.0F;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().objects.waypointLabels.enabled = true;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().objects.superChests.starsEnabled = true;
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().objects.superChests.starsColor = ParseColor("#FFFFFFFF");
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    draftModel.Draft().objects.weaponRacks.color = ParseColor("#000000FF");
    CHECK(draftModel.Dirty());
    draftModel.Discard();
    CHECK(!draftModel.Dirty());

    draftModel.ResetToDefaults();
    CHECK(draftModel.Dirty());
    CHECK(draftModel.Draft().enabled);
    CHECK(draftModel.Draft().monsters.enabled);
    CHECK(draftModel.Draft().overlay.opacity == 1.0F);
    CHECK(draftModel.Draft().overlay.scale == 1.0F);
    CHECK(!draftModel.Draft().overlay.diagnosticPreview);
    CHECK(draftModel.Draft().monsters.normal.thickness == 2.0F);
    CHECK(draftModel.Draft().monsters.normal.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(draftModel.Draft().monsters.minion.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(draftModel.Draft().monsters.champion.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(draftModel.Draft().monsters.unique.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(draftModel.Draft().monsters.superUniqueBoss.shape
        == MonsterMarkerShape::PlayerCross);
    CHECK(draftModel.Draft().monsters.normal.size == 18.0F);
    CHECK(draftModel.Draft().monsters.minion.size == 18.0F);
    CHECK(draftModel.Draft().monsters.champion.size == 20.0F);
    CHECK(draftModel.Draft().monsters.unique.size == 22.0F);
    CHECK(draftModel.Draft().monsters.superUniqueBoss.size == 24.0F);
    CHECK(draftModel.Draft().monsters.superUniqueBoss.showNames);
    CHECK(ColorToHex(draftModel.Draft().monsters.superUniqueBoss.nameColor)
        == "#FFD33DFF");
    CHECK(draftModel.Draft().monsters.superUniqueBoss.nameSize
        == DefaultAutomapLabelSize);
    CHECK(draftModel.Draft().missiles.enabled);
    CHECK(ColorToHex(draftModel.Draft().missiles.fire.color)
        == "#FF00007F");
    CHECK(draftModel.Draft().missiles.fire.size
        == DefaultMissileMarkerSize);
    CHECK(draftModel.Draft().immunities.style
        == ImmunityDisplayStyle::ColoredI);
    CHECK(draftModel.Draft().immunities.indicatorSize
        == DefaultImmunityIndicatorSize);
    CHECK(draftModel.Draft().immunities.haloThickness
        == DefaultImmunityHaloThickness);
    CHECK(ColorToHex(draftModel.Draft().immunities.physical)
        == "#D8C39AFF");
    CHECK(draftModel.Draft().objects.enabled);
    CHECK(draftModel.Draft().objects.exitLabels.enabled);
    CHECK(ColorToHex(draftModel.Draft().objects.exitLabels.color)
        == "#FFD33DFF");
    CHECK(draftModel.Draft().objects.waypointLabels.enabled);
    CHECK(ColorToHex(draftModel.Draft().objects.waypointLabels.color)
        == "#FFD33DFF");
    CHECK(draftModel.Draft().objects.waypointLabels.size
        == DefaultAutomapLabelSize);
    CHECK(draftModel.Draft().objects.superChests.starsEnabled);
    CHECK(ColorToHex(draftModel.Draft().objects.superChests.starsColor)
        == "#FFD33DFF");
    CHECK(draftModel.Draft().objects.superChests.starsSize
        == DefaultAutomapLabelSize);
    CHECK(draftModel.Draft().overlay.frameRate == 37);
    CHECK(!draftModel.Draft().overlay.followNativeAutomap);
    CHECK(draftModel.Draft().hud.sessionTimer);
    CHECK(!draftModel.Draft().menu.showLauncher);

    const auto& newlyApplied = draftModel.Apply();
    CHECK(!draftModel.Dirty());
    CHECK(newlyApplied.enabled);
    CHECK(newlyApplied.overlay.opacity == 1.0F);
    CHECK(newlyApplied.overlay.scale == 1.0F);
    CHECK(newlyApplied.monsters.enabled);
    CHECK(!newlyApplied.overlay.diagnosticPreview);
    CHECK(newlyApplied.overlay.frameRate == 37);
    CHECK(!newlyApplied.overlay.followNativeAutomap);
    CHECK(newlyApplied.monsters.normal.thickness == 2.0F);
    CHECK(newlyApplied.monsters.normal.size == 18.0F);
    CHECK(newlyApplied.monsters.superUniqueBoss.showNames);
    CHECK(newlyApplied.missiles.enabled);
    CHECK(ColorToHex(newlyApplied.missiles.fire.color) == "#FF00007F");
    CHECK(newlyApplied.objects.enabled);
    CHECK(newlyApplied.objects.exitLabels.enabled);
    CHECK(newlyApplied.objects.waypointLabels.enabled);
    CHECK(newlyApplied.objects.superChests.starsEnabled);
    CHECK(newlyApplied.hud.sessionTimer);
    CHECK(!newlyApplied.menu.showLauncher);

    if (argc >= 2) {
        try {
            const auto shippedText = ReadFile(argv[1]);
            const auto shippedDocument = toml::parse(shippedText);
            const auto shippedSchema = shippedDocument["schema_version"]
                .value<std::int64_t>();
            CHECK(shippedSchema.has_value());
            CHECK(shippedSchema.value_or(0) == CurrentConfigSchemaVersion);
            const auto shipped = ParseConfig(shippedDocument);
            CHECK(shipped.enabled);
            CHECK(!shipped.diagnostics);
            CHECK(shipped.overlay.opacity == 1.0F);
            CHECK(shippedText.find("detection_radius") == std::string::npos);
            CHECK(shippedText.find("marker_thickness") == std::string::npos);
            CHECK(shippedText.find("features_enabled") == std::string::npos);
            CHECK(shippedText.find("[overlay]\n# Enables")
                == std::string::npos);
            CHECK(shipped.monsters.normal.shape
                == MonsterMarkerShape::PlayerCross);
            CHECK(shipped.monsters.enabled);
            CHECK(shipped.monsters.minion.shape
                == MonsterMarkerShape::PlayerCross);
            CHECK(shipped.monsters.champion.shape
                == MonsterMarkerShape::PlayerCross);
            CHECK(shipped.monsters.unique.shape
                == MonsterMarkerShape::PlayerCross);
            CHECK(shipped.monsters.superUniqueBoss.shape
                == MonsterMarkerShape::PlayerCross);
            CHECK(shipped.monsters.normal.size == 18.0F);
            CHECK(shipped.monsters.minion.size == 18.0F);
            CHECK(shipped.monsters.champion.size == 20.0F);
            CHECK(shipped.monsters.unique.size == 22.0F);
            CHECK(shipped.monsters.superUniqueBoss.size == 24.0F);
            CHECK(shipped.monsters.normal.thickness == 2.0F);
            CHECK(shipped.monsters.minion.thickness == 2.0F);
            CHECK(shipped.monsters.champion.thickness == 2.0F);
            CHECK(shipped.monsters.unique.thickness == 2.0F);
            CHECK(shipped.monsters.superUniqueBoss.thickness == 2.0F);
            CHECK(ColorToHex(shipped.monsters.normal.color) == "#FFFFFFFF");
            CHECK(ColorToHex(shipped.monsters.minion.color) == "#FFD43BFF");
            CHECK(ColorToHex(shipped.monsters.champion.color) == "#3D8BFFFF");
            CHECK(ColorToHex(shipped.monsters.unique.color) == "#FF8A24FF");
            CHECK(ColorToHex(shipped.monsters.superUniqueBoss.color)
                == "#FF3B30FF");
            CHECK(shipped.monsters.superUniqueBoss.showNames);
            CHECK(ColorToHex(shipped.monsters.superUniqueBoss.nameColor)
                == "#FFD33DFF");
            CHECK(shipped.monsters.superUniqueBoss.nameSize
                == DefaultAutomapLabelSize);
            CHECK(shipped.immunities.enabled);
            CHECK(shipped.immunities.style
                == ImmunityDisplayStyle::ColoredI);
            CHECK(shipped.immunities.indicatorSize
                == DefaultImmunityIndicatorSize);
            CHECK(shipped.immunities.haloThickness
                == DefaultImmunityHaloThickness);
            CHECK(ColorToHex(shipped.immunities.physical)
                == "#D8C39AFF");
            CHECK(shipped.missiles.enabled);
            CHECK(ColorToHex(shipped.missiles.fire.color)
                == "#FF00007F");
            CHECK(ColorToHex(shipped.missiles.cold.color)
                == "#00D0FF7F");
            CHECK(ColorToHex(shipped.missiles.lightning.color)
                == "#FFFF0046");
            CHECK(ColorToHex(shipped.missiles.poison.color)
                == "#32CD327F");
            CHECK(ColorToHex(shipped.missiles.physical.color)
                == "#CD853F7F");
            CHECK(ColorToHex(shipped.missiles.magic.color)
                == "#FF88007F");
            CHECK(shipped.missiles.fire.size
                == DefaultMissileMarkerSize);
            CHECK(shipped.missiles.magic.size
                == DefaultMissileMarkerSize);
            CHECK(shippedText.find("Every monster category")
                == std::string::npos);
            CHECK(shippedText.find("Shows the current area's localized name")
                == std::string::npos);
            CHECK(shippedText.find("exact PrimeMH texture")
                == std::string::npos);
            CHECK(shipped.objects.enabled);
            CHECK(shipped.objects.exitLabels.enabled);
            CHECK(ColorToHex(shipped.objects.exitLabels.color)
                == "#FFD33DFF");
            CHECK(shipped.objects.exitLabels.size
                == DefaultAutomapLabelSize);
            CHECK(shipped.objects.waypointLabels.enabled);
            CHECK(ColorToHex(shipped.objects.waypointLabels.color)
                == "#FFD33DFF");
            CHECK(shipped.objects.waypointLabels.size
                == DefaultAutomapLabelSize);
            CHECK(shippedText.find("[objects.waypoint_labels]")
                != std::string::npos);
            CHECK(shipped.objects.shrineLabels.enabled);
            CHECK(ColorToHex(shipped.objects.shrineLabels.color)
                == "#FFD33DFF");
            CHECK(shipped.objects.chests.enabled);
            CHECK(ColorToHex(shipped.objects.chests.outlineColor)
                == "#1450ADFF");
            CHECK(ColorToHex(shipped.objects.chests.interiorColor)
                == "#B88A2AB0");
            CHECK(ColorToHex(shipped.objects.chests.lockedAccentColor)
                == "#00FFFFFF");
            CHECK(ColorToHex(shipped.objects.chests.trappedAccentColor)
                == "#FF3B30FF");
            CHECK(shipped.objects.superChests.enabled);
            CHECK(shipped.objects.superChests.starsEnabled);
            CHECK(ColorToHex(shipped.objects.superChests.starsColor)
                == "#FFD33DFF");
            CHECK(shipped.objects.armorRacks.enabled);
            CHECK(shipped.objects.weaponRacks.enabled);
            CHECK(shipped.navigation.lineThickness == 2.0F);
            CHECK(shipped.navigation.waypoint.enabled);
            CHECK(ColorToHex(shipped.navigation.waypoint.color)
                == "#3D8BFFFF");
            CHECK(shipped.navigation.progression.enabled);
            CHECK(ColorToHex(shipped.navigation.progression.color)
                == "#57E03DFF");
            CHECK(shipped.navigation.quests.enabled);
            CHECK(ColorToHex(shipped.navigation.quests.color)
                == "#FF3B30FF");
            CHECK(!shipped.navigation.customLevels.enabled);
            CHECK(ColorToHex(shipped.navigation.customLevels.color)
                == "#C75CFFFF");
            CHECK(shipped.navigation.customLevels.targets.size() == 4U);
            CHECK(std::get<std::int32_t>(
                shipped.navigation.customLevels.targets[0]) == 12);
            CHECK(std::get<std::string>(
                shipped.navigation.customLevels.targets[1])
                == "Mausoleum");
            CHECK(std::get<std::string>(
                shipped.navigation.customLevels.targets[2])
                == "Ancient Tunnels");
            CHECK(std::get<std::int32_t>(
                shipped.navigation.customLevels.targets[3]) == 119);
            CHECK(shipped.menu.showLauncher);
            CHECK(shipped.menu.theme == MenuTheme::SanctuaryGold);
            CHECK(!shipped.menu.startExpanded);
            CHECK(shipped.menu.rememberPosition);
            CHECK(shipped.menu.positionX == 0.86F);
            CHECK(shipped.menu.positionY == 0.04F);
            CHECK(!shipped.hud.mercenaryHealth);
            CHECK(!shipped.hud.sessionTimer);
            CHECK(!shipped.hud.experienceTracker);
        } catch (const std::exception& exception) {
            std::cerr << "FAIL shipped configuration: " << exception.what() << '\n';
            ++Failures;
        }
    }
    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
