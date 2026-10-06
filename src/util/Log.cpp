#include "util/Log.h"

#include <windows.h>

#include "util/StringUtil.h"

namespace bys {

namespace {

const wchar_t* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return L"Debug";
        case LogLevel::Info:  return L"Info";
        case LogLevel::Warn:  return L"Warn";
        case LogLevel::Error: return L"Error";
    }
    return L"?";
}

std::wstring NowForLine() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return Format(L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

std::wstring DateForFileName() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return Format(L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
}

}  // namespace

Log& Log::Instance() {
    static Log instance;
    return instance;
}

Log::Log() {
    auto* cs = new CRITICAL_SECTION();
    InitializeCriticalSection(cs);
    mutex_ = cs;

    dir_ = JoinPath(ExecutableDirectory(), L"log");
    CreateDirectoryW(dir_.c_str(), nullptr);
}

void Log::Debug(const std::wstring& message) { Write(LogLevel::Debug, message); }
void Log::Info(const std::wstring& message) { Write(LogLevel::Info, message); }
void Log::Warn(const std::wstring& message) { Write(LogLevel::Warn, message); }
void Log::Error(const std::wstring& message) { Write(LogLevel::Error, message); }

void Log::Write(LogLevel level, const std::wstring& message) {
    if (fileWriteFailed_) {
        return;
    }

    std::wstring line = Format(L"[%s] [%s] %s\r\n",
                               NowForLine().c_str(), LevelName(level), message.c_str());

    // 调试器可见
    OutputDebugStringW(line.c_str());

    std::wstring path = JoinPath(dir_, L"app_" + DateForFileName() + L".log");
    std::string utf8 = WideToUtf8(line);

    auto* cs = static_cast<CRITICAL_SECTION*>(mutex_);
    EnterCriticalSection(cs);

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        fileWriteFailed_ = true;
        LeaveCriticalSection(cs);
        return;
    }

    DWORD written = 0;
    // 写入 BOM 便于记事本识别 UTF-8（仅在文件为空时）
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart == 0) {
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        WriteFile(file, bom, 3, &written, nullptr);
    }
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(file);

    LeaveCriticalSection(cs);
}

}  // namespace bys
