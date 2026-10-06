#include "autoskip/SkipAssets.h"

#include <windows.h>

#include <cmath>

#include "resources/resource.h"
#include "util/StringUtil.h"

namespace bys {

namespace {

// 从 exe 内嵌的 RCDATA 资源解码出图像
bool LoadFromResource(int resourceId, Image& out) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr) {
        return false;
    }

    DWORD size = SizeofResource(module, resource);
    if (size == 0) {
        return false;
    }

    HGLOBAL handle = LoadResource(module, resource);
    if (handle == nullptr) {
        return false;
    }

    const void* data = LockResource(handle);
    if (data == nullptr) {
        return false;
    }

    // 内嵌资源随模块常驻内存，无需释放
    return Image::LoadFromMemory(data, size, out);
}

}  // namespace

SkipAssets SkipAssets::Load(double scale) {
    SkipAssets assets;
    std::wstring dir = JoinPath(ExecutableDirectory(), L"assets\\1920x1080");

    assets.LoadOne(dir, L"disabled_ui.png", IDR_TEMPLATE_DISABLED_UI, scale,
                   assets.disabledUiButton);
    assets.LoadOne(dir, L"stop_auto.png", IDR_TEMPLATE_STOP_AUTO, scale,
                   assets.stopAutoButton);
    assets.LoadOne(dir, L"icon_option.png", IDR_TEMPLATE_OPTION_ICON, scale,
                   assets.optionIcon);
    assets.LoadOne(dir, L"page_close.png", IDR_TEMPLATE_PAGE_CLOSE, scale,
                   assets.pageClose);
    assets.LoadOne(dir, L"hangout_skip.png", IDR_TEMPLATE_HANGOUT_SKIP, scale,
                   assets.hangoutSkip);

    return assets;
}

void SkipAssets::LoadOne(const std::wstring& dir, const wchar_t* fileName,
                         int resourceId, double scale, Image& out) {
    Image raw;
    bool loaded = false;
    std::wstring source;

    // 1) 外部文件优先（用户可直接替换模板，无需重新编译）
    std::wstring path = JoinPath(dir, fileName);
    if (Image::LoadFromFile(path, raw) && !raw.Empty()) {
        loaded = true;
        source = L"file";
    }

    // 2) 回退到 exe 内嵌资源
    if (!loaded && LoadFromResource(resourceId, raw) && !raw.Empty()) {
        loaded = true;
        source = L"resource";
    }

    if (!loaded) {
        missingFiles.push_back(fileName);
        sources.push_back(L"missing");
        return;
    }

    sources.push_back(source);

    if (std::fabs(scale - 1.0) < 0.001) {
        out = std::move(raw);
        return;
    }

    int newWidth = (int)std::lround(raw.Width() * scale);
    int newHeight = (int)std::lround(raw.Height() * scale);
    if (newWidth < 1) newWidth = 1;
    if (newHeight < 1) newHeight = 1;

    out = raw.Resize(newWidth, newHeight);
}

}  // namespace bys
