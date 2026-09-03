#include "external_atlas_cache.hpp"

#include <Windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace RuffnecKk::MapSense {
namespace {

[[nodiscard]] auto ValidKey(ExternalAtlasCacheKey key) noexcept -> bool {
    return key.seed != 0U && key.difficulty <= 2U && key.act < 5U;
}

[[nodiscard]] auto BuildRevealMapIntentPath(
        const std::filesystem::path& root,
        std::uint32_t seed,
        std::uint8_t difficulty,
        std::filesystem::path& output) noexcept -> bool {
    output.clear();
    std::filesystem::path actPath;
    if (!BuildExternalAtlasCachePath(
            root,
            ExternalAtlasCacheKey{
                .seed = seed,
                .difficulty = difficulty,
                .act = 0U,
            },
            actPath)) {
        return false;
    }
    try {
        output = actPath.parent_path() / L"reveal-map.intent";
    } catch (...) {
        output.clear();
        return false;
    }
    return true;
}

[[nodiscard]] auto ReadBoundedFile(
        const std::filesystem::path& path,
        std::vector<std::uint8_t>& output) -> ExternalAtlasCacheResult {
    output.clear();
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return error ? ExternalAtlasCacheResult::IoFailure
            : ExternalAtlasCacheResult::Miss;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < ExternalAtlasGeometryHeaderBytes
        || size > ExternalAtlasGeometryMaximumBytes
        || size > static_cast<std::uintmax_t>(
            (std::numeric_limits<std::size_t>::max)())) {
        return error ? ExternalAtlasCacheResult::IoFailure
            : ExternalAtlasCacheResult::Invalid;
    }
    try {
        output.resize(static_cast<std::size_t>(size));
    } catch (...) {
        return ExternalAtlasCacheResult::IoFailure;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return ExternalAtlasCacheResult::IoFailure;
    input.read(
        reinterpret_cast<char*>(output.data()),
        static_cast<std::streamsize>(output.size()));
    return input && input.peek() == std::char_traits<char>::eof()
        ? ExternalAtlasCacheResult::Hit
        : ExternalAtlasCacheResult::IoFailure;
}

} // namespace

auto ResolveExternalAtlasCacheRoot(
        std::filesystem::path& output) noexcept -> bool {
    output.clear();
    PWSTR localAppData{};
    if (SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppData) != S_OK
        || localAppData == nullptr) {
        return false;
    }
    try {
        output = std::filesystem::path(localAppData)
            / L"RuffnecKk" / L"MapSense" / L"atlas-cache";
    } catch (...) {
        CoTaskMemFree(localAppData);
        output.clear();
        return false;
    }
    CoTaskMemFree(localAppData);
    return !output.empty();
}

auto BuildExternalAtlasCachePath(
        const std::filesystem::path& root,
        ExternalAtlasCacheKey key,
        std::filesystem::path& output) noexcept -> bool {
    output.clear();
    if (root.empty() || !ValidKey(key)) return false;
    std::array<wchar_t, 64U> seedDirectory{};
    std::array<wchar_t, 32U> difficultyDirectory{};
    std::array<wchar_t, 32U> actFile{};
    if (swprintf_s(
            seedDirectory.data(),
            seedDirectory.size(),
            L"seed-%08X",
            key.seed) <= 0
        || swprintf_s(
            difficultyDirectory.data(),
            difficultyDirectory.size(),
            L"difficulty-%u",
            static_cast<unsigned>(key.difficulty)) <= 0
        || swprintf_s(
            actFile.data(),
            actFile.size(),
            L"act-%u-r%u.msa",
            static_cast<unsigned>(key.act),
            ExternalAtlasGeometryCacheRevision) <= 0) {
        return false;
    }
    try {
        auto base = root
            / (L"v" + std::to_wstring(ExternalAtlasCacheRevision));
        if (key.dataFingerprint != 0U) {
            std::array<wchar_t, 40U> dataDirectory{};
            if (swprintf_s(
                    dataDirectory.data(),
                    dataDirectory.size(),
                    L"data-%016llX",
                    static_cast<unsigned long long>(
                        key.dataFingerprint)) <= 0) {
                return false;
            }
            base /= dataDirectory.data();
        }
        output = base / seedDirectory.data()
            / difficultyDirectory.data()
            / actFile.data();
    } catch (...) {
        output.clear();
        return false;
    }
    return true;
}

auto LoadExternalAtlasGeometryCache(
        const std::filesystem::path& root,
        ExternalAtlasCacheKey key,
        ExternalAtlasGeometry& output,
        ExternalAtlasGeometryParseError* parseError) noexcept
        -> ExternalAtlasCacheResult {
    output = {};
    if (parseError != nullptr) {
        *parseError = ExternalAtlasGeometryParseError::None;
    }
    std::filesystem::path path;
    if (!BuildExternalAtlasCachePath(root, key, path)) {
        return ExternalAtlasCacheResult::Invalid;
    }
    try {
        std::vector<std::uint8_t> bytes;
        const auto read = ReadBoundedFile(path, bytes);
        if (read != ExternalAtlasCacheResult::Hit) return read;
        return ParseExternalAtlasGeometry(
            bytes,
            key.seed,
            key.difficulty,
            key.act,
            output,
            parseError)
            ? ExternalAtlasCacheResult::Hit
            : ExternalAtlasCacheResult::Invalid;
    } catch (...) {
        output = {};
        return ExternalAtlasCacheResult::IoFailure;
    }
}

auto StoreExternalAtlasGeometryCache(
        const std::filesystem::path& root,
        ExternalAtlasCacheKey key,
        std::span<const std::uint8_t> bytes) noexcept -> bool {
    std::filesystem::path target;
    if (!BuildExternalAtlasCachePath(root, key, target)) return false;
    try {
        ExternalAtlasGeometry validated;
        if (!ParseExternalAtlasGeometry(
                bytes,
                key.seed,
                key.difficulty,
                key.act,
                validated)) {
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) return false;
        auto temporary = target;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64());
        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        if (MoveFileExW(
                temporary.c_str(),
                target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

auto HasExternalAtlasRevealMapIntent(
        const std::filesystem::path& root,
        std::uint32_t seed,
        std::uint8_t difficulty) noexcept -> bool {
    std::filesystem::path path;
    if (!BuildRevealMapIntentPath(root, seed, difficulty, path)) {
        return false;
    }
    try {
        std::array<std::uint8_t, 16U> bytes{};
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input || input.peek() != std::char_traits<char>::eof()) {
            return false;
        }
        return bytes[0U] == 'M' && bytes[1U] == 'S'
            && bytes[2U] == 'I' && bytes[3U] == '1'
            && bytes[4U] == 1U && bytes[5U] == 0U
            && bytes[6U] == 0U && bytes[7U] == 0U
            && bytes[8U] == static_cast<std::uint8_t>(seed)
            && bytes[9U] == static_cast<std::uint8_t>(seed >> 8U)
            && bytes[10U] == static_cast<std::uint8_t>(seed >> 16U)
            && bytes[11U] == static_cast<std::uint8_t>(seed >> 24U)
            && bytes[12U] == difficulty
            && bytes[13U] == 0U && bytes[14U] == 0U
            && bytes[15U] == 0U;
    } catch (...) {
        return false;
    }
}

auto StoreExternalAtlasRevealMapIntent(
        const std::filesystem::path& root,
        std::uint32_t seed,
        std::uint8_t difficulty,
        bool enabled) noexcept -> bool {
    std::filesystem::path target;
    if (!BuildRevealMapIntentPath(root, seed, difficulty, target)) {
        return false;
    }
    try {
        std::error_code error;
        if (!enabled) {
            if (!std::filesystem::exists(target, error)) return !error;
            return std::filesystem::remove(target, error) && !error;
        }
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) return false;
        const std::array<std::uint8_t, 16U> bytes{
            'M', 'S', 'I', '1',
            1U, 0U, 0U, 0U,
            static_cast<std::uint8_t>(seed),
            static_cast<std::uint8_t>(seed >> 8U),
            static_cast<std::uint8_t>(seed >> 16U),
            static_cast<std::uint8_t>(seed >> 24U),
            difficulty, 0U, 0U, 0U,
        };
        auto temporary = target;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64());
        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        if (MoveFileExW(
                temporary.c_str(),
                target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
            == FALSE) {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace RuffnecKk::MapSense
