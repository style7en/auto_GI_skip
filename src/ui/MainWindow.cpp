#include "ui/MainWindow.h"

#include <shellapi.h>

#include <algorithm>

#include "capture/WindowFinder.h"
#include "resources/resource.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace bys {

namespace {

const wchar_t* kWindowClassName = L"BetterYuanshenMainWindow";
const wchar_t* kWindowTitle = L"BetterYuanshen · 自动剧情";

// 窗口客户区尺寸（逻辑像素，96 DPI 基准）
// 内容底部（日志行按钮）在 592，加 20 留白 => 612
constexpr int kClientWidth = 820;
constexpr int kClientHeight = 612;

// 四边留白统一 20 逻辑像素，保证下方与右方留白一致
constexpr int kMargin = 20;

// 窗口样式：无 WS_THICKFRAME => 不可缩放
constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD kWindowExStyle = 0;

constexpr UINT_PTR kTimerRefresh = 1;
constexpr UINT kRefreshIntervalMs = 250;

// 配色
constexpr COLORREF kWindowBackground = RGB(244, 245, 247);
constexpr COLORREF kCardBackground = RGB(255, 255, 255);
constexpr COLORREF kCardBorder = RGB(228, 230, 235);
constexpr COLORREF kTextPrimary = RGB(31, 35, 41);
constexpr COLORREF kTextSecondary = RGB(138, 144, 153);
constexpr COLORREF kAccentBlue = RGB(59, 130, 246);
constexpr COLORREF kAccentTeal = RGB(20, 184, 166);
constexpr COLORREF kButtonGreen = RGB(31, 164, 99);
constexpr COLORREF kButtonRed = RGB(229, 72, 77);

struct RowDef {
    const wchar_t* label1;
    const wchar_t* label2;
};

const RowDef kStatusRowDefs[5] = {
    {L"状态", L"游戏窗口"},
    {L"截图后端", L"帧率"},
    {L"画面尺寸", L"画面亮度"},
    {L"对话识别", L"选项气泡"},
    {L"底部三角", L"权限"},
};

HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text,
                 DWORD style, DWORD exStyle, int id) {
    return CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

void DrawCard(HDC hdc, const RECT& rc, int radius, int borderWidth) {
    HBRUSH brush = CreateSolidBrush(kCardBackground);
    HPEN pen = CreatePen(PS_SOLID, borderWidth, kCardBorder);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

}  // namespace

MainWindow::~MainWindow() {
    DestroyFonts();
    if (windowBrush_) DeleteObject(windowBrush_);
    if (cardBrush_) DeleteObject(cardBrush_);
}

bool MainWindow::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // 背景完全自绘，避免闪烁
    wc.lpszClassName = kWindowClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;

    if (RegisterClassExW(&wc) == 0) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    return true;
}

bool MainWindow::Create(HINSTANCE instance, bool offscreen, UINT forcedDpi) {
    instance_ = instance;
    forcedDpi_ = forcedDpi;

    // 离屏渲染用无边框窗口：客户区尺寸 == 窗口尺寸，
    // 避免非客户区按显示器 DPI 渲染、而我们按 forcedDpi 计算造成的偏差
    style_ = offscreen ? WS_POPUP : kWindowStyle;
    exStyle_ = offscreen ? 0 : kWindowExStyle;

    // 先按 96 DPI 估算一个尺寸，OnCreate 里会按实际 DPI 修正
    RECT rect{0, 0, kClientWidth, kClientHeight};
    AdjustWindowRectEx(&rect, style_, FALSE, exStyle_);

    const int x = offscreen ? -30000 : CW_USEDEFAULT;
    const int y = offscreen ? -30000 : CW_USEDEFAULT;

    hwnd_ = CreateWindowExW(
        exStyle_, kWindowClassName, kWindowTitle, style_,
        x, y, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, this);

    if (hwnd_ == nullptr) {
        return false;
    }

    ShowWindow(hwnd_, offscreen ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void MainWindow::PumpFor(int milliseconds) {
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)milliseconds;
    MSG msg{};
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

int MainWindow::Dp(int value) const {
    return MulDiv(value, (int)dpi_, 96);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            OnCreate();
            return 0;

        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam));
            return 0;

        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam));
            return 0;

        case WM_TIMER:
            if (wParam == kTimerRefresh) {
                RefreshState();
            }
            return 0;

        case WM_APP_HOTKEY_TOGGLE:
            ToggleRunning();
            return 0;

        case WM_ERASEBKGND:
            return 1;  // 背景在 WM_PAINT 里一次画完

        case WM_PAINT:
            OnPaint();
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(hdc, TRANSPARENT);
            // 落在卡片上的控件用卡片底色，否则用窗口底色，避免出现灰色色块
            bool inCard = IsInsideRect(control, statusCardRect_) ||
                          IsInsideRect(control, settingsCardRect_) ||
                          IsInsideRect(control, hotkeyCardRect_);
            return (LRESULT)(inCard ? cardBrush_ : windowBrush_);
        }

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis->CtlID == IDC_BTN_START_STOP) {
                // 先用窗口底色填满，保证圆角外的区域干净
                FillRect(dis->hDC, &dis->rcItem, windowBrush_);

                bool running = engine_.IsRunning();
                HBRUSH brush = CreateSolidBrush(running ? kButtonRed : kButtonGreen);
                HPEN pen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
                HGDIOBJ oldBrush = SelectObject(dis->hDC, brush);
                HGDIOBJ oldPen = SelectObject(dis->hDC, pen);

                int radius = Dp(10);
                RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                          dis->rcItem.right, dis->rcItem.bottom, radius, radius);

                SelectObject(dis->hDC, oldPen);
                SelectObject(dis->hDC, oldBrush);
                DeleteObject(pen);
                DeleteObject(brush);

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, RGB(255, 255, 255));
                HFONT oldFont = (HFONT)SelectObject(dis->hDC, fontButton_);

                std::wstring text = running ? L"停 止 自 动 剧 情" : L"启 动 自 动 剧 情";
                RECT rc = dis->rcItem;
                DrawTextW(dis->hDC, text.c_str(), -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(dis->hDC, oldFont);
                return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            OnDestroy();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// DPI 与布局
// ---------------------------------------------------------------------------

UINT MainWindow::QueryWindowDpi() const {
    if (hwnd_ != nullptr) {
        UINT dpi = GetDpiForWindow(hwnd_);
        if (dpi >= 72 && dpi <= 480) {
            return dpi;
        }
    }
    // 回退：主显示器 DPI
    if (HDC screenDc = GetDC(nullptr)) {
        int dpi = GetDeviceCaps(screenDc, LOGPIXELSX);
        ReleaseDC(nullptr, screenDc);
        if (dpi >= 72 && dpi <= 480) {
            return (UINT)dpi;
        }
    }
    return 96;
}

void MainWindow::ResizeClientToLogical() {
    if (hwnd_ == nullptr) {
        return;
    }

    // 必须用 DPI 感知的 AdjustWindowRectExForDpi 计算非客户区。
    // 用 AdjustWindowRectEx 会按 96 DPI 估算标题栏/边框厚度，
    // 高 DPI 下实际客户区就会比预期小或大，表现为右下方留白不一致。
    RECT rect{0, 0, Dp(kClientWidth), Dp(kClientHeight)};
    AdjustWindowRectExForDpi(&rect, style_, FALSE, exStyle_, dpi_);

    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::OnDpiChanged(UINT newDpi, const RECT* suggested) {
    if (newDpi >= 72 && newDpi <= 480) {
        dpi_ = newDpi;
    }

    LOG_INFO(Format(L"DPI 变化：新 DPI=%u（缩放 %d%%）", dpi_, (int)(dpi_ * 100 / 96)));

    // 位置用系统建议值，尺寸按我们的逻辑尺寸重新算，
    // 保证客户区精确等于 kClientWidth x kClientHeight 逻辑像素
    RECT target{0, 0, Dp(kClientWidth), Dp(kClientHeight)};
    AdjustWindowRectExForDpi(&target, style_, FALSE, exStyle_, dpi_);
    const int width = target.right - target.left;
    const int height = target.bottom - target.top;

    const int x = suggested != nullptr ? suggested->left : 0;
    const int y = suggested != nullptr ? suggested->top : 0;
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

    // 字体按新 DPI 重建，再重新布局
    DestroyFonts();
    CreateFonts();
    ApplyFontsToControls();
    Relayout();

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::DestroyFonts() {
    auto release = [](HFONT& font) {
        if (font != nullptr) {
            DeleteObject(font);
            font = nullptr;
        }
    };
    release(fontNormal_);
    release(fontTitle_);
    release(fontSection_);
    release(fontButton_);
    release(fontHotkey_);
}

void MainWindow::CreateFonts() {
    auto make = [&](int pointSize, int weight) {
        return CreateFontW(-MulDiv(pointSize, (int)dpi_, 72), 0, 0, 0, weight, FALSE, FALSE,
                           FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    };

    fontNormal_ = make(9, FW_NORMAL);
    fontTitle_ = make(19, FW_BOLD);
    fontSection_ = make(11, FW_SEMIBOLD);
    fontButton_ = make(15, FW_SEMIBOLD);
    fontHotkey_ = make(17, FW_BOLD);
}

void MainWindow::OnCreate() {
    dpi_ = forcedDpi_ > 0 ? forcedDpi_ : QueryWindowDpi();

    windowBrush_ = CreateSolidBrush(kWindowBackground);
    cardBrush_ = CreateSolidBrush(kCardBackground);

    ResizeClientToLogical();

    CreateFonts();
    CreateControls();
    ApplyFontsToControls();
    Relayout();
    SyncConfigToUi();

    hotkey_.Install(hwnd_, WM_APP_HOTKEY_TOGGLE);

    SetTimer(hwnd_, kTimerRefresh, kRefreshIntervalMs, nullptr);
    RefreshState();
}

void MainWindow::CreateControls() {
    // 只创建控件；位置统一由 Relayout() 设置（便于 DPI 变化时重算）
    titleLabel_ = CreateChild(hwnd_, L"STATIC", L"自动剧情", SS_LEFT, 0, 0);
    subtitleLabel_ = CreateChild(hwnd_, L"STATIC",
                                 L"自动推进对话、自动选择选项（键盘操作，不移动鼠标）",
                                 SS_LEFT, 0, 0);
    rightHintLabel_ = CreateChild(hwnd_, L"STATIC", L"仅前台运行 · 需管理员权限",
                                  SS_RIGHT, 0, 0);

    startStopButton_ = CreateChild(hwnd_, L"BUTTON", L"", BS_OWNERDRAW, 0, IDC_BTN_START_STOP);
    hotkeyLabel1_ = CreateChild(hwnd_, L"STATIC", L"快捷键", SS_CENTER, 0, 0);
    hotkeyLabel2_ = CreateChild(hwnd_, L"STATIC", L"F12", SS_CENTER, 0, 0);

    btnSaveFrame_ = CreateChild(hwnd_, L"BUTTON", L"保存调试截图", BS_PUSHBUTTON, 0,
                                IDC_BTN_SAVE_FRAME);
    btnSaveReport_ = CreateChild(hwnd_, L"BUTTON", L"保存诊断报告", BS_PUSHBUTTON, 0,
                                 IDC_BTN_SAVE_REPORT);

    for (int i = 0; i < 5; ++i) {
        statusRows_[i].label1 = CreateChild(hwnd_, L"STATIC", kStatusRowDefs[i].label1, SS_LEFT, 0, 0);
        statusRows_[i].value1 = CreateChild(hwnd_, L"STATIC", L"-", SS_LEFT | SS_ENDELLIPSIS, 0, 0);
        statusRows_[i].label2 = CreateChild(hwnd_, L"STATIC", kStatusRowDefs[i].label2, SS_LEFT, 0, 0);
        statusRows_[i].value2 = CreateChild(hwnd_, L"STATIC", L"-", SS_LEFT | SS_ENDELLIPSIS, 0, 0);
    }

    chkQuickSkip_ = CreateChild(hwnd_, L"BUTTON", L"自动推进对话（按空格）",
                                BS_AUTOCHECKBOX, 0, IDC_CHK_QUICK_SKIP);
    chkInteractKey_ = CreateChild(hwnd_, L"BUTTON", L"推进对话改用交互键（F）",
                                  BS_AUTOCHECKBOX, 0, IDC_CHK_INTERACT_KEY);
    chkPreferOrange_ = CreateChild(hwnd_, L"BUTTON", L"优先选择橙色选项（含奖励）",
                                   BS_AUTOCHECKBOX, 0, IDC_CHK_PREFER_ORANGE);
    // 这两项会移动鼠标，默认不勾选（见 AutoSkipConfig）
    chkBlackScreen_ = CreateChild(hwnd_, L"BUTTON", L"黑屏剧情自动点击（会移动鼠标）",
                                  BS_AUTOCHECKBOX, 0, IDC_CHK_BLACK_SCREEN);
    chkClosePopup_ = CreateChild(hwnd_, L"BUTTON", L"自动关闭弹出页面（会移动鼠标）",
                                 BS_AUTOCHECKBOX, 0, IDC_CHK_CLOSE_POPUP);

    labelStrategy_ = CreateChild(hwnd_, L"STATIC", L"选项兜底策略", SS_LEFT, 0, 0);
    comboStrategy_ = CreateChild(hwnd_, L"COMBOBOX", L"",
                                 CBS_DROPDOWNLIST | WS_VSCROLL, 0, IDC_CMB_OPTION_STRATEGY);
    labelBackend_ = CreateChild(hwnd_, L"STATIC", L"截图后端", SS_LEFT, 0, 0);
    comboBackend_ = CreateChild(hwnd_, L"COMBOBOX", L"",
                                CBS_DROPDOWNLIST | WS_VSCROLL, 0, IDC_CMB_CAPTURE_BACKEND);

    logPathLabel_ = CreateChild(hwnd_, L"STATIC", L"日志目录", SS_LEFT, 0, 0);
    logPathValue_ = CreateChild(hwnd_, L"STATIC", L"-", SS_LEFT | SS_ENDELLIPSIS, 0, IDC_VAL_LOGPATH);
    btnOpenLog_ = CreateChild(hwnd_, L"BUTTON", L"打开日志目录", BS_PUSHBUTTON, 0, IDC_BTN_OPEN_LOG);
}

void MainWindow::ApplyFontsToControls() {
    HWND normalFontControls[] = {
        subtitleLabel_, rightHintLabel_, hotkeyLabel1_,
        btnSaveFrame_, btnSaveReport_, chkQuickSkip_, chkInteractKey_, chkPreferOrange_,
        chkBlackScreen_, chkClosePopup_, labelStrategy_, comboStrategy_,
        labelBackend_, comboBackend_, logPathLabel_, logPathValue_, btnOpenLog_,
    };
    for (HWND h : normalFontControls) {
        if (h != nullptr) {
            SendMessageW(h, WM_SETFONT, (WPARAM)fontNormal_, TRUE);
        }
    }

    for (auto& row : statusRows_) {
        SendMessageW(row.label1, WM_SETFONT, (WPARAM)fontNormal_, TRUE);
        SendMessageW(row.value1, WM_SETFONT, (WPARAM)fontNormal_, TRUE);
        SendMessageW(row.label2, WM_SETFONT, (WPARAM)fontNormal_, TRUE);
        SendMessageW(row.value2, WM_SETFONT, (WPARAM)fontNormal_, TRUE);
    }

    SendMessageW(titleLabel_, WM_SETFONT, (WPARAM)fontTitle_, TRUE);
    SendMessageW(hotkeyLabel2_, WM_SETFONT, (WPARAM)fontHotkey_, TRUE);
}

void MainWindow::Relayout() {
    const int margin = Dp(kMargin);
    const int contentW = Dp(kClientWidth) - margin * 2;

    auto place = [](HWND h, int x, int y, int w, int hh) {
        if (h != nullptr) {
            SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER);
        }
    };

    // ---------- 标题 ----------
    place(titleLabel_, margin, Dp(14), Dp(300), Dp(32));
    place(subtitleLabel_, margin, Dp(46), Dp(500), Dp(20));
    place(rightHintLabel_, margin + contentW - Dp(260), Dp(46), Dp(260), Dp(20));

    // ---------- 启动 / 停止按钮 + F12 卡片 ----------
    const int hotkeyCardW = Dp(200);
    const int buttonW = contentW - hotkeyCardW - Dp(12);
    place(startStopButton_, margin, Dp(78), buttonW, Dp(58));

    hotkeyCardRect_ = {margin + buttonW + Dp(12), Dp(78),
                       margin + contentW, Dp(78) + Dp(58)};
    place(hotkeyLabel1_, hotkeyCardRect_.left, Dp(88), hotkeyCardW, Dp(18));
    place(hotkeyLabel2_, hotkeyCardRect_.left, Dp(107), hotkeyCardW, Dp(26));

    // ---------- 运行状态卡片 ----------
    statusCardRect_ = {margin, Dp(150), margin + contentW, Dp(362)};

    place(btnSaveFrame_, statusCardRect_.right - Dp(266), Dp(162), Dp(122), Dp(30));
    place(btnSaveReport_, statusCardRect_.right - Dp(136), Dp(162), Dp(122), Dp(30));

    const int rowTop = Dp(200);
    const int rowHeight = Dp(30);
    for (int i = 0; i < 5; ++i) {
        int y = rowTop + i * rowHeight;
        place(statusRows_[i].label1, margin + Dp(18), y, Dp(70), Dp(20));
        place(statusRows_[i].value1, margin + Dp(92), y, Dp(250), Dp(20));
        place(statusRows_[i].label2, margin + Dp(364), y, Dp(76), Dp(20));
        place(statusRows_[i].value2, margin + Dp(444), y, Dp(300), Dp(20));
    }

    // ---------- 行为设置卡片 ----------
    settingsCardRect_ = {margin, Dp(374), margin + contentW, Dp(554)};

    place(chkQuickSkip_, margin + Dp(18), Dp(414), Dp(260), Dp(22));
    place(chkInteractKey_, margin + Dp(18), Dp(440), Dp(260), Dp(22));
    place(chkPreferOrange_, margin + Dp(18), Dp(466), Dp(260), Dp(22));
    place(chkBlackScreen_, margin + Dp(410), Dp(414), Dp(280), Dp(22));
    place(chkClosePopup_, margin + Dp(410), Dp(440), Dp(280), Dp(22));

    place(labelStrategy_, margin + Dp(18), Dp(505), Dp(90), Dp(22));
    place(comboStrategy_, margin + Dp(112), Dp(500), Dp(230), Dp(240));
    place(labelBackend_, margin + Dp(410), Dp(505), Dp(70), Dp(22));
    place(comboBackend_, margin + Dp(484), Dp(500), Dp(230), Dp(240));

    // ---------- 日志目录 ----------
    place(logPathLabel_, margin, Dp(566), Dp(70), Dp(22));
    place(logPathValue_, margin + Dp(76), Dp(566), contentW - Dp(206), Dp(22));
    place(btnOpenLog_, margin + contentW - Dp(122), Dp(562), Dp(122), Dp(30));
}

void MainWindow::SyncConfigToUi() {
    suppressCommand_ = true;

    const AutoSkipConfig& cfg = engine_.Config();

    SendMessageW(comboStrategy_, CB_ADDSTRING, 0, (LPARAM)L"优先选择最后一个选项");
    SendMessageW(comboStrategy_, CB_ADDSTRING, 0, (LPARAM)L"优先选择第一个选项");
    SendMessageW(comboStrategy_, CB_ADDSTRING, 0, (LPARAM)L"随机选择选项");
    SendMessageW(comboStrategy_, CB_SETCURSEL, 0, 0);

    SendMessageW(comboBackend_, CB_ADDSTRING, 0, (LPARAM)L"DXGI Desktop Duplication");
    SendMessageW(comboBackend_, CB_ADDSTRING, 0, (LPARAM)L"GDI BitBlt（兜底）");
    SendMessageW(comboBackend_, CB_SETCURSEL, 0, 0);

    auto setCheck = [](HWND h, bool value) {
        SendMessageW(h, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    setCheck(chkQuickSkip_, cfg.quicklySkipConversations);
    setCheck(chkInteractKey_, cfg.useInteractKey);
    setCheck(chkPreferOrange_, cfg.preferOrangeOption);
    setCheck(chkBlackScreen_, cfg.blackScreenClickEnabled);
    setCheck(chkClosePopup_, cfg.autoClosePopup);

    suppressCommand_ = false;
}

bool MainWindow::IsInsideRect(HWND control, const RECT& rect) const {
    if (control == nullptr) {
        return false;
    }

    RECT rc{};
    if (!GetWindowRect(control, &rc)) {
        return false;
    }
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<POINT*>(&rc), 2);

    // 用控件中心点判断，避免边界情况
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    return cx >= rect.left && cx <= rect.right && cy >= rect.top && cy <= rect.bottom;
}

void MainWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);

    // 双缓冲，避免闪烁
    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDc, memBitmap);

    FillRect(memDc, &client, windowBrush_);

    const int radius = Dp(10);
    const int border = std::max(1, Dp(1));

    DrawCard(memDc, hotkeyCardRect_, radius, border);
    DrawCard(memDc, statusCardRect_, radius, border);
    DrawCard(memDc, settingsCardRect_, radius, border);

    // 区块标题：左侧强调竖条 + 标题文字
    auto drawSectionTitle = [&](const RECT& card, const wchar_t* text, COLORREF accent) {
        int barX = card.left + Dp(16);
        int barY = card.top + Dp(14);
        int barW = std::max(2, Dp(3));
        int barH = Dp(15);

        HBRUSH barBrush = CreateSolidBrush(accent);
        RECT barRect{barX, barY, barX + barW, barY + barH};
        FillRect(memDc, &barRect, barBrush);
        DeleteObject(barBrush);

        HFONT oldFont = (HFONT)SelectObject(memDc, fontSection_);
        SetBkMode(memDc, TRANSPARENT);
        SetTextColor(memDc, kTextPrimary);
        RECT textRect{barX + barW + Dp(8), barY - Dp(2), card.right - Dp(16), barY + barH + Dp(2)};
        DrawTextW(memDc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDc, oldFont);
    };

    drawSectionTitle(statusCardRect_, L"运行状态", kAccentBlue);
    drawSectionTitle(settingsCardRect_, L"行为设置", kAccentTeal);

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);

    SelectObject(memDc, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDc);
    EndPaint(hwnd_, &ps);
}

void MainWindow::OnCommand(int controlId, int notifyCode) {
    if (suppressCommand_) {
        return;
    }

    AutoSkipConfig& cfg = engine_.Config();

    switch (controlId) {
        case IDC_BTN_START_STOP:
            ToggleRunning();
            break;

        case IDC_BTN_SAVE_FRAME: {
            std::wstring path = engine_.SaveDebugFrame();
            if (path.empty()) {
                MessageBoxW(hwnd_, L"保存调试截图失败：未找到游戏窗口或截图失败。",
                            L"提示", MB_OK | MB_ICONWARNING);
            } else {
                MessageBoxW(hwnd_, (L"已保存到：\n" + path).c_str(), L"提示",
                            MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case IDC_BTN_SAVE_REPORT: {
            std::wstring dir = Log::Instance().Directory();
            CreateDirectoryW(dir.c_str(), nullptr);
            std::wstring path = JoinPath(dir, L"diagnostic_" + TimestampForFileName() + L".txt");

            std::wstring report = engine_.BuildDiagnosticReport();
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                MessageBoxW(hwnd_, L"写入诊断报告失败。", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }

            DWORD written = 0;
            const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            WriteFile(file, bom, 3, &written, nullptr);
            std::string utf8 = WideToUtf8(report);
            WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
            CloseHandle(file);

            LOG_INFO(L"诊断报告已保存：" + path);
            MessageBoxW(hwnd_, (L"已保存到：\n" + path).c_str(), L"提示",
                        MB_OK | MB_ICONINFORMATION);
            break;
        }

        case IDC_BTN_OPEN_LOG:
            ShellExecuteW(hwnd_, L"open", Log::Instance().Directory().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            break;

        case IDC_CHK_QUICK_SKIP:
            cfg.quicklySkipConversations =
                SendMessageW(chkQuickSkip_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case IDC_CHK_INTERACT_KEY:
            cfg.useInteractKey =
                SendMessageW(chkInteractKey_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case IDC_CHK_PREFER_ORANGE:
            cfg.preferOrangeOption =
                SendMessageW(chkPreferOrange_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case IDC_CHK_BLACK_SCREEN:
            cfg.blackScreenClickEnabled =
                SendMessageW(chkBlackScreen_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case IDC_CHK_CLOSE_POPUP:
            cfg.autoClosePopup =
                SendMessageW(chkClosePopup_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;

        case IDC_CMB_OPTION_STRATEGY:
            if (notifyCode == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(comboStrategy_, CB_GETCURSEL, 0, 0);
                cfg.optionStrategy = sel == 1   ? ChatOptionStrategy::First
                                     : sel == 2 ? ChatOptionStrategy::Random
                                                : ChatOptionStrategy::Last;
            }
            break;

        case IDC_CMB_CAPTURE_BACKEND:
            if (notifyCode == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(comboBackend_, CB_GETCURSEL, 0, 0);
                CaptureBackend backend = sel == 1 ? CaptureBackend::Gdi : CaptureBackend::Dxgi;
                if (cfg.captureBackend != backend) {
                    cfg.captureBackend = backend;
                    bool wasRunning = engine_.IsRunning();
                    if (wasRunning) {
                        engine_.Stop();
                    }
                    engine_.ResetCapture();
                    if (wasRunning) {
                        engine_.Start();
                    }
                }
            }
            break;

        default:
            break;
    }
}

void MainWindow::ToggleRunning() {
    if (engine_.IsRunning()) {
        engine_.Stop();
    } else {
        engine_.Start();
    }
    LOG_INFO(std::wstring(L"自动剧情已") + (engine_.IsRunning() ? L"启动" : L"停止"));
    RefreshState();
}

void MainWindow::RefreshState() {
    EngineState s = engine_.GetState();

    // 按钮只在运行状态真正变化时重绘。
    // 之前每 250ms 无条件 InvalidateRect(..., TRUE)，
    // TRUE 会先擦背景再重绘，视觉上就是按钮一直在闪。
    int buttonState = s.running ? 1 : 0;
    if (buttonState != lastButtonState_) {
        lastButtonState_ = buttonState;
        InvalidateRect(startStopButton_, nullptr, FALSE);
    }

    auto setText = [](HWND h, const std::wstring& text) {
        if (h == nullptr) {
            return;
        }
        // 文本没变就不调用 SetWindowText —— 否则静态控件会整块擦除重画，造成闪烁
        int len = GetWindowTextLengthW(h);
        if (len > 0) {
            std::wstring current((size_t)len + 1, L'\0');
            GetWindowTextW(h, current.data(), len + 1);
            current.resize((size_t)len);
            if (current == text) {
                return;
            }
        } else if (text.empty()) {
            return;
        }
        SetWindowTextW(h, text.c_str());
    };

    setText(statusRows_[0].value1, s.status);
    setText(statusRows_[0].value2, !s.gameWindowFound
                                       ? L"未检测到"
                                       : (s.gameForeground ? L"已找到（前台）" : L"已找到（后台）"));

    setText(statusRows_[1].value1, s.captureBackend);
    setText(statusRows_[1].value2, Format(L"%.1f", s.fps));

    setText(statusRows_[2].value1,
            s.frameWidth > 0 ? Format(L"%dx%d", s.frameWidth, s.frameHeight) : L"-");
    setText(statusRows_[2].value2,
            s.brightness > 0 ? Format(L"%.1f", s.brightness) : L"-");

    setText(statusRows_[3].value1, s.talkUiDetected ? L"已识别到对话" : L"未识别到");
    setText(statusRows_[3].value2, Format(L"%d", s.optionBubbleCount));

    setText(statusRows_[4].value1, s.triangleFound ? L"已识别到" : L"未识别到");
    setText(statusRows_[4].value2, s.elevationInfo);

    setText(logPathValue_, Log::Instance().Directory());
}

void MainWindow::OnDestroy() {
    KillTimer(hwnd_, kTimerRefresh);
    hotkey_.Uninstall();
    engine_.Stop();
    LOG_INFO(L"程序退出");
}

}  // namespace bys
