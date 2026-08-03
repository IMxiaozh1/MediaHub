#include "mediahub/logging/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace mediahub::logging {
namespace {

std::string levelName(const LogLevel level) {
    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

std::string utcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto wholeSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - wholeSeconds).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
           << std::setw(3) << milliseconds << 'Z';
    return output.str();
}

std::string escapedValue(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

std::string safeFieldValue(const std::string_view key, const std::string_view value) {
    const bool explicitPathField = key.find("path") != std::string_view::npos ||
                                   key.find("source") != std::string_view::npos ||
                                   key.find("media") != std::string_view::npos ||
                                   key.find("file") != std::string_view::npos;
    const bool hasDrivePath = value.find(":/") != std::string_view::npos ||
                              value.find(":\\") != std::string_view::npos;
    const bool hasAbsolutePath = !value.empty() &&
                                 (value.front() == '/' || value.front() == '\\');
    if ((!explicitPathField && !hasDrivePath && !hasAbsolutePath) ||
        value.find_first_of("/\\") == std::string_view::npos) {
        return std::string(value);
    }
    const auto separator = value.find_last_of("/\\");
    return std::string(value.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

void appendField(std::ostream& output,
                 const std::string_view key,
                 const std::string_view value) {
    output << ' ' << key << "=\"" << escapedValue(value) << '"';
}

}  // namespace

Logger::Logger(std::ostream& output) noexcept : output_(output) {}

void Logger::log(const LogLevel level,
                 const std::string_view component,
                 const std::string_view event,
                 const std::initializer_list<LogField> fields) noexcept {
    try {
        std::ostringstream line;
        appendField(line, "timestamp", utcTimestamp());
        appendField(line, "level", levelName(level));
        appendField(line, "component", component);
        appendField(line, "event", event);
        for (const auto& field : fields) {
            appendField(line, field.key, safeFieldValue(field.key, field.value));
        }

        const std::lock_guard lock(mutex_);
        output_ << line.str() << '\n';
        output_.flush();
    } catch (...) {
        // 日志是诊断旁路，任何格式化或终端写入失败都不能中断播放器。
    }
}

}  // namespace mediahub::logging
