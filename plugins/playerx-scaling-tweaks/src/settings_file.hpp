#pragma once
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace ruffneckk::player_scaling::menu {
inline bool ReadFileDocument(const std::filesystem::path& path, std::string& document) {
    if (path.empty() || std::filesystem::file_size(path) > 1024 * 1024) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    document.assign(std::istreambuf_iterator<char>(input), {});
    return !input.bad();
}
inline bool SaveFileDocument(const std::filesystem::path& path,
        const std::string& expected, const std::string& updated) {
    // Hold a read handle denying concurrent in-place writes until replacement.
    const auto original = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (original == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    bool created = false;
    std::filesystem::path temporary;
    HANDLE output = INVALID_HANDLE_VALUE;
    try {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(original, &size) && size.QuadPart >= 0
                && static_cast<unsigned long long>(size.QuadPart) == expected.size()) {
            std::string actual(expected.size(), '\0');
            DWORD read{};
            if (ReadFile(original, actual.data(), static_cast<DWORD>(actual.size()), &read, nullptr)
                    && read == actual.size() && actual == expected) {
                static std::atomic_uint64_t sequence{};
                temporary = path;
                temporary += L".playerx-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                    + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
                output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (output != INVALID_HANDLE_VALUE) {
                    created = true;
                    DWORD written{};
                    ok = WriteFile(output, updated.data(), static_cast<DWORD>(updated.size()), &written, nullptr)
                        && written == updated.size() && FlushFileBuffers(output);
                    CloseHandle(output); output = INVALID_HANDLE_VALUE;
                    if (ok) ok = ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr) != FALSE;
                }
            }
        }
    } catch (...) { ok = false; }
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (created && !ok) DeleteFileW(temporary.c_str());
    CloseHandle(original);
    return ok;
}
} // namespace ruffneckk::player_scaling::menu
