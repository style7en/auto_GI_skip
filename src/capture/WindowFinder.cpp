#include "capture/WindowFinder.h"

#include <tlhelp32.h>

#include <algorithm>
#include <vector>

#include "util/StringUtil.h"

namespace bys {

namespace {

const wchar_t* kUnityWindowClass = L"UnityWndClass";
const wchar_t* kGameProcessNames[] = {L"YuanShen.exe", L"GenshinImpact.exe"};

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) {
        return false;
    }
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

// 取进程可执行文件名（不含路径）
std::wstring GetProcessImageName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (int)pid);
    if (process == nullptr) {
        return {};
    }

    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        std::wstring full(buffer, size);
        size_t slash = full.find_last_of(L"\\/");
        result = slash == std::wstring::npos ? full : full.substr(slash + 1);
    }
    CloseHandle(process);
    return result;
}

bool IsGameProcessName(const std::wstring& name) {
    for (const wchar_t* candidate : kGameProcessNames) {
        if (EqualsIgnoreCase(name, candidate)) {
            return true;
        }
    }
    return false;
}

struct EnumContext {
    HWND bestUnity = nullptr;
    long long bestUnityArea = 0;
    HWND bestNamed = nullptr;
    long long bestNamedArea = 0;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return TRUE;
    }
    long long area = (long long)(client.right - client.left) * (client.bottom - client.top);
    if (area <= 0) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return TRUE;
    }

    // 优先：进程名匹配
    std::wstring exeName = GetProcessImageName(pid);
    if (IsGameProcessName(exeName) && area > ctx->bestNamedArea) {
        ctx->bestNamedArea = area;
        ctx->bestNamed = hwnd;
    }

    // 兜底：Unity 渲染窗口类名
    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, 256) > 0
        && EqualsIgnoreCase(className, kUnityWindowClass)
        && area > ctx->bestUnityArea) {
        ctx->bestUnityArea = area;
        ctx->bestUnity = hwnd;
    }

    return TRUE;
}

}  // namespace

HWND WindowFinder::FindGameWindow() {
    EnumContext ctx;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.bestNamed != nullptr ? ctx.bestNamed : ctx.bestUnity;
}

bool WindowFinder::GetClientRectOnScreen(HWND hwnd, RECT& out) {
    if (hwnd == nullptr) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }

    POINT origin{0, 0};
    if (!ClientToScreen(hwnd, &origin)) {
        return false;
    }

    out.left = origin.x;
    out.top = origin.y;
    out.right = origin.x + (client.right - client.left);
    out.bottom = origin.y + (client.bottom - client.top);
    return (out.right - out.left) > 0 && (out.bottom - out.top) > 0;
}

bool WindowFinder::IsForeground(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    if (foreground == hwnd) {
        return true;
    }

    DWORD fgPid = 0;
    DWORD gamePid = 0;
    GetWindowThreadProcessId(foreground, &fgPid);
    GetWindowThreadProcessId(hwnd, &gamePid);
    return fgPid != 0 && fgPid == gamePid;
}

std::wstring WindowFinder::GetTitle(HWND hwnd) {
    if (hwnd == nullptr) {
        return {};
    }
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }
    std::wstring title((size_t)length + 1, L'\0');
    GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize((size_t)length);
    return title;
}

std::wstring WindowFinder::GetClassNameOf(HWND hwnd) {
    if (hwnd == nullptr) {
        return {};
    }
    wchar_t buffer[256] = {};
    int n = GetClassNameW(hwnd, buffer, 256);
    return n > 0 ? std::wstring(buffer, (size_t)n) : std::wstring();
}

DWORD WindowFinder::GetProcessIdOf(HWND hwnd) {
    DWORD pid = 0;
    if (hwnd != nullptr) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
    return pid;
}

bool WindowFinder::TryGetProcessElevation(DWORD pid, bool& elevated) {
    elevated = false;
    if (pid == 0) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (int)pid);
    if (process == nullptr) {
        return false;
    }

    bool ok = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION info{};
        DWORD returned = 0;
        if (GetTokenInformation(token, TokenElevation, &info, sizeof(info), &returned)) {
            elevated = info.TokenIsElevated != 0;
            ok = true;
        }
        CloseHandle(token);
    }
    CloseHandle(process);
    return ok;
}

bool WindowFinder::IsCurrentProcessElevated() {
    bool elevated = false;
    return TryGetProcessElevation(GetCurrentProcessId(), elevated) && elevated;
}

}  // namespace bys
