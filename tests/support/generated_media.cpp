#include "generated_media.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mediahub::test {
namespace {

void writeLittleEndian16(std::ostream& output, const std::uint16_t value) {
    const char bytes[]{static_cast<char>(value & 0xFFU),
                       static_cast<char>((value >> 8U) & 0xFFU)};
    output.write(bytes, sizeof(bytes));
}

void writeLittleEndian32(std::ostream& output, const std::uint32_t value) {
    const char bytes[]{static_cast<char>(value & 0xFFU),
                       static_cast<char>((value >> 8U) & 0xFFU),
                       static_cast<char>((value >> 16U) & 0xFFU),
                       static_cast<char>((value >> 24U) & 0xFFU)};
    output.write(bytes, sizeof(bytes));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::filesystem::path uniqueDirectory(const wchar_t* name) {
    static std::atomic_uint64_t sequence{0};
    const auto uniquePart = std::to_wstring(
                                std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                            L"-" + std::to_wstring(sequence.fetch_add(1));
    return std::filesystem::temp_directory_path() / std::filesystem::path(name) /
           uniquePart;
}

void removeDirectory(const std::filesystem::path& directory) noexcept {
    std::error_code error;
    const auto parent = directory.parent_path();
    std::filesystem::remove_all(directory, error);
    error.clear();
    std::filesystem::remove(parent, error);
}

}  // namespace

GeneratedWav::GeneratedWav(const std::chrono::milliseconds duration)
    : directory_(uniqueDirectory(L"MediaHub 测试音频")),
      path_(directory_ / std::filesystem::path(L"测试 音频.wav")) {
    try {
        std::filesystem::create_directories(directory_);
        writeFile(duration);
    } catch (...) {
        removeDirectory(directory_);
        throw;
    }
}

GeneratedWav::~GeneratedWav() {
    removeDirectory(directory_);
}

std::string GeneratedWav::source() const {
    return pathToUtf8(path_);
}

void GeneratedWav::writeFile(const std::chrono::milliseconds duration) {
    constexpr std::uint32_t kSampleRate = 8'000;
    constexpr std::uint16_t kChannels = 1;
    constexpr std::uint16_t kBitsPerSample = 16;
    constexpr std::uint16_t kBlockAlign = kChannels * kBitsPerSample / 8;
    constexpr std::uint32_t kByteRate = kSampleRate * kBlockAlign;

    const auto durationCount = std::max(duration.count(), std::chrono::milliseconds::rep{0});
    const auto sampleCount = static_cast<std::uint64_t>(durationCount) * kSampleRate / 1'000;
    constexpr auto kMaximumDataSize = std::numeric_limits<std::uint32_t>::max() - 36U;
    if (sampleCount > kMaximumDataSize / kBlockAlign) {
        throw std::invalid_argument("测试 WAV 时长过大。");
    }
    const auto dataSize = static_cast<std::uint32_t>(sampleCount * kBlockAlign);

    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("无法生成测试 WAV 文件。");
    }
    output.write("RIFF", 4);
    writeLittleEndian32(output, 36U + dataSize);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    writeLittleEndian32(output, 16U);
    writeLittleEndian16(output, 1U);
    writeLittleEndian16(output, kChannels);
    writeLittleEndian32(output, kSampleRate);
    writeLittleEndian32(output, kByteRate);
    writeLittleEndian16(output, kBlockAlign);
    writeLittleEndian16(output, kBitsPerSample);
    output.write("data", 4);
    writeLittleEndian32(output, dataSize);
    const std::vector<char> silence(dataSize, 0);
    output.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    if (!output.good()) {
        throw std::runtime_error("无法写入测试 WAV 文件。");
    }
}

GeneratedInvalidMedia::GeneratedInvalidMedia(std::wstring fileName,
                                             std::string contents)
    : directory_(uniqueDirectory(L"MediaHub 测试错误媒体")),
      path_(directory_ / std::filesystem::path(std::move(fileName))) {
    try {
        std::filesystem::create_directories(directory_);
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!output.good()) {
            throw std::runtime_error("无法生成测试错误媒体。");
        }
    } catch (...) {
        removeDirectory(directory_);
        throw;
    }
}

GeneratedInvalidMedia::~GeneratedInvalidMedia() {
    removeDirectory(directory_);
}

std::string GeneratedInvalidMedia::source() const {
    return pathToUtf8(path_);
}

}  // namespace mediahub::test
