#pragma once

// 游戏窗口定位与相关查询。只按进程名/窗口类名匹配，不注入、不读内存。

#include <windows.h>

#include <string>

namespace bys {

class WindowFinder {
public:
    // 查找原神主窗口；未找到返回 nullptr
    static HWND FindGameWindow();

    // 取客户区（实际渲染区域）在屏幕上的矩形
    static bool GetClientRectOnScreen(HWND hwnd, RECT& out);

    // 游戏是否为当前前台。按进程 ID 比较，避免多顶层窗口误判
    static bool IsForeground(HWND hwnd);

    static std::wstring GetTitle(HWND hwnd);
    static std::wstring GetClassNameOf(HWND hwnd);
    static DWORD GetProcessIdOf(HWND hwnd);

    // 查询指定进程是否以管理员权限运行。查询失败返回 false
    static bool TryGetProcessElevation(DWORD pid, bool& elevated);

    // 本进程是否以管理员权限运行
    static bool IsCurrentProcessElevated();
};

}  // namespace bys
