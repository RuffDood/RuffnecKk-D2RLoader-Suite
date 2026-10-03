#include "txt_mode.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace RuffnecKk::MapSense;
namespace {
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / (L"mapsense-txt-mode-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::path executable = root / L"D2RLoader.exe";
    std::filesystem::path config = root / L"d2rloader/config/d2rloader.toml";
    Fixture() { std::filesystem::create_directories(config.parent_path()); }
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove(config, ignored);
        std::filesystem::remove(config.parent_path(), ignored);
        std::filesystem::remove(config.parent_path().parent_path(), ignored);
        std::filesystem::remove(root, ignored);
    }
    void Write(std::string_view contents) const {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << contents;
        if (!output) throw std::runtime_error("Cannot write test fixture");
    }
};
int failures{};
int checks{};
void Expect(TxtModeSource actual, TxtModeSource expected, const char* label) {
    ++checks;
    if (actual != expected || TxtModeEnabled(actual) != TxtModeEnabled(expected)) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}
}
int wmain(int argc, wchar_t** argv) {
    // Read-only probe: same resolver, without loading the plugin or game.
    if (argc == 4 && std::wstring_view(argv[1]) == L"--inspect") {
        const auto result = ResolveTxtMode(argv[3], argv[2]);
        std::cout << "TXT enabled=" << TxtModeEnabled(result)
                  << " source=" << static_cast<int>(result) << '\n';
        return TxtModeEnabled(result) ? 0 : 1;
    }
    Fixture fixture;
    constexpr auto process = L"\"C:\\Games\\D2RLoader.exe\"";
    fixture.Write("[d2rloader]\nlaunch_arguments = \"-txt\"\n");
    Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::LoaderConfiguration,
        "loader-configured -txt must admit the catalog without process arguments");
    fixture.Write("[d2rloader]\nlaunch_arguments = '-w -TXT -locale zhTW'\n");
    Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::LoaderConfiguration,
        "configured flag after another flag is case insensitive");
    fixture.Write("[d2rloader]\nlaunch_arguments = '\"-txt\"'\n");
    Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::LoaderConfiguration,
        "quoted exact argument enables TXT");
    fixture.Write("[d2rloader]\nlaunch_arguments = '-locale 中文 -txt'\n");
    Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::LoaderConfiguration,
        "UTF-8 arguments preserve TXT token");
    for (const auto* document : {
            "", "[d2rloader]\n", "[d2rloader]\nlaunch_arguments = ''\n",
            "[d2rloader]\nlaunch_arguments = '-w'\n",
            "[d2rloader]\nlaunch_arguments = '-txtx -notxt --txt'\n",
            "[d2rloader]\nlaunch_arguments = '\"a -txt b\"'\n",
            "[d2rloader]\nlaunch_arguments = '-w' # -txt\n",
            "[another_section]\nlaunch_arguments = '-txt'\n"}) {
        fixture.Write(document);
        Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::Disabled,
            "only exact argument in loader section enables TXT");
    }
    for (const auto* document : {
            "[d2rloader", "[d2rloader]\nlaunch_arguments = 42\n",
            "[d2rloader]\nlaunch_arguments = ['-txt']\n",
            "[d2rloader]\nlaunch_arguments = \"-txt\\u0000 -w\"\n"}) {
        fixture.Write(document);
        Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::Unavailable,
            "invalid configuration cannot establish TXT mode");
    }
    fixture.Write(std::string(1024 * 1024 + 1, ' '));
    Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::Unavailable,
        "oversized configuration is rejected with bounded reading");
    fixture.Write("invalid TOML");
    Expect(ResolveTxtMode(L"loader.exe -TXT", fixture.executable),
        TxtModeSource::ProcessArguments, "explicit TXT independent of config parsing");
    std::filesystem::remove(fixture.config);
    Expect(ResolveTxtMode(process, fixture.executable), TxtModeSource::Disabled,
        "missing config and absent flag keeps catalog safeguard");
    Expect(ResolveTxtMode(L"loader.exe -w -txt", fixture.executable),
        TxtModeSource::ProcessArguments, "explicit TXT without config");
    Expect(ResolveTxtMode(L"loader.exe -txtx", fixture.executable), TxtModeSource::Disabled,
        "process argument prefixes are not flags");
    Expect(ResolveTxtMode(L"loader.exe \"contains -txt\"", fixture.executable),
        TxtModeSource::Disabled, "quoted process text is not a flag");
    Expect(ResolveTxtMode(process, L"relative/D2RLoader.exe"), TxtModeSource::Unavailable,
        "working directory is not a loader root fallback");
    Expect(ResolveTxtMode(L"loader.exe -txt", {}), TxtModeSource::ProcessArguments,
        "explicit TXT survives unavailable executable path");
    std::cout << checks - failures << '/' << checks << " TXT mode checks passed\n";
    return failures == 0 ? 0 : 1;
}
