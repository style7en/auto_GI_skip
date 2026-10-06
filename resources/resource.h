#pragma once

// 控件与资源 ID

#define IDI_APPICON                 101

// 嵌入到 exe 里的识别模板（RCDATA）。
// 运行时会优先读 <exe目录>\assets\1920x1080\ 下的同名文件，
// 找不到才回退到这里的嵌入资源 —— 这样既开箱即用，又允许用户替换模板而不必重新编译。
#define IDR_TEMPLATE_DISABLED_UI    201
#define IDR_TEMPLATE_STOP_AUTO      202
#define IDR_TEMPLATE_OPTION_ICON    203
#define IDR_TEMPLATE_PAGE_CLOSE     204
#define IDR_TEMPLATE_HANGOUT_SKIP   205

// 主窗口控件
#define IDC_BTN_START_STOP          1001
#define IDC_STATIC_STATUS           1002
#define IDC_BTN_SAVE_FRAME          1003
#define IDC_BTN_SAVE_REPORT         1004
#define IDC_BTN_OPEN_LOG            1005

#define IDC_CHK_QUICK_SKIP          1010
#define IDC_CHK_INTERACT_KEY        1011
#define IDC_CHK_PREFER_ORANGE       1012
#define IDC_CHK_BLACK_SCREEN        1013
#define IDC_CHK_CLOSE_POPUP         1014

#define IDC_CMB_OPTION_STRATEGY     1020
#define IDC_CMB_CAPTURE_BACKEND     1021

// 状态显示字段
#define IDC_VAL_STATUS              1030
#define IDC_VAL_WINDOW              1031
#define IDC_VAL_BACKEND             1032
#define IDC_VAL_FPS                 1033
#define IDC_VAL_FRAME_SIZE          1034
#define IDC_VAL_BRIGHTNESS          1035
#define IDC_VAL_TALK                1036
#define IDC_VAL_BUBBLE              1037
#define IDC_VAL_TRIANGLE            1038
#define IDC_VAL_ELEVATION           1039
#define IDC_VAL_LOGPATH             1040

// 分组框
#define IDC_GROUP_STATUS            1050
#define IDC_GROUP_SETTINGS          1051

// 自定义消息：引擎 -> UI 线程
#define WM_APP_ENGINE_STATE         (WM_APP + 1)
#define WM_APP_HOTKEY_TOGGLE        (WM_APP + 2)
