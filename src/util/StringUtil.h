#pragma once

// 字符串与路径工具。

#include <string>

namespace bys {

// UTF-8 -> UTF-16
std::wstring Utf8ToWide(const std::string& utf8);

// UTF-16 -> UTF-8
std::string WideToUtf8(const std::wstring& wide);

// 宽字符串格式化（类似 StringCchPrintfW，但返回 std::wstring）
std::wstring Format(const wchar_t* fmt, ...);

// 取可执行文件所在目录（结尾不带反斜杠）
std::wstring ExecutableDirectory();

// 拼接路径
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);

// 当前时间的文件名友好格式，如 20261006_134512
std::wstring TimestampForFileName();

}  // namespace bys
