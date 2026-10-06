#pragma once

// 极简 COM 智能指针。
// 不依赖 MSVC 的 <wrl/client.h>，这样 MSVC 与 MinGW 都能编译。

#include <utility>

namespace bys {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }

    // 取地址用于接收 COM 接口指针；会先释放原有对象
    T** GetAddressOf() {
        Reset();
        return &p_;
    }

    // 不释放原有对象，直接取地址（用于 QueryInterface 的 out 参数之外的特殊场景）
    T** ReleaseAndGetAddressOf() { return GetAddressOf(); }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    bool operator==(std::nullptr_t) const { return p_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return p_ != nullptr; }

    void Reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }

    // 接管一个已经 AddRef 过的裸指针
    void Attach(T* raw) {
        Reset();
        p_ = raw;
    }

    // 放弃所有权，返回裸指针（调用方负责 Release）
    T* Detach() {
        T* raw = p_;
        p_ = nullptr;
        return raw;
    }

private:
    T* p_ = nullptr;
};

}  // namespace bys
