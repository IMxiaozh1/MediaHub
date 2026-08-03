#include "support/generated_media.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace mediahub::test {
namespace {

using namespace std::chrono_literals;

std::filesystem::path pathFromUtf8(const std::string& source) {
    const auto* const begin = reinterpret_cast<const char8_t*>(source.data());
    return std::filesystem::path(std::u8string(begin, begin + source.size()));
}

TEST(GeneratedMediaTest, CreatesValidUtf8WavAndRemovesItOnDestruction) {
    std::filesystem::path generatedPath;
    {
        const GeneratedWav media(125ms);
        generatedPath = pathFromUtf8(media.source());
        ASSERT_TRUE(std::filesystem::is_regular_file(generatedPath));
        EXPECT_EQ(generatedPath.filename(), std::filesystem::path(L"测试 音频.wav"));

        std::ifstream input(generatedPath, std::ios::binary);
        ASSERT_TRUE(input.is_open());
        std::array<char, 12> header{};
        input.read(header.data(), static_cast<std::streamsize>(header.size()));
        ASSERT_EQ(input.gcount(), static_cast<std::streamsize>(header.size()));
        EXPECT_EQ(std::string(header.data(), 4), "RIFF");
        EXPECT_EQ(std::string(header.data() + 8, 4), "WAVE");
    }
    EXPECT_FALSE(std::filesystem::exists(generatedPath));
}

TEST(GeneratedMediaTest, PreservesInvalidPayloadAndRemovesItOnDestruction) {
    std::filesystem::path generatedPath;
    {
        const GeneratedInvalidMedia media(L"伪装 视频.mp4", "not a media file");
        generatedPath = pathFromUtf8(media.source());
        std::ifstream input(generatedPath, std::ios::binary);
        ASSERT_TRUE(input.is_open());
        const std::string contents{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
        EXPECT_EQ(contents, "not a media file");
    }
    EXPECT_FALSE(std::filesystem::exists(generatedPath));
}

}  // namespace
}  // namespace mediahub::test
