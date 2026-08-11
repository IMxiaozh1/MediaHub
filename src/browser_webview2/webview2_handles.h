#pragma once

#include <WebView2.h>

#include <functional>
#include <utility>

namespace mediahub::browser_webview2 {

// 持有事件撤销动作，确保控制器关闭前不会遗留 WebView2 事件订阅。
class EventRegistration final {
 public:
    EventRegistration() = default;
    // 调用线程：创建订阅的 GUI STA，析构时在同一线程撤销事件。
    ~EventRegistration() { reset(); }

    EventRegistration(const EventRegistration&) = delete;
    EventRegistration& operator=(const EventRegistration&) = delete;

    EventRegistration(EventRegistration&& other) noexcept
        : token_(other.token_), revoke_(std::move(other.revoke_)) {
        other.revoke_ = {};
    }

    EventRegistration& operator=(EventRegistration&& other) noexcept {
        if (this != &other) {
            reset();
            token_ = other.token_;
            revoke_ = std::move(other.revoke_);
            other.revoke_ = {};
        }
        return *this;
    }

    // 调用线程：创建事件源的 GUI STA，保存同一 STA 使用的撤销动作。
    void bind(const EventRegistrationToken token,
              std::function<HRESULT(EventRegistrationToken)> revoke) {
        reset();
        token_ = token;
        revoke_ = std::move(revoke);
    }

    // 调用线程：创建事件源的 GUI STA，必须在释放对应 COM 对象前调用。
    void reset() noexcept {
        if (!revoke_) {
            return;
        }
        auto revoke = std::move(revoke_);
        revoke_ = {};
        static_cast<void>(revoke(token_));
        token_ = {};
    }

 private:
    EventRegistrationToken token_{};
    std::function<HRESULT(EventRegistrationToken)> revoke_;
};

}  // namespace mediahub::browser_webview2
