#include "native_dropdown_art.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace MapSeedSelector::NativeDropdownArt {
namespace {

constexpr std::uintptr_t FileSizeRva = 0xA00050;
constexpr std::uintptr_t FileReadRva = 0xA00EC0;
constexpr std::uintptr_t FileCloseRva = 0x9FFE50;
constexpr std::uint16_t FileOpenOrdinal = 160;
constexpr std::size_t SpriteHeaderSize = 40;
constexpr std::size_t MaximumArchiveFileBytes = 64U * 1024U * 1024U;

constexpr std::array<std::uint8_t, 7> FileSizeWitness{
    0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x30,
};
constexpr std::array<std::uint8_t, 10> FileReadWitness{
    0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10,
};
constexpr std::array<std::uint8_t, 1> FileCloseWitness{0xE9};

using FileOpenFunction = void* (*)(const char*) noexcept;
using FileSizeFunction = std::uint64_t (*)(void*) noexcept;
using FileReadFunction = bool (*)(void*, void*, std::uint64_t,
    std::uint64_t*) noexcept;
using FileCloseFunction = void (*)(void*) noexcept;

struct Frame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels{};
};

auto ReadU16(const std::string& data, std::size_t offset) noexcept
        -> std::uint16_t {
    return static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(data[offset])
        | (static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(data[offset + 1])) << 8U));
}

auto ReadU32(const std::string& data, std::size_t offset) noexcept
        -> std::uint32_t {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(data[offset + index]))
            << (8U * index);
    }
    return value;
}

void WriteUnsigned(std::vector<std::uint8_t>& bytes, std::size_t offset,
        std::uint32_t value, std::size_t byteCount) noexcept {
    for (std::size_t index = 0; index < byteCount; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (8U * index)) & 0xFFU);
    }
}

auto ReadArchiveFile(const D2RL::PluginContext* context,
        const char* path, std::string& bytes, std::string& error) -> bool {
    if (context == nullptr
            || !context->CheckExpectedBytes(FileSizeRva,
                FileSizeWitness.data(),
                static_cast<std::uint32_t>(FileSizeWitness.size()))
            || !context->CheckExpectedBytes(FileReadRva,
                FileReadWitness.data(),
                static_cast<std::uint32_t>(FileReadWitness.size()))
            || !context->CheckExpectedBytes(FileCloseRva,
                FileCloseWitness.data(),
                static_cast<std::uint32_t>(FileCloseWitness.size()))) {
        error = "the game's archive file functions did not match their validated signatures";
        return false;
    }

    HMODULE core = GetModuleHandleW(L"D2RCore.dll");
    if (core == nullptr) {
        error = "D2RCore.dll is not loaded";
        return false;
    }
    const auto open = reinterpret_cast<FileOpenFunction>(
        GetProcAddress(core, MAKEINTRESOURCEA(FileOpenOrdinal)));
    if (open == nullptr) {
        error = "D2RCore archive-open export is unavailable";
        return false;
    }

    void* const file = open(path);
    if (file == nullptr) {
        error = std::string("game archive file is missing: ") + path;
        return false;
    }

    const auto base = context->exeBase;
    const auto sizeFunction = reinterpret_cast<FileSizeFunction>(
        base + FileSizeRva);
    const auto readFunction = reinterpret_cast<FileReadFunction>(
        base + FileReadRva);
    const auto closeFunction = reinterpret_cast<FileCloseFunction>(
        base + FileCloseRva);
    const std::uint64_t size = sizeFunction(file);
    bool success{};
    if (size >= SpriteHeaderSize && size <= MaximumArchiveFileBytes
            && size <= (std::numeric_limits<std::size_t>::max)()) {
        bytes.resize(static_cast<std::size_t>(size));
        std::uint64_t read{};
        success = readFunction(file, bytes.data(), size, &read)
            && read == size;
    }
    closeFunction(file);
    if (!success) {
        bytes.clear();
        error = std::string("could not read game archive file: ") + path;
    }
    return success;
}

auto DecodeFrames(const std::string& bytes, std::uint32_t expectedWidth,
        std::uint32_t expectedHeight, std::uint32_t expectedCount,
        std::vector<Frame>& frames) -> bool {
    frames.clear();
    if (bytes.size() < SpriteHeaderSize || bytes.compare(0, 4, "SpA1") != 0
            || ReadU16(bytes, 4) != 31) {
        return false;
    }
    const std::uint32_t frameWidth = ReadU16(bytes, 6);
    const std::uint32_t sheetWidth = ReadU32(bytes, 8);
    const std::uint32_t height = ReadU32(bytes, 12);
    const std::uint32_t frameCount = ReadU32(bytes, 20);
    if (frameWidth != expectedWidth || height != expectedHeight
            || frameCount != expectedCount
            || sheetWidth < frameWidth * frameCount
            || sheetWidth % frameCount != 0
            || height > 4096 || sheetWidth > 8192) {
        return false;
    }
    // D2R's dropdown button sheets reserve two horizontal pixels per frame.
    // The logical frame width excludes this padding, so adjacent frames start
    // at the sheet stride rather than at frameWidth intervals.
    const std::uint32_t frameStride = sheetWidth / frameCount;
    if (frameStride != frameWidth && frameStride != frameWidth + 2U) {
        return false;
    }
    const std::uint64_t pixelBytes = static_cast<std::uint64_t>(sheetWidth)
        * height * 4U;
    if (pixelBytes > bytes.size() - SpriteHeaderSize) return false;

    frames.reserve(frameCount);
    for (std::uint32_t frameIndex = 0; frameIndex < frameCount;
            ++frameIndex) {
        Frame frame{
            .width = frameWidth,
            .height = height,
            .pixels = std::vector<std::uint8_t>(
                static_cast<std::size_t>(frameWidth) * height * 4U),
        };
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto sourceOffset = SpriteHeaderSize
                + (static_cast<std::size_t>(y) * sheetWidth
                    + static_cast<std::size_t>(frameIndex) * frameStride) * 4U;
            const auto destinationOffset = static_cast<std::size_t>(y)
                * frameWidth * 4U;
            std::memcpy(frame.pixels.data() + destinationOffset,
                bytes.data() + sourceOffset,
                static_cast<std::size_t>(frameWidth) * 4U);
        }
        frames.push_back(std::move(frame));
    }
    return true;
}

void CopyScaled(const Frame& source, Frame& destination,
        std::uint32_t destinationX, std::uint32_t destinationWidth) noexcept {
    for (std::uint32_t y = 0; y < destination.height; ++y) {
        const std::uint32_t sourceY = y * source.height / destination.height;
        for (std::uint32_t x = 0; x < destinationWidth; ++x) {
            const std::uint32_t sourceX = x * source.width / destinationWidth;
            const auto sourceOffset = (static_cast<std::size_t>(sourceY)
                * source.width + sourceX) * 4U;
            const auto destinationOffset = (static_cast<std::size_t>(y)
                * destination.width + destinationX + x) * 4U;
            std::memcpy(destination.pixels.data() + destinationOffset,
                source.pixels.data() + sourceOffset, 4U);
        }
    }
}

auto JoinFrames(const std::vector<Frame>& leftFrames,
        const std::vector<Frame>& middleFrames,
        const std::vector<Frame>& rightFrames,
        std::uint32_t outputWidth, std::vector<Frame>& outputFrames) -> bool {
    if (leftFrames.empty() || middleFrames.empty() || rightFrames.empty()
            || leftFrames.size() != middleFrames.size()
            || leftFrames.size() != rightFrames.size()) {
        return false;
    }
    const std::uint32_t height = leftFrames.front().height;
    const std::uint32_t leftWidth = leftFrames.front().width;
    const std::uint32_t rightSourceWidth = rightFrames.front().width;
    const std::uint32_t middleWidth = middleFrames.front().width;
    if (height == 0 || leftWidth == 0 || rightSourceWidth == 0
            || middleWidth == 0 || outputWidth <= leftWidth + rightSourceWidth) {
        return false;
    }
    const std::uint32_t repeats = (outputWidth - leftWidth
        - rightSourceWidth) / middleWidth;
    const std::uint32_t rightStart = outputWidth - rightSourceWidth;
    const std::uint32_t middleSpace = rightStart - leftWidth;
    if (repeats == 0 || middleSpace == 0) return false;

    outputFrames.clear();
    outputFrames.reserve(leftFrames.size());
    for (std::size_t frameIndex = 0; frameIndex < leftFrames.size();
            ++frameIndex) {
        const auto& left = leftFrames[frameIndex];
        const auto& middle = middleFrames[frameIndex];
        const auto& right = rightFrames[frameIndex];
        if (left.height != height || middle.height != height
                || right.height != height) {
            return false;
        }
        Frame output{
            .width = outputWidth,
            .height = height,
            .pixels = std::vector<std::uint8_t>(
                static_cast<std::size_t>(outputWidth) * height * 4U),
        };
        CopyScaled(left, output, 0, leftWidth);
        for (std::uint32_t tileX = leftWidth; tileX < rightStart;
                tileX += middleWidth) {
            const std::uint32_t copyWidth = (std::min)(middleWidth,
                rightStart - tileX);
            for (std::uint32_t y = 0; y < height; ++y) {
                const auto sourceOffset = static_cast<std::size_t>(y)
                    * middle.width * 4U;
                const auto destinationOffset = (static_cast<std::size_t>(y)
                    * output.width + tileX) * 4U;
                std::memcpy(output.pixels.data() + destinationOffset,
                    middle.pixels.data() + sourceOffset,
                    static_cast<std::size_t>(copyWidth) * 4U);
            }
        }
        CopyScaled(right, output, rightStart, rightSourceWidth);
        outputFrames.push_back(std::move(output));
    }
    return true;
}

auto HalfFrames(const std::vector<Frame>& frames,
        std::vector<Frame>& output) -> bool {
    output.clear();
    output.reserve(frames.size());
    for (const auto& source : frames) {
        const std::uint32_t width = (std::max)(1U, source.width / 2U);
        const std::uint32_t height = (std::max)(1U, source.height / 2U);
        Frame frame{
            .width = width,
            .height = height,
            .pixels = std::vector<std::uint8_t>(
                static_cast<std::size_t>(width) * height * 4U),
        };
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto sourceOffset = (static_cast<std::size_t>(y * 2U)
                    * source.width + x * 2U) * 4U;
                const auto destinationOffset = (static_cast<std::size_t>(y)
                    * width + x) * 4U;
                std::memcpy(frame.pixels.data() + destinationOffset,
                    source.pixels.data() + sourceOffset, 4U);
            }
        }
        output.push_back(std::move(frame));
    }
    return true;
}

auto EncodeFrames(const std::vector<Frame>& frames,
        std::vector<std::uint8_t>& bytes) -> bool {
    if (frames.empty() || frames.size() > 32) return false;
    const auto width = frames.front().width;
    const auto height = frames.front().height;
    if (width == 0 || width > (std::numeric_limits<std::uint16_t>::max)()
            || height == 0) {
        return false;
    }
    for (const auto& frame : frames) {
        if (frame.width != width || frame.height != height
                || frame.pixels.size()
                    != static_cast<std::size_t>(width) * height * 4U) {
            return false;
        }
    }
    const std::uint64_t imageBytes = static_cast<std::uint64_t>(width)
        * frames.size() * height * 4U;
    if (imageBytes + SpriteHeaderSize > MaximumArchiveFileBytes) return false;

    bytes.assign(SpriteHeaderSize + static_cast<std::size_t>(imageBytes), 0);
    std::memcpy(bytes.data(), "SpA1", 4);
    WriteUnsigned(bytes, 4, 31, 2);
    WriteUnsigned(bytes, 6, width, 2);
    WriteUnsigned(bytes, 8,
        width * static_cast<std::uint32_t>(frames.size()), 4);
    WriteUnsigned(bytes, 12, height, 4);
    WriteUnsigned(bytes, 20, static_cast<std::uint32_t>(frames.size()), 4);
    auto* destination = bytes.data() + SpriteHeaderSize;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (const auto& frame : frames) {
            const auto offset = static_cast<std::size_t>(y) * width * 4U;
            std::memcpy(destination, frame.pixels.data() + offset,
                static_cast<std::size_t>(width) * 4U);
            destination += static_cast<std::size_t>(width) * 4U;
        }
    }
    return true;
}

auto Compose(const std::array<std::string, 3>& paths,
        const std::array<std::uint32_t, 3>& widths,
        std::uint32_t height, std::vector<std::uint8_t>& standard,
        std::vector<std::uint8_t>& lowEnd, const D2RL::PluginContext* context,
        std::string& error) -> bool {
    std::array<std::vector<Frame>, 3> pieces{};
    for (std::size_t index = 0; index < paths.size(); ++index) {
        std::string bytes;
        if (!ReadArchiveFile(context, paths[index].c_str(), bytes, error)) {
            return false;
        }
        if (!DecodeFrames(bytes, widths[index], height, 4, pieces[index])) {
            error = std::string("unexpected sprite layout in game asset: ")
                + paths[index];
            return false;
        }
    }
    std::vector<Frame> joined{};
    std::vector<Frame> half{};
    return JoinFrames(pieces[0], pieces[1], pieces[2], 448, joined)
        && HalfFrames(joined, half)
        && EncodeFrames(joined, standard)
        && EncodeFrames(half, lowEnd);
}

} // namespace

auto Load(const D2RL::PluginContext* context, SpriteBundle& sprites,
        std::string& error) noexcept -> bool {
    try {
        error.clear();
        const std::array<std::string, 3> dropdownPaths{
            "data/hd/global/ui/panel/dropdownbutton_left.sprite",
            "data/hd/global/ui/panel/dropdownbutton_center.sprite",
            "data/hd/global/ui/panel/dropdownbutton_right.sprite",
        };
        const std::array<std::uint32_t, 3> dropdownWidths{106, 13, 131};
        if (!Compose(dropdownPaths, dropdownWidths, 56,
                sprites.dropdown, sprites.dropdownLowEnd, context, error)) {
            if (error.empty()) error = "could not assemble dropdown button artwork";
            return false;
        }

        const std::array<std::string, 3> optionPaths{
            "data/hd/global/ui/panel/dropdowntile_left.sprite",
            "data/hd/global/ui/panel/dropdowntile_center.sprite",
            "data/hd/global/ui/panel/dropdowntile_right.sprite",
        };
        const std::array<std::uint32_t, 3> optionWidths{127, 4, 127};
        if (!Compose(optionPaths, optionWidths, 43,
                sprites.optionTile, sprites.optionTileLowEnd,
                context, error)) {
            if (error.empty()) error = "could not assemble dropdown option artwork";
            return false;
        }
        return true;
    } catch (...) {
        sprites = {};
        error = "allocation or sprite parsing failed while loading game artwork";
        return false;
    }
}

} // namespace MapSeedSelector::NativeDropdownArt
