#include "mediahub/logging/logger.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <streambuf>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace mediahub::logging {
namespace {

TEST(LoggerTest, WritesOneStructuredUtf8LineAndEscapesControlCharacters) {
    std::ostringstream output;
    Logger logger(output);

    logger.log(LogLevel::Warning,
               "player",
               "media_error",
               {{"source", "C:/Users/adminstrator/测试 \"视频\".mp4"},
                {"detail", "line1\nline2\\tail"}});

    const std::string line = output.str();
    EXPECT_EQ(line.find('\n'), line.size() - 1);
    EXPECT_NE(line.find("timestamp=\""), std::string::npos);
    EXPECT_NE(line.find("level=\"WARNING\""), std::string::npos);
    EXPECT_NE(line.find("component=\"player\""), std::string::npos);
    EXPECT_NE(line.find("event=\"media_error\""), std::string::npos);
    EXPECT_NE(line.find("测试 \\\"视频\\\".mp4"), std::string::npos);
    EXPECT_EQ(line.find("C:/Users/adminstrator"), std::string::npos);
    EXPECT_NE(line.find("line1\\nline2\\\\tail"), std::string::npos);
}

TEST(LoggerTest, SwallowsOutputFailuresSoDiagnosticsCannotBreakPlayback) {
    class ThrowingBuffer final : public std::streambuf {
    protected:
        std::streamsize xsputn(const char*, std::streamsize) override {
            throw std::runtime_error("simulated terminal failure");
        }

        int_type overflow(const int_type) override {
            throw std::runtime_error("simulated terminal failure");
        }
    } buffer;
    std::ostream output(&buffer);
    Logger logger(output);

    EXPECT_NO_THROW(logger.log(LogLevel::Error, "application", "failure"));
}

TEST(LoggerTest, SerializesConcurrentWritersWithoutMergingLines) {
    std::ostringstream output;
    Logger logger(output);
    std::vector<std::thread> writers;
    constexpr int kWriterCount = 4;
    constexpr int kLinesPerWriter = 25;
    for (int writer = 0; writer < kWriterCount; ++writer) {
        writers.emplace_back([writer, &logger] {
            for (int line = 0; line < kLinesPerWriter; ++line) {
                logger.log(LogLevel::Info,
                           "concurrency_test",
                           "line",
                           {{"writer", std::to_string(writer)},
                            {"line", std::to_string(line)}});
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    const std::string logs = output.str();
    EXPECT_EQ(static_cast<int>(std::count(logs.begin(), logs.end(), '\n')),
              kWriterCount * kLinesPerWriter);
}

}  // namespace
}  // namespace mediahub::logging
