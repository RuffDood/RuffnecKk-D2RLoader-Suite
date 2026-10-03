#include "txt_mode.hpp"

#include <Windows.h>
#include <shellapi.h>
#include <toml++/toml.hpp>

#include <array>
#include <fstream>
#include <string>

namespace RuffnecKk::MapSense {
namespace {

[[nodiscard]] auto HasTxtArgument(std::wstring_view commandLine) -> bool {
    const std::wstring terminated(commandLine);
    int argumentCount{};
    auto** const arguments = CommandLineToArgvW(terminated.c_str(), &argumentCount);
    if (arguments == nullptr) return false;
    bool enabled = false;
    for (int index = 1; index < argumentCount; ++index) {
        if (CompareStringOrdinal(arguments[index], -1, L"-txt", -1, TRUE)
                == CSTR_EQUAL) {
            enabled = true;
            break;
        }
    }
    LocalFree(arguments);
    return enabled;
}

} // namespace

auto ResolveTxtMode(std::wstring_view processCommandLine,
                    const std::filesystem::path& executablePath) noexcept
        -> TxtModeSource {
    try {
        if (HasTxtArgument(processCommandLine)) return TxtModeSource::ProcessArguments;

        // Only the global loader configuration contributes launch_arguments.
        // A mod-local plugin config or the current working directory is not a
        // substitute. Read once at load, not when a new game starts.
        if (executablePath.empty() || !executablePath.is_absolute()) {
            return TxtModeSource::Unavailable;
        }
        const auto loaderConfigPath = executablePath.parent_path()
            / L"d2rloader" / L"config" / L"d2rloader.toml";
        std::error_code error;
        const auto exists = std::filesystem::exists(loaderConfigPath, error);
        if (error) return TxtModeSource::Unavailable;
        if (!exists) return TxtModeSource::Disabled;
        std::ifstream input(loaderConfigPath, std::ios::binary);
        if (!input) return TxtModeSource::Unavailable;
        constexpr std::size_t MaximumLoaderConfigBytes = 1024 * 1024;
        std::string document(MaximumLoaderConfigBytes + 1, '\0');
        input.read(document.data(), static_cast<std::streamsize>(document.size()));
        const auto size = static_cast<std::size_t>(input.gcount());
        if (input.bad() || size > MaximumLoaderConfigBytes) return TxtModeSource::Unavailable;
        document.resize(size);
        const auto table = toml::parse(document);
        const auto node = table["d2rloader"]["launch_arguments"];
        if (!node) return TxtModeSource::Disabled;
        const auto arguments = node.value<std::string>();
        if (!arguments) return TxtModeSource::Unavailable;
        if (arguments->empty()) return TxtModeSource::Disabled;
        if (arguments->find('\0') != std::string::npos) return TxtModeSource::Unavailable;

        const auto wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            arguments->data(), static_cast<int>(arguments->size()), nullptr, 0);
        if (wideSize <= 0) return TxtModeSource::Unavailable;
        // Prefix an executable token: CommandLineToArgvW treats argv[0]
        // specially, and the first configured option must not be skipped.
        std::wstring commandLine = L"D2RLoader.exe ";
        const auto offset = commandLine.size();
        commandLine.resize(offset + static_cast<std::size_t>(wideSize));
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, arguments->data(),
                static_cast<int>(arguments->size()), commandLine.data() + offset,
                wideSize) != wideSize) return TxtModeSource::Unavailable;
        return HasTxtArgument(commandLine)
            ? TxtModeSource::LoaderConfiguration : TxtModeSource::Disabled;
    } catch (...) {
        return TxtModeSource::Unavailable;
    }
}

auto CaptureTxtMode() noexcept -> TxtModeSource {
    try {
        std::array<wchar_t, 32768> executable{};
        const auto length = GetModuleFileNameW(nullptr, executable.data(),
            static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) {
            return HasTxtArgument(GetCommandLineW())
                ? TxtModeSource::ProcessArguments : TxtModeSource::Unavailable;
        }
        return ResolveTxtMode(GetCommandLineW(), executable.data());
    } catch (...) {
        return TxtModeSource::Unavailable;
    }
}

} // namespace RuffnecKk::MapSense
