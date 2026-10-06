#pragma once

// 线程安全的文件日志。输出到 <exe目录>\log\app_yyyyMMdd.log（UTF-8 编码）。
// 界面不再展示日志，需要排查时直接看日志文件。

#include <string>

namespace bys {

enum class LogLevel { Debug, Info, Warn, Error };

class Log {
public:
    static Log& Instance();

    // 日志目录（首次调用时创建）
    const std::wstring& Directory() const { return dir_; }

    void Debug(const std::wstring& message);
    void Info(const std::wstring& message);
    void Warn(const std::wstring& message);
    void Error(const std::wstring& message);

    void Write(LogLevel level, const std::wstring& message);

private:
    Log();

    std::wstring dir_;
    void* mutex_;        // CRITICAL_SECTION*
    bool fileWriteFailed_ = false;
};

// 便捷宏
#define LOG_DEBUG(msg) ::bys::Log::Instance().Debug(msg)
#define LOG_INFO(msg)  ::bys::Log::Instance().Info(msg)
#define LOG_WARN(msg)  ::bys::Log::Instance().Warn(msg)
#define LOG_ERROR(msg) ::bys::Log::Instance().Error(msg)

}  // namespace bys
