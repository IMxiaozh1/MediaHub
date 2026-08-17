#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace mediahub::test {

// 在系统临时目录生成测试媒体，析构时只清理自己创建的唯一目录。
class GeneratedWav final {
public:
    explicit GeneratedWav(
        std::chrono::milliseconds duration,
        std::wstring fileName = L"测试 音频.wav");
    ~GeneratedWav();

    GeneratedWav(const GeneratedWav&) = delete;
    GeneratedWav& operator=(const GeneratedWav&) = delete;

    [[nodiscard]] std::string source() const;

private:
    void writeFile(std::chrono::milliseconds duration);

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

// 创建可读但不是媒体的文件，用于验证错误分类和失败恢复。
class GeneratedInvalidMedia final {
public:
    GeneratedInvalidMedia(std::wstring fileName, std::string contents);
    ~GeneratedInvalidMedia();

    GeneratedInvalidMedia(const GeneratedInvalidMedia&) = delete;
    GeneratedInvalidMedia& operator=(const GeneratedInvalidMedia&) = delete;

    [[nodiscard]] std::string source() const;

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

}  // namespace mediahub::test
