#include "util/StringUtil.h"

#include <windows.h>

#include <cstdarg>
#include <cwchar>
#include <vector>

namespace bys {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int need = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), need);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    int need = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring Format(const wchar_t* fmt, ...) {
    // 先试 512 字符，不够再按需扩容
    std::vector<wchar_t> buffer(512);
    for (;;) {
        va_list args;
        va_start(args, fmt);
        int written = _vsnwprintf_s(buffer.data(), buffer.size(), _TRUNCATE, fmt, args);
        va_end(args);

        if (written >= 0) {
            return std::wstring(buffer.data(), (size_t)written);
        }

        if (buffer.size() > 64 * 1024) {
            return L"<Format overflow>";
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
        if (len == 0) {
            return L".";
        }
        if (len < buffer.size() - 1) {
            std::wstring path(buffer.data(), len);
            size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : path.substr(0, slash);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) {
        return name;
    }
    wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') {
        return dir + name;
    }
    return dir + L"\\" + name;
}

std::wstring TimestampForFileName() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return Format(L"%04d%02d%02d_%02d%02d%02d",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

}  // namespace bys
