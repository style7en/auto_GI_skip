# BetterYuanshen（C++ / Win32 实现）

原神自动剧情小工具，使用 **Win32 + C++20** 实现。

**零第三方依赖** —— 视觉算法全部自实现，PNG 读写走系统自带的 WIC，编译产物仅 **964 KB**。

| 能力 | 实现方式 |
|------|----------|
| 分发形式 | 单文件 **964 KB**，无运行时依赖 |
| 视觉算法 | 自实现（模板匹配 / HSV / 连通域） |
| 图像编解码 | 系统 WIC（IWICImagingFactory） |

---

## 一、环境要求

| 项 | 要求 |
|----|------|
| 编译器 | MinGW-w64（g++ 13+，需含 `windres`） |
| 构建工具 | GNU Make |
| Windows SDK | 10（MinGW 自带头文件，需含 `d3d11.h` / `dxgi1_2.h` / `wincodec.h`） |
| 系统 | Windows 10 1903 及以上（DXGI Desktop Duplication 要求） |

**不需要 vcpkg，不需要 OpenCV，不需要任何第三方库。**

## 二、构建

用 **Makefile** 构建（GNU Make + MinGW g++）。在工程根目录执行：

```bash
make            # 构建正式版 -> release/BetterYuanshen.exe
make dev        # 构建开发版 -> build/dev/BetterYuanshen.exe
make clean      # 清理全部构建产物
make rebuild    # 清理后重新构建
```

其他便捷目标：

| 命令 | 作用 |
|------|------|
| `make run` | 构建开发版并启动 |
| `make cv-test` | 运行视觉算法自检并打印报告 |
| `make capture-test` | 运行截图管线自检并打印报告 |
| `make ui-preview` | 离屏渲染界面到 `docs/ui-preview.png` |
| `make help` | 列出所有目标 |

### 正式版 vs 开发版

| | 正式版 `make` | 开发版 `make dev` |
|---|---|---|
| 输出 | `release/BetterYuanshen.exe` | `build/dev/BetterYuanshen.exe` |
| 清单 | `app.manifest`（**强制管理员权限**） | `app.dev.manifest`（asInvoker） |
| 用途 | 发布 / 日常使用 | 调试、跑自检 |

正式版嵌入的是 `requireAdministrator` 清单，**非管理员会话无法直接启动**（会被系统拦下），
所以上面的自检类目标（`cv-test` / `capture-test` / `ui-preview` / `run`）**都自动改用开发版**。

### 换编译器

Makefile 里的 `CXX` / `WINDRES` / `LDFLAGS` / `LIBS` 都是变量，可以直接覆盖：

```bash
make CXX=clang++ WINDRES=windres
```

> 代码是**跨编译器**写的（自写了 `ComPtr`，不依赖 MSVC 专有的 `<wrl/client.h>`），
> 理论上 MSVC 也能编译，但需要自行准备对应的 Makefile 或工程文件。

### 依赖

只需要 MinGW-w64（g++ 13+，含 `windres`）和 GNU Make。
**不需要 vcpkg、不需要 OpenCV、不需要任何第三方库。**

## 三、关于 assets 目录

识别模板（`disabled_ui.png` / `stop_auto.png` / `icon_option.png` / `page_close.png` / `hangout_skip.png`）
已经**编译进 exe 内**（作为 RCDATA 资源），所以 **assets 目录是可选的，删掉照样能跑**。

运行时的查找顺序是两级：

| 顺序 | 位置 | 说明 |
|------|------|------|
| 1 | `<exe目录>\assets\1920x1080\<文件名>` | 外部文件优先 |
| 2 | exe 内嵌 RCDATA 资源 | 找不到外部文件时回退 |

这样设计的好处：**开箱即用**（不依赖任何外部文件），同时**允许用户替换模板而不必重新编译**
—— 游戏 UI 改版导致识别失效时，把新的截图裁成模板丢进 `assets\1920x1080\` 覆盖即可。

日志会记录实际来源，便于确认：

```
识别素材已加载（缩放 1.333，外部文件 0 个 / 内嵌资源 5 个）
```

构建时 `assets/` 会自动复制到输出目录。如果你不想要这个目录，删掉即可。

## 四、命令行参数

| 参数 | 用途 |
|------|------|
| （无） | 正常启动图形界面 |
| `--capture-test <目录>` | 无界面截图自检：验证 DXGI / GDI 能否抓到非黑帧，输出各后端亮度 |
| `--cv-test <目录>` | 视觉算法自检：模板匹配位置/分数、橙色判定、底部指示器识别（合成图） |
| `--detect-image <png> [scale]` | 对指定图片跑底部指示器识别，输出报告与带标注的图片 |
| `--render-ui <png> [dpi]` | 把主窗口离屏渲染成 PNG，用于检查界面布局，不打扰当前用户 |
| `--no-elevation-check` | 跳过提权检查（配合开发版使用） |

示例：

```bash
BetterYuanshen.exe --cv-test ./out                    # 视觉算法自检
BetterYuanshen.exe --capture-test ./out               # 截图管线自检
BetterYuanshen.exe --detect-image 游戏截图.png        # 离线验证指示器识别
BetterYuanshen.exe --render-ui ./ui.png 192           # 按 192 DPI 渲染界面布局
```

`--cv-test` 的预期输出：

```
模板尺寸: 28x10  来源=resource
最高匹配分数: 1.0000
定位结果: 命中  位置=(137,89) 期望=(137,89)
位置校验: 通过
分数校验: 通过（合成图应接近 1.0）
橙色判定: 通过
灰色误判为橙色: 通过（正确）
指示器自检[存在] -> 命中  包围盒=(1259,1377) 33x33  金色像素=545
指示器自检[不存在] -> 正确未命中
全部完成
```

## 五、目录结构

```
BetterYuanshenCpp/
├─ Makefile                        构建脚本（make / make dev / make clean ...）
├─ build/                          构建中间产物（根目录只此一个构建目录）
│  ├─ obj/                         正式版 .o / .res
│  └─ dev/                         开发版中间产物 + 开发版 exe
├─ release/                        正式版 exe + assets  ← 唯一的分发目录
├─ resources/
│  ├─ app.ico                      应用图标（exe + 窗口共用）
│  ├─ app.manifest                 正式清单：requireAdministrator + PerMonitorV2
│  ├─ app.dev.manifest             开发清单：asInvoker
│  ├─ app.rc / app.dev.rc          资源脚本（图标 + 清单 + 版本信息 + 内嵌模板）
│  ├─ templates.rcinc              内嵌模板资源声明
│  ├─ version.rcinc                版本信息（两个 rc 共用）
│  └─ resource.h                   控件与资源 ID
├─ assets/1920x1080/*.png          识别模板源文件（构建时复制到 release/assets）
└─ src/
   ├─ main.cpp                     入口：命令行解析、COM 初始化、权限检查、消息循环、各测试模式
   ├─ util/
   │  ├─ ComPtr.h                   极简 COM 智能指针（不依赖 wrl/client.h，兼容 MSVC/MinGW）
   │  ├─ Log.h/.cpp                 线程安全文件日志
   │  └─ StringUtil.h/.cpp          UTF-8/16 转换、格式化、路径拼接
   ├─ core/
   │  ├─ Image.h/.cpp               BGR/灰度图像 + WIC 读写 PNG + 缩放 + 绘制
   │  └─ Cv.h/.cpp                  模板匹配 / HSV 判定 / 连通域（替代 OpenCV）
   ├─ capture/
   │  ├─ ScreenCapture.h            捕获抽象 + 工厂
   │  ├─ DxgiCapture.h/.cpp         DXGI Desktop Duplication（主后端）
   │  ├─ GdiCapture.h/.cpp          GDI BitBlt（兜底）
   │  └─ WindowFinder.h/.cpp        游戏窗口定位、前台判定、提权查询
   ├─ input/
   │  ├─ InputSimulator.h/.cpp      SendInput 键鼠封装
   │  └─ KeyboardHook.h/.cpp        WH_KEYBOARD_LL 捕获 F12
   ├─ autoskip/
   │  ├─ AutoSkipConfig.h           配置结构体
   │  ├─ SkipAssets.h/.cpp          模板素材加载（外部文件优先 + 内嵌资源回退）
   │  └─ AutoSkipEngine.h/.cpp      工作线程 + 50ms 主循环
   └─ ui/
      └─ MainWindow.h/.cpp          Win32 界面（自绘卡片 + 按 DPI 重算布局）
```

### 构建目录约定

| 目录 | 内容 | 是否入库 |
|------|------|----------|
| `build/obj/` | 正式版 `.o` / `.res` | 否（.gitignore） |
| `build/dev/` | 开发版中间产物与开发版 exe | 否（.gitignore） |
| `release/` | 正式版 exe 与 `assets/` | 可入库（分发产物） |

## 六、实现要点

### 1. 视觉算法为什么自己实现

为了做到**零第三方依赖、单文件不到 1 MB**，视觉算法全部自己实现，而非引入 OpenCV 等大型库。实际只用到模板匹配、HSV 判定、连通域等少量函数，全部自实现后：

| 需要的能力 | 实现方式 |
|---|---|
| 模板匹配 CCoeffNormed | 积分图求窗口统计量 + 精确整数点积，多线程按行切分 |
| 找多个匹配 | 局部极大值 + 非极大值抑制（替代 `findContours` 那套） |
| BGR→灰度 / HSV | BT.601 灰度、与 OpenCV 一致的 H∈[0,180] 取值 |
| 橙色判定 | HSV 阈值统计像素占比 |
| 连通域标记 | 8 邻接 BFS（替代 `findContours` + `approxPolyDP`） |
| 缩放 | 缩小用区域平均、放大用双线性 |
| PNG 读写 | 系统 WIC（`IWICImagingFactory`） |

**模板匹配的精度细节**：一开始把模板"减均值后取整"存成 `int`，导致满匹配只得到 **0.9546** 而不是 1.0（取整误差约 4.5%，会吃掉阈值余量）。改成保留原始整数模板、用
`分子 = ΣT·I - (ΣI·ΣT)/N` 的精确公式后，满匹配回到 **1.0000**。

### 2. 界面：Win32 自绘卡片

Win32 默认外观比较陈旧，这里做了几件事：

- 窗口类不设背景刷（`hbrBackground = nullptr`），背景与卡片全部在 `WM_PAINT` 里双缓冲绘制
- 用 `RoundRect` 画白色圆角卡片，替代 90 年代风格的 `BS_GROUPBOX`
- 区块标题用"强调竖条 + 文字"绘制
- **`WM_CTLCOLORSTATIC` 按控件所处位置返回对应底色**（卡片内返回白色、卡片外返回窗口灰），
  否则静态控件会用系统按钮面色，出现一块块灰色补丁
- 启动/停止按钮用 `BS_OWNERDRAW` + `WM_DRAWITEM` 自绘圆角，绿色/红色随状态切换
- `WM_ERASEBKGND` 返回 1 避免闪烁

### 3. DPI

- 清单声明 `PerMonitorV2`
- 所有布局坐标经过 `Dp()` 按实际 DPI 缩放（本机 200% 缩放下，820×600 逻辑尺寸会变成 1640×1200 物理像素）
- 窗口无 `WS_THICKFRAME`，禁止缩放
- 字体用 `CreateFontW(-MulDiv(pt, dpi, 72), ...)` 按 DPI 生成

### 4. 焦点不在游戏上时不做任何处理

引擎主循环每轮先判断"是否值得干活"（窗口存在、未最小化、**游戏在前台**）：

```cpp
const int intervalMs = IsGameReadyForProcessing() ? config_.loopIntervalMs   // 50ms
                                                  : kIdleIntervalMs;         // 250ms
```

不满足条件时**完全跳过采集与识别**，只以 250ms 的低频确认焦点是否回来，
帧率显示为 0。这样切到别的窗口时几乎不占 CPU，也不会误触其他窗口。

### 5. 界面为什么不再闪

两个原因，都已修掉：

1. **启动/停止按钮**：`RefreshState()` 每 250ms 被定时器调用，
   之前**无条件**执行 `InvalidateRect(button, nullptr, TRUE)`——
   第三个参数 `TRUE` 表示先擦背景再重绘，于是每 250ms 闪一次。
   现在只在运行状态**真正变化**时重绘，并且不再擦除背景（`FALSE`）。
2. **状态文本**：`SetWindowTextW` 会让静态控件整块擦除重画。
   现在先比较当前文本，**内容没变就不调用**。

## 七、底部"继续对话"指示器

它其实是**金色菱形外框 + 内部实心倒三角**的组合，位置固定在对话框下方正中。

早期版本直接套用一套偏黄的 HSV 范围，**实测完全匹配不上** —— 这是"识别不到底部三角"的根因：

| | H | S | V |
|---|---|---|---|
| 初始掩码（黄） | 0~25 | **≥240** | **≥229** |
| **指示器实测** | 12~21 | **140~180** | **160~200** |
| 背景肤色实测 | 6~15 | 80~120 | 80~140 |

两者色相接近，但**饱和度/亮度分得很开**。现在用 `H∈[5,40]、S≥135、V≥155` 就能干净分离，
背景肤色（S≤120、V≤140）全部被排除。

| 项 | 值 |
|---|---|
| 位置 | 1080p 下中心约 `(956, 1045)`，搜索区 `±50 × ±45`（随分辨率等比缩放） |
| 颜色 | 金色 `HSV(5,135,155)~(40,255,255)` |
| 判定 | 区域内金色像素数 ≥ `270 × scale² × 0.35`，且包围盒不超过 `45 × scale` |

> 注：指示器由"菱形外框 + 实心三角"两部分组成，轮廓顶点数并不稳定，
> 因此没有采用基于轮廓顶点的形状判定，而是改用"金色像素数量 + 包围盒尺寸"两个指标，
> 反而更稳，也避开了自己实现轮廓追踪的复杂度。

**离线验证**：拿一张真实游戏截图直接跑，不需要开游戏

```bash
make dev
build/dev/BetterYuanshen.exe --detect-image 截图.png
# 输出 截图.png.detect.txt（识别结果）与 截图.png.detect.png（带紫框标注）
```

实测效果：包围盒 `(944,1030) 29x30`，金色像素 458，紫框精确框住指示器。

## 八、踩过的坑（已在代码中规避）

### 工作线程必须自己初始化 COM（WIC 依赖）★

**这是"识别素材加载失败、所有匹配分数恒为 0"的根因。**

引擎跑在 `std::thread` 上，而 PNG 编解码走的是 WIC，
`CoCreateInstance(CLSID_WICImagingFactory)` 要求**调用线程已初始化 COM**。

主线程在 `wWinMain` 里调过 `CoInitializeEx`，但**工作线程是独立的 COM apartment**。
不初始化的话 `CoCreateInstance` 直接返回 `CO_E_NOTINITIALIZED`，
`Image::LoadFromFile` 和 `Image::LoadFromMemory` 会全部失败 ——
**外部模板文件与 exe 内嵌资源都加载不了**，于是模板为空、匹配分数恒为 0，
表现为"对话识别和底部指示器完全识别不到"。

```cpp
void AutoSkipEngine::Loop() {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // ... 主循环 ...
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
}
```

`make cv-test` 里有一项专门验证这个：

```
工作线程素材加载：不初始化 COM -> 0/5 成功；初始化 COM -> 5/5 成功
  => 符合预期：工作线程必须自行初始化 COM
```

> 排查经验：这个 bug 的特点是**离线测试全过、实机全废**——
> 因为 `--cv-test` / `--detect-image` 都跑在主线程（COM 已初始化），
> 只有引擎的工作线程才会踩到。

### DXGI 空帧
桌面无变化时 `AcquireNextFrame` **仍会返回一帧，但表面内容未初始化（全黑）**。必须判断：

```cpp
if (frameInfo.LastPresentTime.QuadPart == 0 && frameInfo.AccumulatedFrames == 0) {
    return false;   // 视为无新帧，复用缓存
}
```
仅鼠标移动（`LastMouseUpdateTime` 非 0）也不会填充画面，不能作为有效帧的依据。

### D3D11CreateDevice 的 DriverType
传入显式 adapter 时 **`DriverType` 必须是 `D3D_DRIVER_TYPE_UNKNOWN`**，传 `D3D_DRIVER_TYPE_HARDWARE` 会返回 `E_INVALIDARG`。

### FeatureLevel 11_1
部分系统上请求 `D3D_FEATURE_LEVEL_11_1` 会返回 `E_INVALIDARG`，需退回只请求 `11_0`。

### F12 不能用 RegisterHotKey
**F12 被 Windows 保留给内核调试器**，`RegisterHotKey` 注册必然失败。改用 `SetWindowsHookEx(WH_KEYBOARD_LL)`。

钩子回调里**绝不能做耗时操作**（超过 `LowLevelHooksTimeout` 会被系统静默移除），所以只 `PostMessage` 给主窗口，实际处理在 UI 线程。

### LoadImage 是宏
Windows 头文件把 `LoadImage` 定义成了宏（映射到 `LoadImageW`），自定义方法不能叫这个名字 —— 本项目里叫 `Image::LoadFromFile`。

### MinGW 需要 -municode
用 `wWinMain` 作为入口时，MinGW 必须加 `-municode`，否则链接报 `undefined reference to WinMain`。

## 九、功能清单

| 功能 | 状态 |
|------|------|
| 自动推进对话（空格 / 交互键 F） | ✅ |
| 键盘选择对话选项（W/S + F，不移动鼠标） | ✅ |
| 优先选择橙色选项（HSV 判定） | ✅ |
| 底部指示器识别与点击（金色像素数 + 包围盒） | ✅ |
| 黑屏剧情点击推进 | ✅ |
| 自动关闭弹出页面 | ✅ |
| F12 全局热键启停 | ✅ |
| 仅前台运行 / 管理员权限 / DPI 适配 | ✅ |
| 运行状态诊断 / 保存调试截图 / 诊断报告 | ✅ |
| 日志落盘到 `log/app_yyyyMMdd.log` | ✅ |

## 十、免责声明

本工具仅通过**模拟键鼠输入 + 屏幕识别**实现自动化，不读写游戏内存、不注入进程。
但任何自动化操作都存在账号风险，请自行评估后使用。

## 十一、许可证

本项目以 **MIT 许可证** 开源，详见仓库中的 [LICENSE](LICENSE) 文件。
