#pragma once
#include "live_settings.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace RuffnecKk::LiveSettings {
inline std::string ReadFile(const std::filesystem::path& path) {
    const auto n = std::filesystem::file_size(path);
    if (n > 65535) throw std::runtime_error("Configuration exceeds 64 KiB.");
    std::ifstream input(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(n), '\0');
    if (n) input.read(text.data(), static_cast<std::streamsize>(n));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) throw std::runtime_error("Cannot read configuration.");
    return text;
}
inline bool SaveFile(const std::filesystem::path& path, std::string_view text) noexcept {
    try {
        static std::atomic<unsigned long> sequence{};
        const auto temp = std::filesystem::path(path.wstring() + L".live-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence));
        const auto file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        const auto ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
            && written == text.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) {
            // Readers/antivirus can briefly hold a file between adjacent hub
            // edits. Retry only sharing/access contention, retaining atomicity.
            for (unsigned attempt=0; attempt<6; ++attempt) {
                if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
                const auto reason=GetLastError();
                if (reason!=ERROR_SHARING_VIOLATION && reason!=ERROR_ACCESS_DENIED && reason!=ERROR_USER_MAPPED_FILE) break;
                if (attempt<5) Sleep(10);
            }
        }
        DeleteFileW(temp.c_str());
    } catch (...) {}
    return false;
}
}
