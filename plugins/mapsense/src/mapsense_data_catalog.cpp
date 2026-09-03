#include "mapsense_data_catalog.hpp"

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <system_error>
#include <utility>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::size_t FamilyCount =
    static_cast<std::size_t>(DataCatalogFamily::Count);
constexpr std::size_t HardMaximumTableBytes = 64U * 1'024U * 1'024U;
constexpr std::size_t HardMaximumLineBytes = 4U * 1'024U * 1'024U;
constexpr std::size_t HardMaximumColumns = 2'048U;
constexpr std::size_t HardMaximumRowsPerFamily = 131'072U;
constexpr std::size_t HardMaximumKeyBytes = 4'096U;
constexpr std::size_t HardMaximumLocalizedBytes = 65'536U;
constexpr std::size_t HardMaximumDiagnostics = 512U;
constexpr std::size_t MaximumDiagnosticMessageBytes = 1'024U;
constexpr std::size_t MaximumAtlasAssetFingerprintEntries = 65'536U;

const std::array<std::filesystem::path, 7U> AtlasTableNames{{
    L"levels.txt",
    L"lvlprest.txt",
    L"lvltypes.txt",
    L"lvlmaze.txt",
    L"lvlsub.txt",
    L"lvlwarp.txt",
    L"objects.txt",
}};

struct TableSpec final {
    DataCatalogFamily family{};
    std::filesystem::path txtName{};
    std::filesystem::path binName{};
};

const std::array<TableSpec, FamilyCount> TableSpecs{{
    {DataCatalogFamily::Levels, L"levels.txt", L"levels.bin"},
    {DataCatalogFamily::Shrines, L"shrines.txt", L"shrines.bin"},
    {DataCatalogFamily::SuperUniques, L"superuniques.txt",
        L"superuniques.bin"},
    {DataCatalogFamily::MonStats, L"monstats.txt", L"monstats.bin"},
    {DataCatalogFamily::Objects, L"objects.txt", L"objects.bin"},
    {DataCatalogFamily::Missiles, L"missiles.txt", L"missiles.bin"},
}};

// PrimeMH's pinned missile taxonomy is indexed by the immutable D2 missile
// class id. It is compressed to one byte per stock class: Y=physical,
// F=fire, C=cold/ice, L=lightning, P=poison, M=magic and H=non-gameplay
// SFX/trigger/dummy. The second table records the stock Missiles.txt EType.
// A modded EType overrides PrimeMH only when it differs from the stock row;
// appended custom rows are classified directly from their active TXT data.
constexpr std::string_view PrimeMissileTaxonomy =
    "YYLLLLLYYYYYFYYYYYHHHHFFFFFMCFCYPHFYYYPPCFFPFFFPPPHFFFFMLLFCFFFPPPPFFFPPPPPPHMMMMMFHHFMCCCLC"
    "MLHHCMLLFFFFFFCCCCCCCCMYPYPCYFCLLYYYYFFFFFFYFFFPHPMHFFPMMMHFPCYPHHCCCCCCCCLLLLLPLMHLFFLFFMMM"
    "MMMMMMMMMMCLYYYYYLLPMLLMPCFLPYYLMPMMPPPPPLYYYYFLLHMMMMFPFYYMMPPFMHHHHHHHHHLMCCCCCFYLMMMCCCCC"
    "CFMLFCPFHLLMMLMMHHHHHHHHFFHHHHHHFFFFHHHHHHHHLPHPFCHMHMHFHHHHFHHLLYYLCCYHHHHLLHHHHMHHHHHHHHHH"
    "HHHYHHHHHHHHHHHHHHHHHHYPYHHHHHFFLLLLFFLLLHYYYFYCCCPPPFFFHHHFCFHLLLCCPPLLLFFFYYYFFFLMFFFFFFFC"
    "CFFFFFYYYYHYYHPPHYYFFFFFFYYHFFFFFCCLYYHYYYYYYYYYYYYYLLMPPMMHYFYFFYLLHHHHMYYFHHHHHHHLLYHLLPMY"
    "YYFLFFLLCCCFFFFFLCHHHHLFYFHHHCCCCCCYYCCMMMMMMHHHHHHMMMHHHHHHHMMMMMMFMMMFCHHHHHHHHHHHHHHHMLLL"
    "YYYYPPFFMFLPFCYYYPCFLMLPPFFFFFFFFPLCLFYYYYMMMFFMMPMFPPPMMMMMMPMMFMMPPPMMFMMMMMFMMCCCCCFYFFFL";
constexpr std::string_view StockMissileElementTaxonomy =
    "__LLLLL_____F_________FFFFFM____P_______CF__FFFPPP_____M______FPPPP___PPPPP__MMMM_F__F______"
    "M___C__L____FF_CCCC__________FC_L____F_F____FFFP____FF_________P______________LPL___FFL_F___"
    "__________CL_______P_L__PCFLP_______P_PPPL____F_L_M___FPF______________________________C____"
    "CF_LFCPF__L__L______________________________LP_PFC___M_F____F______L________________________"
    "____________________________________F___L____F__C___P_FF_____F___________F________L____F____"
    "____________________________FFFFFC___________________L_______F_____L_______F___________L_P__"
    "_____FLL_C_F___FLC____L__F_____________________________________________F__________________LL"
    "_____P______FC___PCFLMLPPFF___FFFPL_LF_______F___MMFMMM______M___M_MMM_MF_________________F_";
static_assert(PrimeMissileTaxonomy.size() == 736U);
static_assert(StockMissileElementTaxonomy.size() == 736U);

[[nodiscard]] constexpr auto MissileElementFromCode(char code) noexcept
        -> DataCatalogMissileElement {
    switch (code) {
        case 'Y': return DataCatalogMissileElement::Physical;
        case 'F': return DataCatalogMissileElement::Fire;
        case 'C': return DataCatalogMissileElement::Cold;
        case 'L': return DataCatalogMissileElement::Lightning;
        case 'P': return DataCatalogMissileElement::Poison;
        case 'M': return DataCatalogMissileElement::Magic;
        default: return DataCatalogMissileElement::Hidden;
    }
}

[[nodiscard]] constexpr auto MissileElementCodeFromTxt(
        std::string_view element) noexcept -> char {
    if (element == "fire" || element == "burn") return 'F';
    if (element == "cold" || element == "frze") return 'C';
    if (element == "ltng") return 'L';
    if (element == "pois") return 'P';
    if (element == "mag") return 'M';
    return '_';
}

[[nodiscard]] constexpr auto ClassifyMissileElementImpl(
        std::uint32_t classId,
        std::string_view activeElement,
        bool hasPhysicalDamage) noexcept -> DataCatalogMissileElement {
    const auto activeCode = MissileElementCodeFromTxt(activeElement);
    if (classId < PrimeMissileTaxonomy.size()) {
        if (activeCode == StockMissileElementTaxonomy[classId]) {
            return MissileElementFromCode(PrimeMissileTaxonomy[classId]);
        }
        if (activeCode != '_') return MissileElementFromCode(activeCode);
        return hasPhysicalDamage
            ? DataCatalogMissileElement::Physical
            : DataCatalogMissileElement::Hidden;
    }
    if (activeCode != '_') return MissileElementFromCode(activeCode);
    return hasPhysicalDamage
        ? DataCatalogMissileElement::Physical
        : DataCatalogMissileElement::Hidden;
}

[[nodiscard]] constexpr auto FamilyIndex(DataCatalogFamily family) noexcept
        -> std::size_t {
    const auto index = static_cast<std::size_t>(family);
    return index < FamilyCount ? index : 0U;
}

[[nodiscard]] auto ClampLimits(MapSenseDataCatalogLimits limits) noexcept
        -> MapSenseDataCatalogLimits {
    const auto bounded = [](std::size_t value, std::size_t fallback,
                            std::size_t hardMaximum) noexcept {
        return (std::min)(value == 0U ? fallback : value, hardMaximum);
    };
    limits.maximumTableBytes = bounded(
        limits.maximumTableBytes, 32U * 1'024U * 1'024U,
        HardMaximumTableBytes);
    limits.maximumLineBytes = bounded(
        limits.maximumLineBytes, 2U * 1'024U * 1'024U,
        HardMaximumLineBytes);
    limits.maximumColumns = bounded(
        limits.maximumColumns, 1'024U, HardMaximumColumns);
    limits.maximumRowsPerFamily = bounded(
        limits.maximumRowsPerFamily, 65'536U,
        HardMaximumRowsPerFamily);
    limits.maximumKeyBytes = bounded(
        limits.maximumKeyBytes, 1'024U, HardMaximumKeyBytes);
    limits.maximumLocalizedBytes = bounded(
        limits.maximumLocalizedBytes, 4'096U,
        HardMaximumLocalizedBytes);
    limits.maximumDiagnostics = bounded(
        limits.maximumDiagnostics, 128U, HardMaximumDiagnostics);
    return limits;
}

class DiagnosticSink final {
public:
    DiagnosticSink(
        std::vector<DataCatalogDiagnostic>& diagnostics,
        std::size_t maximum) noexcept
        : diagnostics_(diagnostics), maximum_(maximum) {}

    void Add(
            DataCatalogDiagnosticSeverity severity,
            DataCatalogFamily family,
            std::string code,
            std::string message) {
        if (maximum_ == 0U) return;
        if (message.size() > MaximumDiagnosticMessageBytes) {
            message.resize(MaximumDiagnosticMessageBytes);
        }
        if (diagnostics_.size() < maximum_) {
            diagnostics_.push_back({
                .severity = severity,
                .family = family,
                .code = std::move(code),
                .message = std::move(message),
            });
            return;
        }
        if (truncated_) return;
        truncated_ = true;
        diagnostics_.back() = {
            .severity = DataCatalogDiagnosticSeverity::Warning,
            .family = family,
            .code = "diagnostics_truncated",
            .message = "MapSense data-catalog diagnostics reached the "
                "configured bound.",
        };
    }

private:
    std::vector<DataCatalogDiagnostic>& diagnostics_;
    std::size_t maximum_{};
    bool truncated_{};
};

[[nodiscard]] auto Utf8Valid(std::string_view text) noexcept -> bool {
    std::size_t index{};
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        std::size_t continuationCount{};
        std::uint32_t value{};
        std::uint32_t minimum{};
        if ((first & 0xE0U) == 0xC0U) {
            continuationCount = 1U;
            value = first & 0x1FU;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            continuationCount = 2U;
            value = first & 0x0FU;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            continuationCount = 3U;
            value = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuationCount >= text.size()) return false;
        for (std::size_t part = 1U; part <= continuationCount; ++part) {
            const auto byte = static_cast<unsigned char>(text[index + part]);
            if ((byte & 0xC0U) != 0x80U) return false;
            value = (value << 6U) | (byte & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFFU
            || (value >= 0xD800U && value <= 0xDFFFU)) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

[[nodiscard]] auto DisplayPath(const std::filesystem::path& path)
        -> std::string {
    const auto value = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(value.data()), value.size());
}

[[nodiscard]] auto NormalizedPathKey(const std::filesystem::path& path)
        -> std::wstring {
    auto value = path.lexically_normal().native();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z'
            ? static_cast<wchar_t>(ch - L'A' + L'a')
            : ch;
    });
    return value;
}

void FingerprintBytes(
        std::uint64_t& fingerprint,
        const void* data,
        std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0U; index < size; ++index) {
        fingerprint ^= bytes[index];
        fingerprint *= UINT64_C(1099511628211);
    }
}

template <typename Value>
void FingerprintValue(
        std::uint64_t& fingerprint,
        const Value& value) noexcept {
    FingerprintBytes(fingerprint, &value, sizeof(value));
}

void FingerprintPath(
        std::uint64_t& fingerprint,
        const std::filesystem::path& path) {
    const auto normalized = NormalizedPathKey(path);
    FingerprintBytes(
        fingerprint,
        normalized.data(),
        normalized.size() * sizeof(wchar_t));
}

[[nodiscard]] auto BuildAtlasDataFingerprint(
        const std::vector<std::filesystem::path>& excelDirectories,
        const std::vector<std::filesystem::path>& tileDirectories,
        std::size_t maximumTableBytes) -> std::uint64_t {
    if (excelDirectories.empty() && tileDirectories.empty()) return 0U;
    auto fingerprint = UINT64_C(14695981039346656037);
    for (const auto& root : excelDirectories) {
        FingerprintPath(fingerprint, root);
        for (const auto& name : AtlasTableNames) {
            const auto path = root / name;
            FingerprintPath(fingerprint, name);
            std::error_code tableError;
            const auto status = std::filesystem::symlink_status(
                path, tableError);
            const auto size = tableError
                ? std::uintmax_t{0U}
                : std::filesystem::file_size(path, tableError);
            std::string bytes;
            if (!tableError && std::filesystem::is_regular_file(status)
                && size != 0U && size <= maximumTableBytes
                && size <= static_cast<std::uintmax_t>(
                    (std::numeric_limits<std::size_t>::max)())) {
                bytes.assign(static_cast<std::size_t>(size), '\0');
                std::ifstream input(path, std::ios::binary);
                input.read(
                    bytes.data(),
                    static_cast<std::streamsize>(bytes.size()));
                if (!input
                    || input.peek() != std::char_traits<char>::eof()) {
                    bytes.clear();
                }
            }
            if (!bytes.empty()) {
                const std::uint8_t present = 1U;
                FingerprintValue(fingerprint, present);
                FingerprintBytes(
                    fingerprint, bytes.data(), bytes.size());
            } else {
                const std::uint8_t missing = 0U;
                FingerprintValue(fingerprint, missing);
            }
        }
    }
    std::size_t assetCount{};
    for (const auto& root : tileDirectories) {
        FingerprintPath(fingerprint, root);
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error) continue;
        std::filesystem::recursive_directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end{};
        while (!error && iterator != end
            && assetCount < MaximumAtlasAssetFingerprintEntries) {
            const auto& entry = *iterator;
            std::error_code entryError;
            const auto status = entry.symlink_status(entryError);
            if (!entryError && std::filesystem::is_regular_file(status)) {
                auto extension = entry.path().extension().native();
                std::transform(
                    extension.begin(), extension.end(), extension.begin(),
                    [](wchar_t value) {
                        return value >= L'A' && value <= L'Z'
                            ? static_cast<wchar_t>(value + (L'a' - L'A'))
                            : value;
                    });
                if (extension == L".ds1" || extension == L".dt1") {
                    FingerprintPath(
                        fingerprint,
                        entry.path().lexically_relative(root));
                    const auto size = entry.file_size(entryError);
                    if (!entryError) FingerprintValue(fingerprint, size);
                    const auto modified = entry.last_write_time(entryError);
                    if (!entryError) {
                        const auto ticks = modified.time_since_epoch().count();
                        FingerprintValue(fingerprint, ticks);
                    }
                    ++assetCount;
                }
            }
            iterator.increment(error);
        }
        FingerprintValue(fingerprint, assetCount);
    }
    return fingerprint == 0U ? UINT64_C(1) : fingerprint;
}

void AppendUniquePath(
        std::vector<std::filesystem::path>& paths,
        const std::filesystem::path& candidate) {
    if (candidate.empty()) return;
    const auto key = NormalizedPathKey(candidate);
    if (std::any_of(paths.begin(), paths.end(), [&](const auto& current) {
            return NormalizedPathKey(current) == key;
        })) {
        return;
    }
    paths.push_back(candidate.lexically_normal());
}

[[nodiscard]] auto ContextHasField(
        const D2RL::PluginContext* context,
        std::size_t fieldEnd) noexcept -> bool {
    return context != nullptr && context->contextSize >= fieldEnd;
}

[[nodiscard]] auto BoundedCString(
        const char* value,
        std::size_t maximum,
        std::string& output) -> bool {
    output.clear();
    if (value == nullptr) return true;
    std::size_t length{};
    while (length < maximum && value[length] != '\0') ++length;
    if (length == maximum) return false;
    output.assign(value, length);
    return Utf8Valid(output);
}

[[nodiscard]] auto ValidActiveModToken(std::string_view token) noexcept
        -> bool {
    if (token.empty() || token == "." || token == "..") return false;
    for (const char value : token) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte < 0x20U || value == '/' || value == '\\'
            || value == ':') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto EndsWithMpq(std::string_view value) noexcept -> bool {
    if (value.size() < 4U) return false;
    constexpr std::string_view suffix{".mpq"};
    const auto offset = value.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        auto ch = value[offset + index];
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
        if (ch != suffix[index]) return false;
    }
    return true;
}

struct SourceDirectories final {
    bool activeInspectionAllowed{true};
    std::vector<std::filesystem::path> active{};
    std::vector<std::filesystem::path> vanilla{};
};

[[nodiscard]] auto DiscoverSourceDirectories(
        const D2RL::PluginContext* context,
        const MapSenseDataCatalogLoadOptions& options,
        DiagnosticSink& diagnostics) -> SourceDirectories {
    SourceDirectories result{};
    for (const auto& path : options.vanillaExcelDirectories) {
        AppendUniquePath(result.vanilla, path);
    }

    std::string activeMod;
    constexpr auto ActiveModEnd =
        offsetof(D2RL::PluginContext, activeMod)
        + sizeof(const char*);
    constexpr auto ModDirectoryEnd =
        offsetof(D2RL::PluginContext, modDirectory)
        + sizeof(const wchar_t*);
    const bool hasActiveModField = ContextHasField(context, ActiveModEnd);
    const bool hasModDirectoryField = ContextHasField(context, ModDirectoryEnd);
    if (hasActiveModField
        && !BoundedCString(context->activeMod, 256U, activeMod)) {
        result.activeInspectionAllowed = false;
        diagnostics.Add(
            DataCatalogDiagnosticSeverity::Error,
            DataCatalogFamily::Levels,
            "invalid_active_mod",
            "D2RLoader exposed an invalid or overlong active-mod name; "
            "all table families fail closed.");
        return result;
    }
    const bool hasActiveMod = !activeMod.empty();
    if (hasActiveMod && !ValidActiveModToken(activeMod)) {
        result.activeInspectionAllowed = false;
        diagnostics.Add(
            DataCatalogDiagnosticSeverity::Error,
            DataCatalogFamily::Levels,
            "invalid_active_mod",
            "D2RLoader exposed an unsafe active-mod path token; all table "
            "families fail closed.");
        return result;
    }

    if (hasActiveMod) {
        if (!hasModDirectoryField || context->modDirectory == nullptr
            || context->modDirectory[0] == L'\0') {
            result.activeInspectionAllowed = false;
            diagnostics.Add(
                DataCatalogDiagnosticSeverity::Error,
                DataCatalogFamily::Levels,
                "active_mod_directory_unavailable",
                "An active mod is present but D2RLoader did not expose its "
                "directory; vanilla fallback is unsafe and disabled.");
            return result;
        }
        const std::filesystem::path root(context->modDirectory);
        std::error_code rootError;
        if (!std::filesystem::is_directory(root, rootError)
            || rootError) {
            result.activeInspectionAllowed = false;
            diagnostics.Add(
                DataCatalogDiagnosticSeverity::Error,
                DataCatalogFamily::Levels,
                "active_mod_directory_unavailable",
                "The active mod directory is unavailable; vanilla fallback "
                "is unsafe and disabled.");
            return result;
        }
        AppendUniquePath(
            result.active, root / L"data" / L"global" / L"excel");
        std::string packageName = activeMod;
        if (!EndsWithMpq(packageName)) packageName += ".mpq";
        const auto* const packageBegin =
            reinterpret_cast<const char8_t*>(packageName.data());
        const std::filesystem::path packagePath(std::u8string(
            packageBegin, packageBegin + packageName.size()));
        AppendUniquePath(
            result.active,
            root / packagePath / L"data" / L"global" / L"excel");
        for (const auto& directory : result.active) {
            AppendUniquePath(result.vanilla, directory / L"base");
        }
    }

    constexpr auto PluginDirectoryEnd =
        offsetof(D2RL::PluginContext, pluginDirectory)
        + sizeof(const wchar_t*);
    if (ContextHasField(context, PluginDirectoryEnd)
        && context->pluginDirectory != nullptr
        && context->pluginDirectory[0] != L'\0') {
        AppendUniquePath(
            result.vanilla,
            std::filesystem::path(context->pluginDirectory)
                / L"vanilla-excel");
    }
    return result;
}

enum class FilePresence : std::uint8_t {
    Missing,
    Regular,
    Invalid,
};

[[nodiscard]] auto InspectFile(
        const std::filesystem::path& path,
        std::string& error) -> FilePresence {
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError) {
        if (statusError == std::errc::no_such_file_or_directory) {
            return FilePresence::Missing;
        }
        error = "cannot inspect " + DisplayPath(path) + ": "
            + statusError.message();
        return FilePresence::Invalid;
    }
    if (!std::filesystem::exists(status)) return FilePresence::Missing;
    if (!std::filesystem::is_regular_file(status)) {
        error = "expected a regular file at " + DisplayPath(path);
        return FilePresence::Invalid;
    }
    return FilePresence::Regular;
}

[[nodiscard]] auto ReadBoundedFile(
        const std::filesystem::path& path,
        std::size_t maximumBytes,
        std::string& bytes,
        std::string& error) -> bool {
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        error = "cannot measure " + DisplayPath(path) + ": "
            + sizeError.message();
        return false;
    }
    if (size == 0U || size > maximumBytes
        || size > (std::numeric_limits<std::size_t>::max)()) {
        error = "table size is outside the configured bound at "
            + DisplayPath(path);
        return false;
    }
    bytes.assign(static_cast<std::size_t>(size), '\0');
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open " + DisplayPath(path);
        return false;
    }
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "short read while loading " + DisplayPath(path);
        return false;
    }
    if (stream.peek() != std::char_traits<char>::eof()) {
        error = "table changed while it was being read: " + DisplayPath(path);
        return false;
    }
    return true;
}

enum class LayerResolutionState : std::uint8_t {
    None,
    Text,
    BinaryOnly,
    Invalid,
};

struct LayerResolution final {
    LayerResolutionState state{LayerResolutionState::None};
    std::filesystem::path path{};
    std::string bytes{};
    std::string error{};
};

[[nodiscard]] auto ResolveLayer(
        const std::vector<std::filesystem::path>& directories,
        const TableSpec& spec,
        std::size_t maximumBytes) -> LayerResolution {
    LayerResolution result{};
    std::vector<std::pair<std::filesystem::path, std::string>> texts;
    for (const auto& directory : directories) {
        const auto txtPath = directory / spec.txtName;
        const auto binPath = directory / spec.binName;
        std::string error;
        const auto txt = InspectFile(txtPath, error);
        if (txt == FilePresence::Invalid) {
            result.state = LayerResolutionState::Invalid;
            result.error = std::move(error);
            return result;
        }
        const auto bin = InspectFile(binPath, error);
        if (bin == FilePresence::Invalid) {
            result.state = LayerResolutionState::Invalid;
            result.error = std::move(error);
            return result;
        }
        if (bin == FilePresence::Regular && txt == FilePresence::Missing) {
            result.state = LayerResolutionState::BinaryOnly;
            result.path = binPath;
            result.error = "binary override exists without its auditable TXT "
                "source at " + DisplayPath(binPath);
            return result;
        }
        if (txt != FilePresence::Regular) continue;
        std::string bytes;
        if (!ReadBoundedFile(txtPath, maximumBytes, bytes, error)) {
            result.state = LayerResolutionState::Invalid;
            result.path = txtPath;
            result.error = std::move(error);
            return result;
        }
        texts.emplace_back(txtPath, std::move(bytes));
    }
    if (texts.empty()) return result;
    for (std::size_t index = 1U; index < texts.size(); ++index) {
        if (texts[index].second == texts.front().second) continue;
        result.state = LayerResolutionState::Invalid;
        result.error = "conflicting TXT overrides were found at "
            + DisplayPath(texts.front().first) + " and "
            + DisplayPath(texts[index].first);
        return result;
    }
    result.state = LayerResolutionState::Text;
    result.path = std::move(texts.front().first);
    result.bytes = std::move(texts.front().second);
    return result;
}

enum class LineEnding : std::uint8_t {
    None,
    Lf,
    CrLf,
};

class StrictTsvReader final {
public:
    [[nodiscard]] auto Open(
            std::string_view bytes,
            const MapSenseDataCatalogLimits& limits,
            std::string& error) -> bool {
        limits_ = &limits;
        if (bytes.size() >= 3U
            && static_cast<unsigned char>(bytes[0]) == 0xEFU
            && static_cast<unsigned char>(bytes[1]) == 0xBBU
            && static_cast<unsigned char>(bytes[2]) == 0xBFU) {
            bytes.remove_prefix(3U);
        }
        if (bytes.empty()) {
            error = "TXT table is empty";
            return false;
        }
        if (!Utf8Valid(bytes)) {
            error = "TXT table is not valid UTF-8";
            return false;
        }
        std::size_t lineBytes{};
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const char value = bytes[index];
            if (value == '\0') {
                error = "TXT table contains a NUL byte";
                return false;
            }
            if (value == '\r') {
                if (index + 1U >= bytes.size() || bytes[index + 1U] != '\n') {
                    error = "TXT table contains a bare CR line ending";
                    return false;
                }
                if (ending_ == LineEnding::Lf) {
                    error = "TXT table mixes LF and CRLF line endings";
                    return false;
                }
                ending_ = LineEnding::CrLf;
                if (lineBytes > limits.maximumLineBytes) {
                    error = "TXT line exceeds the configured byte bound";
                    return false;
                }
                lineBytes = 0U;
                ++index;
                continue;
            }
            if (value == '\n') {
                if (ending_ == LineEnding::CrLf) {
                    error = "TXT table mixes LF and CRLF line endings";
                    return false;
                }
                ending_ = LineEnding::Lf;
                if (lineBytes > limits.maximumLineBytes) {
                    error = "TXT line exceeds the configured byte bound";
                    return false;
                }
                lineBytes = 0U;
                continue;
            }
            ++lineBytes;
        }
        if (lineBytes > limits.maximumLineBytes) {
            error = "TXT line exceeds the configured byte bound";
            return false;
        }
        if (ending_ == LineEnding::None) {
            error = "TXT table has no data rows";
            return false;
        }
        bytes_ = bytes;
        std::string_view headerLine;
        bool hasLine{};
        if (!ExtractLine(headerLine, hasLine, error) || !hasLine) {
            if (error.empty()) error = "TXT table has no header";
            return false;
        }
        if (!SplitLine(headerLine, header_, error)) return false;
        for (std::size_t index = 0U; index < header_.size(); ++index) {
            if (header_[index].empty()) {
                error = "TXT header contains an empty column name";
                return false;
            }
            if (header_[index].size() > limits.maximumKeyBytes) {
                error = "TXT header column exceeds the configured byte bound";
                return false;
            }
            const bool inserted = columns_.emplace(
                std::string(header_[index]), index).second;
            if (!inserted) {
                error = "TXT header contains duplicate column "
                    + std::string(header_[index]);
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto RequireColumns(
            std::initializer_list<std::string_view> names,
            std::vector<std::size_t>& result,
            std::string& error) const -> bool {
        result.clear();
        result.reserve(names.size());
        for (const auto name : names) {
            const auto found = columns_.find(name);
            if (found == columns_.end()) {
                error = "TXT table is missing required column "
                    + std::string(name);
                return false;
            }
            result.push_back(found->second);
        }
        return true;
    }

    [[nodiscard]] auto FindColumn(std::string_view name) const noexcept
            -> std::size_t {
        const auto found = columns_.find(name);
        return found == columns_.end()
            ? (std::numeric_limits<std::size_t>::max)()
            : found->second;
    }

    [[nodiscard]] auto Next(
            std::vector<std::string_view>& row,
            bool& hasRow,
            std::string& error) -> bool {
        std::string_view line;
        if (!ExtractLine(line, hasRow, error) || !hasRow) return error.empty();
        if (line.empty()) {
            error = "TXT table contains an empty data row at line "
                + std::to_string(lineNumber_);
            return false;
        }
        if (!SplitLine(line, row, error)) return false;
        if (row.size() != header_.size()) {
            error = "TXT row width differs from its header at line "
                + std::to_string(lineNumber_);
            return false;
        }
        return true;
    }

    [[nodiscard]] auto LineNumber() const noexcept -> std::size_t {
        return lineNumber_;
    }

private:
    [[nodiscard]] auto ExtractLine(
            std::string_view& line,
            bool& hasLine,
            std::string& error) -> bool {
        hasLine = false;
        if (cursor_ >= bytes_.size()) return true;
        const auto delimiter = ending_ == LineEnding::CrLf
            ? std::string_view{"\r\n"}
            : std::string_view{"\n"};
        const auto end = bytes_.find(delimiter, cursor_);
        if (end == std::string_view::npos) {
            line = bytes_.substr(cursor_);
            cursor_ = bytes_.size();
        } else {
            line = bytes_.substr(cursor_, end - cursor_);
            cursor_ = end + delimiter.size();
            if (cursor_ == bytes_.size() && line.empty()) {
                error = "TXT table contains an empty data row";
                return false;
            }
        }
        ++lineNumber_;
        hasLine = true;
        return true;
    }

    [[nodiscard]] auto SplitLine(
            std::string_view line,
            std::vector<std::string_view>& fields,
            std::string& error) const -> bool {
        fields.clear();
        std::size_t start{};
        while (start <= line.size()) {
            if (fields.size() >= limits_->maximumColumns) {
                error = "TXT row exceeds the configured column bound";
                return false;
            }
            const auto end = line.find('\t', start);
            fields.emplace_back(line.substr(start, end - start));
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
        return true;
    }

    const MapSenseDataCatalogLimits* limits_{};
    std::string_view bytes_{};
    LineEnding ending_{LineEnding::None};
    std::size_t cursor_{};
    // Header extraction increments this to 1; data records begin at line 2.
    std::size_t lineNumber_{};
    std::vector<std::string_view> header_{};
    std::map<std::string, std::size_t, std::less<>> columns_{};
};

[[nodiscard]] auto ParseUnsigned(
        std::string_view value,
        bool blankIsZero,
        std::uint32_t& output) noexcept -> bool {
    output = 0U;
    if (value.empty()) return blankIsZero;
    const auto* const begin = value.data();
    const auto* const end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

[[nodiscard]] auto ParseSignedId(
        std::string_view value,
        std::int32_t& output) noexcept -> bool {
    output = 0;
    if (value.empty()) return false;
    const auto* const begin = value.data();
    const auto* const end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end && output >= 0;
}

[[nodiscard]] auto ParseSigned(
        std::string_view value,
        std::int32_t& output) noexcept -> bool {
    output = 0;
    if (value.empty()) return false;
    const auto* const begin = value.data();
    const auto* const end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, output, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

[[nodiscard]] auto ParseFlag(std::string_view value, bool& output) noexcept
        -> bool {
    if (value.empty() || value == "0") {
        output = false;
        return true;
    }
    if (value == "1") {
        output = true;
        return true;
    }
    return false;
}

[[nodiscard]] auto CopyBoundedKey(
        std::string_view value,
        const MapSenseDataCatalogLimits& limits,
        bool allowEmpty,
        std::string& output) -> bool {
    if ((!allowEmpty && value.empty()) || value.size() > limits.maximumKeyBytes) {
        return false;
    }
    output.assign(value);
    return true;
}

struct CachedLocalization final {
    std::string utf8{};
    bool serviceResolved{};
};

class LocalizationCache final {
public:
    LocalizationCache(
            const D2RL::PluginContext* context,
            const D2RL::LocalizationServiceV1* service,
            std::size_t maximumBytes) noexcept
        : context_(context), service_(service), maximumBytes_(maximumBytes) {}

    [[nodiscard]] auto Resolve(
            std::string_view key,
            std::string_view playerFacingFallback = {})
            -> DataCatalogLocalizedText {
        DataCatalogLocalizedText result{};
        result.key.assign(key);
        if (key.empty()) return result;
        const auto found = cache_.find(key);
        if (found != cache_.end()) {
            result.utf8 = found->second.utf8;
            result.localized = IsPlayerFacingResolution(
                key, playerFacingFallback, found->second);
            if (result.localized && result.utf8 != key) {
                hasVerifiedPlayerFacingLocalization_ = true;
            }
            return result;
        }
        auto cached = QueryService(key);
        const auto inserted = cache_.emplace(
            std::string(key), std::move(cached)).first;
        result.utf8 = inserted->second.utf8;
        result.localized = IsPlayerFacingResolution(
            key, playerFacingFallback, inserted->second);
        if (result.localized && result.utf8 != key) {
            hasVerifiedPlayerFacingLocalization_ = true;
        }
        return result;
    }

    // A localized value may legitimately equal its lookup key, especially
    // for player-facing names added by a mod in the client's current
    // language. The same shape is also produced while D2R's language tables
    // are not ready. Once another lookup proves that the service is ready,
    // re-query exact unresolved values: a real entry succeeds again while a
    // missing key is required to return NotFound. This preserves the early
    // key-echo guard without rejecting arbitrary mod-defined names.
    [[nodiscard]] auto RefreshVerifiedExactResolution(
            DataCatalogLocalizedText& value) -> bool {
        if (!hasVerifiedPlayerFacingLocalization_
            || value.localized || value.key.empty()
            || value.utf8 != value.key) {
            return false;
        }
        auto refreshed = QueryService(value.key);
        if (!refreshed.serviceResolved) return false;
        if (refreshed.utf8 == value.key
            && value.key.starts_with("ShrId")) {
            return false;
        }
        value.utf8 = refreshed.utf8;
        value.localized = true;
        cache_.insert_or_assign(value.key, std::move(refreshed));
        return true;
    }

    [[nodiscard]] auto HasVerifiedPlayerFacingLocalization() const noexcept
            -> bool {
        return hasVerifiedPlayerFacingLocalization_;
    }

private:
    [[nodiscard]] auto QueryService(std::string_view key) const
            -> CachedLocalization {
        CachedLocalization result{.utf8 = std::string(key)};
        if (service_ == nullptr || service_->getStringByKey == nullptr) {
            return result;
        }
        std::uint32_t required{};
        const auto first = service_->getStringByKey(
            context_, result.utf8.c_str(), nullptr, 0U, &required);
        if (first != D2RL::Localization::Result::BufferTooSmall
            || required <= 1U || required > maximumBytes_) {
            return result;
        }
        std::vector<char> buffer(required, '\0');
        std::uint32_t returned = required;
        const auto second = service_->getStringByKey(
            context_, result.utf8.c_str(), buffer.data(),
            static_cast<std::uint32_t>(buffer.size()), &returned);
        if (second != D2RL::Localization::Result::Success
            || returned <= 1U || returned > buffer.size()
            || buffer[returned - 1U] != '\0') {
            return result;
        }
        const std::string_view localized(buffer.data(), returned - 1U);
        if (localized.find('\0') != std::string_view::npos
            || !Utf8Valid(localized)) {
            return result;
        }
        result.utf8.assign(localized);
        result.serviceResolved = true;
        return result;
    }

    [[nodiscard]] static auto IsPlayerFacingResolution(
            std::string_view key,
            std::string_view fallback,
            const CachedLocalization& cached) noexcept -> bool {
        if (!cached.serviceResolved) return false;
        if (cached.utf8 != key) return true;
        // Before D2R initializes its language tables the service reports
        // Success but echoes technical keys. The human-readable TXT comments
        // let us reject that state without substituting English at render time.
        if (!fallback.empty() && fallback != key) return false;
        return !key.starts_with("ShrId");
    }

    const D2RL::PluginContext* context_{};
    const D2RL::LocalizationServiceV1* service_{};
    std::size_t maximumBytes_{};
    bool hasVerifiedPlayerFacingLocalization_{};
    std::map<std::string, CachedLocalization, std::less<>> cache_{};
};

void CountLocalization(
        const DataCatalogLocalizedText& name,
        DataCatalogFamilyStatus& status) noexcept {
    // Some official technical rows deliberately have no display key. They are
    // valid catalog records, but not unresolved localization requests.
    if (name.key.empty()) return;
    if (name.localized) {
        ++status.localizedNameCount;
    } else {
        ++status.unresolvedNameCount;
    }
}

[[nodiscard]] auto ExpansionSeparator(
        std::string_view id,
        std::string_view index) noexcept -> bool {
    return index.empty() && id == "Expansion";
}

} // namespace

auto ClassifyDataCatalogMissileElement(
        std::uint32_t classId,
        std::string_view activeElement,
        bool hasPhysicalDamage) noexcept -> DataCatalogMissileElement {
    return ClassifyMissileElementImpl(
        classId,
        activeElement,
        hasPhysicalDamage);
}

auto StockDataCatalogMissileElement(std::uint32_t classId) noexcept
        -> DataCatalogMissileElement {
    return classId < PrimeMissileTaxonomy.size()
        ? MissileElementFromCode(PrimeMissileTaxonomy[classId])
        : DataCatalogMissileElement::Hidden;
}

struct MapSenseDataCatalog::Impl final {
    std::array<DataCatalogFamilyStatus, FamilyCount> statuses{};
    bool hasLocalizationService{};
    bool hasVerifiedPlayerFacingLocalization{};
    std::vector<std::filesystem::path> activeExcelDirectories{};
    std::vector<std::filesystem::path> activeTileDirectories{};
    std::uint64_t atlasDataFingerprint{};

    std::vector<DataCatalogLevel> levels{};
    std::map<std::int32_t, std::size_t> levelById{};

    std::vector<DataCatalogShrine> shrines{};
    std::map<std::uint32_t, std::size_t> shrineByRow{};
    std::map<std::uint32_t, std::vector<std::uint32_t>> shrineRowsByCode{};

    std::vector<DataCatalogSuperUnique> superUniques{};
    std::map<std::uint32_t, std::size_t> superUniqueByHcIdx{};

    std::vector<DataCatalogMonStats> monStats{};
    std::map<std::uint32_t, std::size_t> monStatsByHcIdx{};

    std::vector<DataCatalogObject> objects{};
    std::map<std::string, std::size_t, std::less<>> objectByClass{};
    std::map<std::uint32_t, std::size_t> objectById{};

    std::vector<DataCatalogMissile> missiles{};
};

namespace {

void RefreshVerifiedExactLocalizations(
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl) {
    for (auto& level : impl.levels) {
        if (!localization.RefreshVerifiedExactResolution(level.name)) {
            continue;
        }
        level.waypointLabelUtf8.clear();
        if (!level.name.utf8.empty()) {
            level.waypointLabelUtf8.reserve(
                level.name.utf8.size() + sizeof(" Waypoint") - 1U);
            level.waypointLabelUtf8 = level.name.utf8;
            level.waypointLabelUtf8 += " Waypoint";
        }
    }
    for (auto& shrine : impl.shrines) {
        static_cast<void>(
            localization.RefreshVerifiedExactResolution(shrine.name));
    }
    for (auto& superUnique : impl.superUniques) {
        static_cast<void>(localization.RefreshVerifiedExactResolution(
            superUnique.name));
    }
    for (auto& monStats : impl.monStats) {
        static_cast<void>(
            localization.RefreshVerifiedExactResolution(monStats.name));
    }
    for (auto& object : impl.objects) {
        static_cast<void>(
            localization.RefreshVerifiedExactResolution(object.name));
    }
}

void RecountCatalogLocalizations(MapSenseDataCatalog::Impl& impl) noexcept {
    for (auto& status : impl.statuses) {
        status.localizedNameCount = 0U;
        status.unresolvedNameCount = 0U;
    }
    auto& levelStatus =
        impl.statuses[FamilyIndex(DataCatalogFamily::Levels)];
    for (const auto& level : impl.levels) {
        CountLocalization(level.name, levelStatus);
    }
    auto& shrineStatus =
        impl.statuses[FamilyIndex(DataCatalogFamily::Shrines)];
    for (const auto& shrine : impl.shrines) {
        if (shrine.row != 0U) CountLocalization(shrine.name, shrineStatus);
    }
    auto& superUniqueStatus =
        impl.statuses[FamilyIndex(DataCatalogFamily::SuperUniques)];
    for (const auto& superUnique : impl.superUniques) {
        CountLocalization(superUnique.name, superUniqueStatus);
    }
    auto& monStatsStatus =
        impl.statuses[FamilyIndex(DataCatalogFamily::MonStats)];
    for (const auto& monStats : impl.monStats) {
        CountLocalization(monStats.name, monStatsStatus);
    }
    auto& objectStatus =
        impl.statuses[FamilyIndex(DataCatalogFamily::Objects)];
    for (const auto& object : impl.objects) {
        CountLocalization(object.name, objectStatus);
    }
}

[[nodiscard]] auto ParseLevels(
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    StrictTsvReader reader;
    if (!reader.Open(bytes, limits, error)) return false;
    std::vector<std::size_t> columns;
    if (!reader.RequireColumns({"Name", "Id", "LevelName"}, columns, error)) {
        return false;
    }
    const auto displayNameColumn = reader.FindColumn("*StringName");
    const auto waypointColumn = reader.FindColumn("Waypoint");
    const auto actColumn = reader.FindColumn("Act");
    const auto layerColumn = reader.FindColumn("Layer");
    const std::array sizeXColumns{
        reader.FindColumn("SizeX"),
        reader.FindColumn("SizeX(N)"),
        reader.FindColumn("SizeX(H)"),
    };
    const std::array sizeYColumns{
        reader.FindColumn("SizeY"),
        reader.FindColumn("SizeY(N)"),
        reader.FindColumn("SizeY(H)"),
    };
    const auto offsetXColumn = reader.FindColumn("OffsetX");
    const auto offsetYColumn = reader.FindColumn("OffsetY");
    const auto dependColumn = reader.FindColumn("Depend");
    const auto drlgTypeColumn = reader.FindColumn("DrlgType");
    constexpr auto MissingColumn = (std::numeric_limits<std::size_t>::max)();
    const bool hasPlacementColumns = actColumn != MissingColumn
        && layerColumn != MissingColumn
        && std::ranges::none_of(sizeXColumns, [](std::size_t column) {
            return column == MissingColumn;
        })
        && std::ranges::none_of(sizeYColumns, [](std::size_t column) {
            return column == MissingColumn;
        })
        && offsetXColumn != MissingColumn && offsetYColumn != MissingColumn
        && dependColumn != MissingColumn && drlgTypeColumn != MissingColumn;
    std::vector<DataCatalogLevel> records;
    std::map<std::int32_t, std::size_t> index;
    std::vector<std::string_view> row;
    bool hasRow{};
    while (reader.Next(row, hasRow, error) && hasRow) {
        if (ExpansionSeparator(row[columns[0]], row[columns[1]])) continue;
        // Levels.txt reserves id 0 for the unnamed Null record. It cannot be
        // a physical destination and intentionally has no localization key.
        if (row[columns[1]] == "0" && row[columns[2]].empty()) continue;
        if (records.size() >= limits.maximumRowsPerFamily) {
            error = "Levels.txt exceeds the configured row bound";
            return false;
        }
        DataCatalogLevel record{};
        if (!ParseSignedId(row[columns[1]], record.id)) {
            error = "Levels.txt has an invalid Id at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string key;
        if (!CopyBoundedKey(row[columns[2]], limits, false, key)) {
            error = "Levels.txt has an invalid LevelName at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string displayFallback;
        if (displayNameColumn != (std::numeric_limits<std::size_t>::max)()
            && !CopyBoundedKey(
                row[displayNameColumn], limits, true, displayFallback)) {
            error = "Levels.txt has an invalid *StringName at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        record.name = localization.Resolve(key, displayFallback);
        if (actColumn != MissingColumn) {
            std::int32_t act{};
            if (ParseSigned(row[actColumn], act) && act >= 0 && act < 5) {
                record.act = act;
            }
        }
        if (hasPlacementColumns) {
            bool placementValid = record.act >= 0
                && ParseSigned(row[layerColumn], record.layer)
                && record.layer >= 0
                && ParseSigned(row[offsetXColumn], record.offsetX)
                && ParseSigned(row[offsetYColumn], record.offsetY)
                && ParseSigned(row[dependColumn], record.depend)
                && record.depend >= 0
                && ParseSigned(row[drlgTypeColumn], record.drlgType)
                && record.drlgType >= 0;
            for (std::size_t difficulty = 0U;
                    difficulty < record.sizeX.size();
                    ++difficulty) {
                placementValid = ParseSigned(
                        row[sizeXColumns[difficulty]],
                        record.sizeX[difficulty])
                    && ParseSigned(
                        row[sizeYColumns[difficulty]],
                        record.sizeY[difficulty])
                    && placementValid;
            }
            record.hasAtlasPlacement = placementValid;
        }
        if (waypointColumn != (std::numeric_limits<std::size_t>::max)()) {
            std::int32_t waypointOrdinal{};
            if (!ParseSignedId(row[waypointColumn], waypointOrdinal)
                    || waypointOrdinal < 0 || waypointOrdinal > 255) {
                error = "Levels.txt has an invalid Waypoint at line "
                    + std::to_string(reader.LineNumber());
                return false;
            }
            record.hasWaypoint = waypointOrdinal < 255;
        }
        if (!record.name.utf8.empty()) {
            record.waypointLabelUtf8.reserve(
                record.name.utf8.size() + sizeof(" Waypoint") - 1U);
            record.waypointLabelUtf8 = record.name.utf8;
            record.waypointLabelUtf8 += " Waypoint";
        }
        if (!index.emplace(record.id, records.size()).second) {
            error = "Levels.txt contains duplicate Id "
                + std::to_string(record.id);
            return false;
        }
        CountLocalization(record.name, status);
        records.push_back(std::move(record));
    }
    if (!error.empty()) return false;
    if (records.empty()) {
        error = "Levels.txt contains no records";
        return false;
    }
    status.rowCount = records.size();
    impl.levels = std::move(records);
    impl.levelById = std::move(index);
    return true;
}

[[nodiscard]] auto ParseShrines(
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    StrictTsvReader reader;
    if (!reader.Open(bytes, limits, error)) return false;
    std::vector<std::size_t> columns;
    if (!reader.RequireColumns({"Code", "StringName"}, columns, error)) {
        return false;
    }
    const auto displayNameColumn = reader.FindColumn("Name");
    std::vector<DataCatalogShrine> records;
    std::map<std::uint32_t, std::size_t> byRow;
    std::map<std::uint32_t, std::vector<std::uint32_t>> rowsByCode;
    std::vector<std::string_view> row;
    bool hasRow{};
    std::uint32_t rowIndex{};
    while (reader.Next(row, hasRow, error) && hasRow) {
        if (records.size() >= limits.maximumRowsPerFamily
            || rowIndex == (std::numeric_limits<std::uint32_t>::max)()) {
            error = "Shrines.txt exceeds the configured row bound";
            return false;
        }
        DataCatalogShrine record{.row = rowIndex};
        if (!ParseUnsigned(row[columns[0]], false, record.code)) {
            error = "Shrines.txt has an invalid Code at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string key;
        if (!CopyBoundedKey(row[columns[1]], limits, false, key)) {
            error = "Shrines.txt has an invalid StringName at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string displayFallback;
        if (displayNameColumn != (std::numeric_limits<std::size_t>::max)()
            && !CopyBoundedKey(
                row[displayNameColumn], limits, true, displayFallback)) {
            error = "Shrines.txt has an invalid Name at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        record.name = localization.Resolve(key, displayFallback);
        byRow.emplace(record.row, records.size());
        rowsByCode[record.code].push_back(record.row);
        // Row zero is the engine-reserved None record and is never drawn.
        if (rowIndex != 0U) CountLocalization(record.name, status);
        records.push_back(std::move(record));
        ++rowIndex;
    }
    if (!error.empty()) return false;
    if (records.empty()) {
        error = "Shrines.txt contains no records";
        return false;
    }
    status.rowCount = records.size();
    impl.shrines = std::move(records);
    impl.shrineByRow = std::move(byRow);
    impl.shrineRowsByCode = std::move(rowsByCode);
    return true;
}

[[nodiscard]] auto ParseSuperUniques(
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    StrictTsvReader reader;
    if (!reader.Open(bytes, limits, error)) return false;
    std::vector<std::size_t> columns;
    if (!reader.RequireColumns(
            {"Superunique", "Name", "Class", "hcIdx"},
            columns, error)) {
        return false;
    }
    std::vector<DataCatalogSuperUnique> records;
    std::map<std::uint32_t, std::size_t> index;
    std::vector<std::string_view> row;
    bool hasRow{};
    while (reader.Next(row, hasRow, error) && hasRow) {
        if (ExpansionSeparator(row[columns[0]], row[columns[3]])) continue;
        if (records.size() >= limits.maximumRowsPerFamily) {
            error = "SuperUniques.txt exceeds the configured row bound";
            return false;
        }
        DataCatalogSuperUnique record{};
        if (!ParseUnsigned(row[columns[3]], false, record.hcIdx)) {
            error = "SuperUniques.txt has an invalid hcIdx at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        if (!CopyBoundedKey(
                row[columns[2]], limits, false, record.classId)) {
            error = "SuperUniques.txt has an invalid Class at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string key;
        if (!CopyBoundedKey(row[columns[1]], limits, false, key)) {
            error = "SuperUniques.txt has an invalid Name at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        record.name = localization.Resolve(key);
        if (!index.emplace(record.hcIdx, records.size()).second) {
            error = "SuperUniques.txt contains duplicate hcIdx "
                + std::to_string(record.hcIdx);
            return false;
        }
        CountLocalization(record.name, status);
        records.push_back(std::move(record));
    }
    if (!error.empty()) return false;
    if (records.empty()) {
        error = "SuperUniques.txt contains no records";
        return false;
    }
    status.rowCount = records.size();
    impl.superUniques = std::move(records);
    impl.superUniqueByHcIdx = std::move(index);
    return true;
}

[[nodiscard]] auto ParseMonStats(
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    StrictTsvReader reader;
    if (!reader.Open(bytes, limits, error)) return false;
    std::vector<std::size_t> columns;
    if (!reader.RequireColumns(
            {"Id", "NameStr", "primeevil"},
            columns, error)) {
        return false;
    }
    std::vector<DataCatalogMonStats> records;
    std::map<std::uint32_t, std::size_t> index;
    std::vector<std::string_view> row;
    bool hasRow{};
    while (reader.Next(row, hasRow, error) && hasRow) {
        // Every '*' column is a comment ignored by D2R. Runtime class IDs are
        // the ordinal of real rows, with the Expansion separator excluded.
        if (row[columns[0]] == "Expansion") continue;
        if (records.size() >= limits.maximumRowsPerFamily) {
            error = "MonStats.txt exceeds the configured row bound";
            return false;
        }
        DataCatalogMonStats record{
            .hcIdx = static_cast<std::uint32_t>(records.size()),
        };
        if (!ParseFlag(row[columns[2]], record.primeEvil)) {
            error = "MonStats.txt has an invalid numeric/flag value at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        if (!CopyBoundedKey(row[columns[0]], limits, false, record.id)) {
            error = "MonStats.txt has an invalid Id at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string key;
        if (!CopyBoundedKey(row[columns[1]], limits, true, key)) {
            error = "MonStats.txt has an invalid NameStr at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        record.name = localization.Resolve(key);
        if (!index.emplace(record.hcIdx, records.size()).second) {
            error = "MonStats.txt contains duplicate *hcIdx "
                + std::to_string(record.hcIdx);
            return false;
        }
        CountLocalization(record.name, status);
        records.push_back(std::move(record));
    }
    if (!error.empty()) return false;
    if (records.empty()) {
        error = "MonStats.txt contains no records";
        return false;
    }
    status.rowCount = records.size();
    impl.monStats = std::move(records);
    impl.monStatsByHcIdx = std::move(index);
    return true;
}

[[nodiscard]] auto ParseObjects(
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    StrictTsvReader reader;
    if (!reader.Open(bytes, limits, error)) return false;
    std::vector<std::size_t> columns;
    if (!reader.RequireColumns({
            "Class", "Name", "SubClass", "Lockable", "OperateFn",
            "PopulateFn", "InitFn", "ClientFn", "ShrineFunction"},
            columns, error)) {
        return false;
    }
    std::vector<DataCatalogObject> records;
    std::map<std::string, std::size_t, std::less<>> byClass;
    std::map<std::uint32_t, std::size_t> byId;
    std::vector<std::string_view> row;
    bool hasRow{};
    while (reader.Next(row, hasRow, error) && hasRow) {
        // ObjectsTxt IDs are row ordinals. The descriptive *ID field is not
        // consumed because D2R ignores every '*' comment column.
        if (row[columns[0]] == "Expansion") continue;
        if (records.size() >= limits.maximumRowsPerFamily) {
            error = "Objects.txt exceeds the configured row bound";
            return false;
        }
        DataCatalogObject record{
            .objectId = static_cast<std::uint32_t>(records.size()),
        };
        if (!ParseUnsigned(row[columns[2]], true, record.subClass)
            || !ParseFlag(row[columns[3]], record.lockable)
            || !ParseUnsigned(row[columns[4]], true, record.operateFn)
            || !ParseUnsigned(row[columns[5]], true, record.populateFn)
            || !ParseUnsigned(row[columns[6]], true, record.initFn)
            || !ParseUnsigned(row[columns[7]], true, record.clientFn)
            || !ParseUnsigned(row[columns[8]], true,
                record.shrineFunction)) {
            error = "Objects.txt has an invalid numeric/flag value at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        if (!CopyBoundedKey(
                row[columns[0]], limits, false, record.classId)) {
            error = "Objects.txt has an invalid Class at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        std::string key;
        if (!CopyBoundedKey(row[columns[1]], limits, true, key)) {
            error = "Objects.txt has an invalid Name at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        record.name = localization.Resolve(key);
        if (!byClass.emplace(record.classId, records.size()).second) {
            error = "Objects.txt contains duplicate Class " + record.classId;
            return false;
        }
        if (!byId.emplace(record.objectId, records.size()).second) {
            error = "Objects.txt contains duplicate *ID "
                + std::to_string(record.objectId);
            return false;
        }
        CountLocalization(record.name, status);
        records.push_back(std::move(record));
    }
    if (!error.empty()) return false;
    if (records.empty()) {
        error = "Objects.txt contains no records";
        return false;
    }
    status.rowCount = records.size();
    impl.objects = std::move(records);
    impl.objectByClass = std::move(byClass);
    impl.objectById = std::move(byId);
    return true;
}

[[nodiscard]] auto ParseMissiles(
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    StrictTsvReader reader;
    if (!reader.Open(bytes, limits, error)) return false;
    std::vector<std::size_t> columns;
    if (!reader.RequireColumns({
            "Missile", "EType", "SrcDamage", "SrcMissDmg",
            "MinDamage", "MinLevDam1", "MinLevDam2", "MinLevDam3",
            "MinLevDam4", "MinLevDam5", "MaxDamage", "MaxLevDam1",
            "MaxLevDam2", "MaxLevDam3", "MaxLevDam4", "MaxLevDam5"},
            columns, error)) {
        return false;
    }
    std::vector<DataCatalogMissile> records;
    std::vector<std::string_view> row;
    bool hasRow{};
    while (reader.Next(row, hasRow, error) && hasRow) {
        if (records.size() >= limits.maximumRowsPerFamily
            || records.size()
                > static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {
            error = "Missiles.txt exceeds the configured row bound";
            return false;
        }
        DataCatalogMissile record{
            .classId = static_cast<std::uint32_t>(records.size()),
        };
        if (!CopyBoundedKey(row[columns[0]], limits, false, record.id)) {
            error = "Missiles.txt has an invalid Missile at line "
                + std::to_string(reader.LineNumber());
            return false;
        }
        auto hasPhysicalDamage = false;
        for (std::size_t columnIndex = 2U;
                columnIndex < columns.size(); ++columnIndex) {
            const auto value = row[columns[columnIndex]];
            if (value.empty()) continue;
            std::int32_t parsed{};
            if (!ParseSigned(value, parsed)) {
                error = "Missiles.txt has an invalid damage value at line "
                    + std::to_string(reader.LineNumber());
                return false;
            }
            hasPhysicalDamage = hasPhysicalDamage || parsed > 0;
        }
        record.element = ClassifyDataCatalogMissileElement(
            record.classId,
            row[columns[1]],
            hasPhysicalDamage);
        records.push_back(std::move(record));
    }
    if (!error.empty()) return false;
    if (records.empty()) {
        error = "Missiles.txt contains no records";
        return false;
    }
    status.rowCount = records.size();
    impl.missiles = std::move(records);
    return true;
}

[[nodiscard]] auto ParseFamily(
        DataCatalogFamily family,
        std::string_view bytes,
        const MapSenseDataCatalogLimits& limits,
        LocalizationCache& localization,
        MapSenseDataCatalog::Impl& impl,
        DataCatalogFamilyStatus& status,
        std::string& error) -> bool {
    switch (family) {
        case DataCatalogFamily::Levels:
            return ParseLevels(
                bytes, limits, localization, impl, status, error);
        case DataCatalogFamily::Shrines:
            return ParseShrines(
                bytes, limits, localization, impl, status, error);
        case DataCatalogFamily::SuperUniques:
            return ParseSuperUniques(
                bytes, limits, localization, impl, status, error);
        case DataCatalogFamily::MonStats:
            return ParseMonStats(
                bytes, limits, localization, impl, status, error);
        case DataCatalogFamily::Objects:
            return ParseObjects(
                bytes, limits, localization, impl, status, error);
        case DataCatalogFamily::Missiles:
            return ParseMissiles(bytes, limits, impl, status, error);
        case DataCatalogFamily::Count:
            break;
    }
    error = "unknown MapSense data-catalog family";
    return false;
}

} // namespace

auto MapSenseDataCatalog::Load(
        const D2RL::PluginContext* context,
        const MapSenseDataCatalogLoadOptions& options) noexcept
        -> MapSenseDataCatalogLoadResult {
    MapSenseDataCatalogLoadResult result{};
    try {
        const auto limits = ClampLimits(options.limits);
        DiagnosticSink diagnostics(result.diagnostics, limits.maximumDiagnostics);
        if (context == nullptr) {
            diagnostics.Add(
                DataCatalogDiagnosticSeverity::Error,
                DataCatalogFamily::Levels,
                "context_unavailable",
                "MapSense cannot build a session data catalog without a "
                "D2RLoader PluginContext.");
            return result;
        }

        const D2RL::LocalizationServiceV1* localizationService{};
        if (context->QueryService(
                D2RL::ServiceId::Localization,
                D2RL::LocalizationServiceV1Version,
                &localizationService) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceV1Field(
                localizationService,
                D2RL::LocalizationServiceV1RequiredSize)) {
            localizationService = nullptr;
            diagnostics.Add(
                DataCatalogDiagnosticSeverity::Warning,
                DataCatalogFamily::Levels,
                "localization_unavailable",
                "LocalizationServiceV1 is unavailable; raw TXT keys are "
                "cached but localized labels remain marked unresolved.");
        }
        LocalizationCache localization(
            context, localizationService, limits.maximumLocalizedBytes);

        auto impl = std::make_shared<Impl>();
        impl->hasLocalizationService = localizationService != nullptr;
        for (std::size_t index = 0U; index < FamilyCount; ++index) {
            impl->statuses[index].family =
                static_cast<DataCatalogFamily>(index);
        }

        const auto directories = DiscoverSourceDirectories(
            context, options, diagnostics);
        if (directories.activeInspectionAllowed) {
            impl->activeExcelDirectories = directories.active;
            for (const auto& excel : directories.active) {
                AppendUniquePath(
                    impl->activeTileDirectories,
                    excel.parent_path() / L"tiles");
            }
            impl->atlasDataFingerprint = BuildAtlasDataFingerprint(
                impl->activeExcelDirectories,
                impl->activeTileDirectories,
                limits.maximumTableBytes);
        }
        for (const auto& spec : TableSpecs) {
            auto& status = impl->statuses[FamilyIndex(spec.family)];
            if (!directories.activeInspectionAllowed) {
                status.state = DataCatalogFamilyState::Invalid;
                continue;
            }

            auto source = ResolveLayer(
                directories.active, spec, limits.maximumTableBytes);
            bool fallback{};
            if (source.state == LayerResolutionState::None) {
                source = ResolveLayer(
                    directories.vanilla, spec, limits.maximumTableBytes);
                fallback = source.state == LayerResolutionState::Text;
            }
            if (source.state == LayerResolutionState::None) {
                status.state = DataCatalogFamilyState::Unavailable;
                diagnostics.Add(
                    DataCatalogDiagnosticSeverity::Warning,
                    spec.family,
                    "table_unavailable",
                    std::string(DataCatalogFamilyName(spec.family))
                        + " has no active TXT override and no auditable "
                          "vanilla TXT fallback.");
                continue;
            }
            if (source.state == LayerResolutionState::BinaryOnly) {
                status.state = DataCatalogFamilyState::BinaryOnlyConflict;
                status.sourcePath = source.path;
                diagnostics.Add(
                    DataCatalogDiagnosticSeverity::Error,
                    spec.family,
                    "binary_without_txt",
                    source.error);
                continue;
            }
            if (source.state == LayerResolutionState::Invalid) {
                status.state = DataCatalogFamilyState::Invalid;
                status.sourcePath = source.path;
                diagnostics.Add(
                    DataCatalogDiagnosticSeverity::Error,
                    spec.family,
                    "table_source_invalid",
                    source.error);
                continue;
            }

            status.sourcePath = source.path;
            std::string parseError;
            if (!ParseFamily(
                    spec.family, source.bytes, limits, localization,
                    *impl, status, parseError)) {
                status.state = DataCatalogFamilyState::Invalid;
                status.rowCount = 0U;
                status.localizedNameCount = 0U;
                status.unresolvedNameCount = 0U;
                diagnostics.Add(
                    DataCatalogDiagnosticSeverity::Error,
                    spec.family,
                    "table_parse_failed",
                    DisplayPath(source.path) + ": " + parseError);
                continue;
            }
            status.state = fallback
                ? DataCatalogFamilyState::VanillaFallbackTxt
                : DataCatalogFamilyState::ActiveTxt;
        }

        impl->hasVerifiedPlayerFacingLocalization =
            localization.HasVerifiedPlayerFacingLocalization();
        if (impl->hasVerifiedPlayerFacingLocalization) {
            RefreshVerifiedExactLocalizations(localization, *impl);
        }
        RecountCatalogLocalizations(*impl);
        for (const auto& spec : TableSpecs) {
            const auto& status = impl->statuses[FamilyIndex(spec.family)];
            if (status.unresolvedNameCount == 0U) continue;
            diagnostics.Add(
                DataCatalogDiagnosticSeverity::Warning,
                spec.family,
                "localization_incomplete",
                std::string(DataCatalogFamilyName(spec.family)) + " has "
                    + std::to_string(status.unresolvedNameCount)
                    + " unresolved localization key(s); raw keys are "
                      "retained and explicitly marked.");
        }

        result.catalog = std::shared_ptr<const MapSenseDataCatalog>(
            new MapSenseDataCatalog(std::move(impl)));
        return result;
    } catch (const std::exception& exception) {
        if (result.diagnostics.size() < HardMaximumDiagnostics) {
            result.diagnostics.push_back({
                .severity = DataCatalogDiagnosticSeverity::Error,
                .family = DataCatalogFamily::Levels,
                .code = "catalog_exception",
                .message = exception.what(),
            });
        }
        result.catalog.reset();
        return result;
    } catch (...) {
        if (result.diagnostics.size() < HardMaximumDiagnostics) {
            result.diagnostics.push_back({
                .severity = DataCatalogDiagnosticSeverity::Error,
                .family = DataCatalogFamily::Levels,
                .code = "catalog_exception",
                .message = "unknown exception while building the session "
                    "data catalog",
            });
        }
        result.catalog.reset();
        return result;
    }
}

MapSenseDataCatalog::MapSenseDataCatalog(
        std::shared_ptr<const Impl> impl) noexcept
    : impl_(std::move(impl)) {}

MapSenseDataCatalog::~MapSenseDataCatalog() = default;

auto MapSenseDataCatalog::FindLevel(std::int32_t id) const noexcept
        -> const DataCatalogLevel* {
    if (!impl_) return nullptr;
    const auto found = impl_->levelById.find(id);
    return found == impl_->levelById.end()
        ? nullptr : &impl_->levels[found->second];
}

auto MapSenseDataCatalog::Levels() const noexcept
        -> std::span<const DataCatalogLevel> {
    return impl_ ? std::span<const DataCatalogLevel>{impl_->levels}
                 : std::span<const DataCatalogLevel>{};
}

auto MapSenseDataCatalog::ResolveLevelAtlasPlacement(
        std::int32_t id,
        std::uint8_t difficulty,
        DataCatalogLevelAtlasPlacement& output) const noexcept -> bool {
    output = {};
    if (!impl_ || id <= 0 || difficulty >= 3U) return false;
    const auto* const target = FindLevel(id);
    if (target == nullptr || !target->hasAtlasPlacement
        || target->act < 0 || target->act >= 5
        || target->sizeX[difficulty] <= 0
        || target->sizeY[difficulty] <= 0) {
        return false;
    }

    // Depend chains in the governed tables are shallow, but a mod can supply
    // arbitrary data. Bound traversal without allocating on a hot/session
    // path and reject cycles deterministically.
    constexpr std::size_t MaximumDependDepth = 64U;
    std::array<std::int32_t, MaximumDependDepth> visited{};
    std::size_t visitedCount{};
    std::int64_t originTileX{};
    std::int64_t originTileY{};
    const DataCatalogLevel* current = target;
    for (;;) {
        if (current == nullptr || !current->hasAtlasPlacement
            || current->act != target->act
            || visitedCount >= visited.size()) {
            return false;
        }
        if (std::find(
                visited.begin(),
                visited.begin() + static_cast<std::ptrdiff_t>(visitedCount),
                current->id)
            != visited.begin()
                + static_cast<std::ptrdiff_t>(visitedCount)) {
            return false;
        }
        visited[visitedCount++] = current->id;
        originTileX += current->offsetX;
        originTileY += current->offsetY;
        if (current->depend == 0) break;
        current = FindLevel(current->depend);
    }

    constexpr std::int64_t SubtilesPerGameTile = 5;
    const auto originSubtileX = originTileX * SubtilesPerGameTile;
    const auto originSubtileY = originTileY * SubtilesPerGameTile;
    const auto anchorSubtileX = originSubtileX
        + static_cast<std::int64_t>(target->sizeX[difficulty])
            * SubtilesPerGameTile / 2;
    const auto anchorSubtileY = originSubtileY
        + static_cast<std::int64_t>(target->sizeY[difficulty])
            * SubtilesPerGameTile / 2;
    constexpr auto Maximum = static_cast<std::int64_t>(
        (std::numeric_limits<std::int32_t>::max)());
    if (originSubtileX < 0 || originSubtileY < 0
        || anchorSubtileX < 0 || anchorSubtileY < 0
        || originSubtileX > Maximum || originSubtileY > Maximum
        || anchorSubtileX > Maximum || anchorSubtileY > Maximum) {
        return false;
    }
    output = {
        .originSubtileX = static_cast<std::int32_t>(originSubtileX),
        .originSubtileY = static_cast<std::int32_t>(originSubtileY),
        .anchorSubtileX = static_cast<std::int32_t>(anchorSubtileX),
        .anchorSubtileY = static_cast<std::int32_t>(anchorSubtileY),
    };
    return true;
}

auto MapSenseDataCatalog::FindShrineByRow(std::uint32_t row) const noexcept
        -> const DataCatalogShrine* {
    if (!impl_) return nullptr;
    const auto found = impl_->shrineByRow.find(row);
    return found == impl_->shrineByRow.end()
        ? nullptr : &impl_->shrines[found->second];
}

auto MapSenseDataCatalog::ShrineRowsForCode(std::uint32_t code) const noexcept
        -> std::span<const std::uint32_t> {
    if (!impl_) return {};
    const auto found = impl_->shrineRowsByCode.find(code);
    return found == impl_->shrineRowsByCode.end()
        ? std::span<const std::uint32_t>{}
        : std::span<const std::uint32_t>{found->second};
}

auto MapSenseDataCatalog::FindSuperUnique(std::uint32_t hcIdx) const noexcept
        -> const DataCatalogSuperUnique* {
    if (!impl_) return nullptr;
    const auto found = impl_->superUniqueByHcIdx.find(hcIdx);
    return found == impl_->superUniqueByHcIdx.end()
        ? nullptr : &impl_->superUniques[found->second];
}

auto MapSenseDataCatalog::FindMonStats(std::uint32_t hcIdx) const noexcept
        -> const DataCatalogMonStats* {
    if (!impl_) return nullptr;
    const auto found = impl_->monStatsByHcIdx.find(hcIdx);
    return found == impl_->monStatsByHcIdx.end()
        ? nullptr : &impl_->monStats[found->second];
}

auto MapSenseDataCatalog::FindObject(std::string_view classId) const noexcept
        -> const DataCatalogObject* {
    if (!impl_) return nullptr;
    const auto found = impl_->objectByClass.find(classId);
    return found == impl_->objectByClass.end()
        ? nullptr : &impl_->objects[found->second];
}

auto MapSenseDataCatalog::FindObjectById(std::uint32_t objectId) const noexcept
        -> const DataCatalogObject* {
    if (!impl_) return nullptr;
    const auto found = impl_->objectById.find(objectId);
    return found == impl_->objectById.end()
        ? nullptr : &impl_->objects[found->second];
}

auto MapSenseDataCatalog::FindMissile(std::uint32_t classId) const noexcept
        -> const DataCatalogMissile* {
    return impl_ && classId < impl_->missiles.size()
        ? &impl_->missiles[classId]
        : nullptr;
}

auto MapSenseDataCatalog::FamilyStatus(DataCatalogFamily family) const noexcept
        -> const DataCatalogFamilyStatus& {
    static const DataCatalogFamilyStatus unavailable{};
    if (!impl_ || family == DataCatalogFamily::Count) return unavailable;
    return impl_->statuses[FamilyIndex(family)];
}

auto MapSenseDataCatalog::FamilyStatuses() const noexcept
        -> std::span<const DataCatalogFamilyStatus> {
    return impl_ ? std::span<const DataCatalogFamilyStatus>{impl_->statuses}
        : std::span<const DataCatalogFamilyStatus>{};
}

auto MapSenseDataCatalog::ActiveExcelDirectories() const noexcept
        -> std::span<const std::filesystem::path> {
    return impl_
        ? std::span<const std::filesystem::path>{
            impl_->activeExcelDirectories}
        : std::span<const std::filesystem::path>{};
}

auto MapSenseDataCatalog::ActiveTileDirectories() const noexcept
        -> std::span<const std::filesystem::path> {
    return impl_
        ? std::span<const std::filesystem::path>{
            impl_->activeTileDirectories}
        : std::span<const std::filesystem::path>{};
}

auto MapSenseDataCatalog::AtlasDataFingerprint() const noexcept
        -> std::uint64_t {
    return impl_ ? impl_->atlasDataFingerprint : 0U;
}

auto MapSenseDataCatalog::HasLocalizationService() const noexcept -> bool {
    return impl_ && impl_->hasLocalizationService;
}

auto MapSenseDataCatalog::HasVerifiedPlayerFacingLocalization() const noexcept
        -> bool {
    return impl_ && impl_->hasVerifiedPlayerFacingLocalization;
}

auto MapSenseDataCatalog::AllFamiliesAvailable() const noexcept -> bool {
    return impl_ && std::all_of(
        impl_->statuses.begin(), impl_->statuses.end(),
        [](const auto& status) { return status.Available(); });
}

auto DataCatalogFamilyName(DataCatalogFamily family) noexcept
        -> std::string_view {
    switch (family) {
        case DataCatalogFamily::Levels: return "levels";
        case DataCatalogFamily::Shrines: return "shrines";
        case DataCatalogFamily::SuperUniques: return "superuniques";
        case DataCatalogFamily::MonStats: return "monstats";
        case DataCatalogFamily::Objects: return "objects";
        case DataCatalogFamily::Missiles: return "missiles";
        case DataCatalogFamily::Count: break;
    }
    return "unknown";
}

} // namespace RuffnecKk::MapSense
