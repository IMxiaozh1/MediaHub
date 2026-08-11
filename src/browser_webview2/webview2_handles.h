#pragma once

#include <WebView2.h>

#include <functional>
#include <utility>

namespace mediahub::browser_webview2 {

// 持有事件撤销动作，确保控制器关闭前不会遗留 WebView2 事件订阅。
class EventRegistration final {
 public:
    EventRegistration() = default;
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

    void bind(const EventRegistrationToken token,
              std::function<HRESULT(EventRegistrationToken)> revoke) {
        reset();
        token_ = token;
        revoke_ = std::move(revoke);
    }

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
