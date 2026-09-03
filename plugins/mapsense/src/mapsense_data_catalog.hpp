#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

enum class DataCatalogFamily : std::uint8_t {
    Levels,
    Shrines,
    SuperUniques,
    MonStats,
    Objects,
    Missiles,
    Count,
};

enum class DataCatalogFamilyState : std::uint8_t {
    Unavailable,
    ActiveTxt,
    VanillaFallbackTxt,
    BinaryOnlyConflict,
    Invalid,
};

enum class DataCatalogDiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

struct DataCatalogLocalizedText final {
    // The exact key read from the active/fallback TXT table.
    std::string key{};
    // A session-owned UTF-8 copy. When localization is unavailable or the key
    // is absent, this retains the raw key and localized is false.
    std::string utf8{};
    bool localized{};
};

struct DataCatalogLevel final {
    std::int32_t id{};
    DataCatalogLocalizedText name{};
    // Optional placement metadata copied from the active Levels.txt. Older
    // fixtures and fallback tables may omit these columns; those records stay
    // valid for names while atlas placement fails closed.
    std::int32_t act{-1};
    std::int32_t layer{-1};
    std::array<std::int32_t, 3U> sizeX{{-1, -1, -1}};
    std::array<std::int32_t, 3U> sizeY{{-1, -1, -1}};
    std::int32_t offsetX{};
    std::int32_t offsetY{};
    std::int32_t depend{};
    std::int32_t drlgType{-1};
    bool hasAtlasPlacement{};
    // Levels.txt uses 255 for "no waypoint" and a smaller ordinal for every
    // data-defined waypoint. This lets passive level labels preserve waypoint
    // wording even when the exact PresetUnit chain has not materialized.
    bool hasWaypoint{};
    // Precomposed at catalog load so the render thread performs no string
    // allocation. D2R exposes localized level names but no standalone
    // localized "Waypoint" token in the active language catalog.
    std::string waypointLabelUtf8{};
};

struct DataCatalogLevelAtlasPlacement final {
    std::int32_t originSubtileX{};
    std::int32_t originSubtileY{};
    std::int32_t anchorSubtileX{};
    std::int32_t anchorSubtileY{};
};

struct DataCatalogShrine final {
    std::uint32_t row{};
    std::uint32_t code{};
    DataCatalogLocalizedText name{};
};

struct DataCatalogSuperUnique final {
    std::uint32_t hcIdx{};
    std::string classId{};
    DataCatalogLocalizedText name{};
};

struct DataCatalogMonStats final {
    std::uint32_t hcIdx{};
    std::string id{};
    DataCatalogLocalizedText name{};
    bool primeEvil{};
};

enum class DataCatalogMissileElement : std::uint8_t {
    Hidden,
    Physical,
    Fire,
    Cold,
    Lightning,
    Poison,
    Magic,
};

struct DataCatalogMissile final {
    std::uint32_t classId{};
    std::string id{};
    DataCatalogMissileElement element{DataCatalogMissileElement::Hidden};
};

// Combines PrimeMH's stock class taxonomy with the active mod's Missiles.txt.
// A changed/extended EType wins, then positive physical damage is the bounded
// fallback; unknown effect-only rows remain hidden.
[[nodiscard]] auto ClassifyDataCatalogMissileElement(
    std::uint32_t classId,
    std::string_view activeElement,
    bool hasPhysicalDamage) noexcept -> DataCatalogMissileElement;
[[nodiscard]] auto StockDataCatalogMissileElement(
    std::uint32_t classId) noexcept -> DataCatalogMissileElement;

struct DataCatalogObject final {
    std::uint32_t objectId{};
    std::string classId{};
    DataCatalogLocalizedText name{};
    std::uint32_t subClass{};
    bool lockable{};
    std::uint32_t operateFn{};
    std::uint32_t populateFn{};
    std::uint32_t initFn{};
    std::uint32_t clientFn{};
    std::uint32_t shrineFunction{};
};

// Generic engine contracts proven against the governed D2R 3.3 ObjectsTxt
// layout. They intentionally avoid object ids and localized display names so
// custom mod rows that reuse the native callbacks remain discoverable.
[[nodiscard]] constexpr auto IsShrineObjectDefinition(
        const DataCatalogObject& record) noexcept -> bool {
    // InitFn 1 is the native shrine initializer that stores the selected
    // ShrinesTxt row in ObjectData.InteractType. SubClass bit 0 distinguishes
    // real shrine objects from other definitions that reuse InitFn 1.
    return record.initFn == 1U && (record.subClass & 0x01U) != 0U;
}

[[nodiscard]] constexpr auto IsChestObjectDefinition(
        const DataCatalogObject& record) noexcept -> bool {
    return record.initFn == 3U || record.initFn == 57U;
}

[[nodiscard]] constexpr auto IsSpecialChestObjectDefinition(
        const DataCatalogObject& record) noexcept -> bool {
    // InitFn 57 is the data-driven special/sparkly chest constructor in the
    // active D2R Objects.txt. Keeping this semantic avoids object ids, class
    // names and level-specific exceptions, so mod-added rows inherit the same
    // behavior everywhere they reuse the native special-chest contract.
    if (IsChestObjectDefinition(record) && record.initFn == 57U) {
        return true;
    }
    // The four Arcane Sanctuary chest classes use the ordinary chest
    // initializer even though D2's object taxonomy treats them as special
    // chest POIs. Class is the stable semantic TXT key here; this is global
    // classification and never depends on the current level or numeric id.
    return record.initFn == 3U
        && (record.classId == "ArcaneChest1"
            || record.classId == "ArcaneChest2"
            || record.classId == "ArcaneChest3"
            || record.classId == "ArcaneChest4");
}

[[nodiscard]] inline auto IsSpecialChestPresetObjectDefinition(
        const DataCatalogObject& record) noexcept -> bool {
    // InitFn 79 is a generated-object placer, not a live chest. Only the
    // engine's immutable PlaceUniqueChest semantic key denotes a guaranteed
    // special/sparkly chest; PlaceRandomTreasureChest must remain ordinary.
    return record.initFn == 79U
        && record.classId == "PlaceUniqueChest";
}

struct DataCatalogFamilyStatus final {
    DataCatalogFamily family{DataCatalogFamily::Levels};
    DataCatalogFamilyState state{DataCatalogFamilyState::Unavailable};
    std::filesystem::path sourcePath{};
    std::size_t rowCount{};
    std::size_t localizedNameCount{};
    std::size_t unresolvedNameCount{};

    [[nodiscard]] constexpr auto Available() const noexcept -> bool {
        return state == DataCatalogFamilyState::ActiveTxt
            || state == DataCatalogFamilyState::VanillaFallbackTxt;
    }
};

struct DataCatalogDiagnostic final {
    DataCatalogDiagnosticSeverity severity{
        DataCatalogDiagnosticSeverity::Info};
    DataCatalogFamily family{DataCatalogFamily::Levels};
    std::string code{};
    std::string message{};
};

struct MapSenseDataCatalogLimits final {
    std::size_t maximumTableBytes{32U * 1'024U * 1'024U};
    std::size_t maximumLineBytes{2U * 1'024U * 1'024U};
    std::size_t maximumColumns{1'024U};
    std::size_t maximumRowsPerFamily{65'536U};
    std::size_t maximumKeyBytes{1'024U};
    std::size_t maximumLocalizedBytes{4'096U};
    std::size_t maximumDiagnostics{128U};
};

struct MapSenseDataCatalogLoadOptions final {
    // Optional, explicitly trusted vanilla TXT roots. Each path must directly
    // contain levels.txt, shrines.txt, superuniques.txt, monstats.txt,
    // objects.txt and/or missiles.txt. The loader also checks a packaged "vanilla-excel" folder
    // next to the plugin and "base" below active Excel roots when present.
    std::vector<std::filesystem::path> vanillaExcelDirectories{};
    MapSenseDataCatalogLimits limits{};
};

class MapSenseDataCatalog;

struct MapSenseDataCatalogLoadResult final {
    std::shared_ptr<const MapSenseDataCatalog> catalog{};
    std::vector<DataCatalogDiagnostic> diagnostics{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return catalog != nullptr;
    }
};

// Immutable after Load returns. All lookups are allocation-free const reads and
// are safe from any thread while the shared catalog remains alive. Load queries
// LocalizationServiceV1 and copies all names; call it during session setup,
// never from a Present callback.
class MapSenseDataCatalog final {
public:
    // Opaque implementation type. It is public only so the translation-unit
    // parser helpers can populate it; callers never receive a mutable handle.
    struct Impl;

    [[nodiscard]] static auto Load(
        const D2RL::PluginContext* context,
        const MapSenseDataCatalogLoadOptions& options = {}) noexcept
        -> MapSenseDataCatalogLoadResult;

    MapSenseDataCatalog(const MapSenseDataCatalog&) = default;
    MapSenseDataCatalog(MapSenseDataCatalog&&) noexcept = default;
    auto operator=(const MapSenseDataCatalog&)
        -> MapSenseDataCatalog& = default;
    auto operator=(MapSenseDataCatalog&&) noexcept
        -> MapSenseDataCatalog& = default;
    ~MapSenseDataCatalog();

    [[nodiscard]] auto FindLevel(std::int32_t id) const noexcept
        -> const DataCatalogLevel*;
    [[nodiscard]] auto Levels() const noexcept
        -> std::span<const DataCatalogLevel>;
    // Resolves fixed Levels.txt placement, including bounded Depend chains.
    // Dynamic (-1) coordinates/sizes, cycles and overflow are rejected rather
    // than guessed. Difficulty uses D2R's 0=Normal, 1=Nightmare, 2=Hell.
    [[nodiscard]] auto ResolveLevelAtlasPlacement(
        std::int32_t id,
        std::uint8_t difficulty,
        DataCatalogLevelAtlasPlacement& output) const noexcept -> bool;
    [[nodiscard]] auto FindShrineByRow(std::uint32_t row) const noexcept
        -> const DataCatalogShrine*;
    [[nodiscard]] auto FindShrineByInteractType(
            std::uint32_t interactType) const noexcept
            -> const DataCatalogShrine* {
        // ObjectData.InteractType stores the Shrines.txt row index. Row zero
        // is the engine-reserved "None" record and is never a displayable
        // shrine effect, even when a mod changes its Code or StringName.
        if (interactType == 0U) return nullptr;
        return FindShrineByRow(interactType);
    }
    // Code is not unique in modded Shrines.txt. This returns exact row ids;
    // resolve each through FindShrineByRow instead of choosing arbitrarily.
    [[nodiscard]] auto ShrineRowsForCode(std::uint32_t code) const noexcept
        -> std::span<const std::uint32_t>;
    [[nodiscard]] auto FindSuperUnique(std::uint32_t hcIdx) const noexcept
        -> const DataCatalogSuperUnique*;
    [[nodiscard]] auto FindMonStats(std::uint32_t hcIdx) const noexcept
        -> const DataCatalogMonStats*;
    [[nodiscard]] auto FindMonStatsByClassId(
            std::uint32_t classId) const noexcept
            -> const DataCatalogMonStats* {
        return FindMonStats(classId);
    }
    [[nodiscard]] auto FindObject(std::string_view classId) const noexcept
        -> const DataCatalogObject*;
    [[nodiscard]] auto FindObjectById(std::uint32_t objectId) const noexcept
        -> const DataCatalogObject*;
    [[nodiscard]] auto FindObjectByClassId(
            std::uint32_t classId) const noexcept
            -> const DataCatalogObject* {
        return FindObjectById(classId);
    }
    [[nodiscard]] auto FindMissile(std::uint32_t classId) const noexcept
        -> const DataCatalogMissile*;

    [[nodiscard]] auto FamilyStatus(DataCatalogFamily family) const noexcept
        -> const DataCatalogFamilyStatus&;
    [[nodiscard]] auto FamilyStatuses() const noexcept
        -> std::span<const DataCatalogFamilyStatus>;
    // Ordered roots resolved from D2RLoader's active mod. The external helper
    // searches them in the same precedence order and falls back to its
    // embedded vanilla data for every missing table/DS1.
    [[nodiscard]] auto ActiveExcelDirectories() const noexcept
        -> std::span<const std::filesystem::path>;
    [[nodiscard]] auto ActiveTileDirectories() const noexcept
        -> std::span<const std::filesystem::path>;
    // Stable session identity over the active table bytes plus bounded tile
    // metadata. Zero denotes the all-embedded vanilla source set.
    [[nodiscard]] auto AtlasDataFingerprint() const noexcept
        -> std::uint64_t;
    [[nodiscard]] auto HasLocalizationService() const noexcept -> bool;
    // True only after LocalizationService returned at least one player-facing
    // string that differs from its technical TXT key. This distinguishes an
    // initialized language table from the service's early key-echo behavior.
    [[nodiscard]] auto HasVerifiedPlayerFacingLocalization() const noexcept
        -> bool;
    [[nodiscard]] auto AllFamiliesAvailable() const noexcept -> bool;

private:
    explicit MapSenseDataCatalog(std::shared_ptr<const Impl> impl) noexcept;

    std::shared_ptr<const Impl> impl_{};
};

[[nodiscard]] auto DataCatalogFamilyName(DataCatalogFamily family) noexcept
    -> std::string_view;

} // namespace RuffnecKk::MapSense
