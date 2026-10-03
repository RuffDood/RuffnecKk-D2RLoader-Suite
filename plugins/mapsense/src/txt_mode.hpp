#pragma once

#include <filesystem>
#include <string_view>

namespace RuffnecKk::MapSense {

enum class TxtModeSource {
    Disabled,
    ProcessArguments,
    LoaderConfiguration,
    Unavailable,
};

[[nodiscard]] auto ResolveTxtMode(
    std::wstring_view processCommandLine,
    const std::filesystem::path& executablePath) noexcept -> TxtModeSource;

[[nodiscard]] auto CaptureTxtMode() noexcept -> TxtModeSource;

[[nodiscard]] constexpr auto TxtModeEnabled(TxtModeSource source) noexcept -> bool {
    return source == TxtModeSource::ProcessArguments
        || source == TxtModeSource::LoaderConfiguration;
}

} // namespace RuffnecKk::MapSense
