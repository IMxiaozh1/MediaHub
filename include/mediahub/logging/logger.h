#pragma once

#include <initializer_list>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace mediahub::logging {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

// 表示结构化日志中的一个键值字段；值会在输出前转义为单行文本。
struct LogField {
    std::string key;
    std::string value;
};

// 向调用方提供的终端流写入单行 UTF-8 结构化日志，不创建或管理日志文件。
class Logger final {
public:
    explicit Logger(std::ostream& output) noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 调用线程：任意线程。内部串行写入；终端不可用时静默放弃，不能影响播放流程。
    void log(LogLevel level,
             std::string_view component,
             std::string_view event,
             std::initializer_list<LogField> fields = {}) noexcept;

private:
    std::ostream& output_;
    std::mutex mutex_;
};

}  // namespace mediahub::logging
