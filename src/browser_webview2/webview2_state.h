#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>

namespace mediahub::browser_webview2 {

enum class SuspensionAction {
    None,
    TrySuspend,
    Resume,
};

struct SuspensionStep {
    SuspensionAction action{SuspensionAction::None};
    std::uint64_t requestSerial{0};
};

// 协调异步挂起与同步恢复，避免旧完成回调覆盖最新可见性目标。
class SuspensionCoordinator final {
 public:
    explicit SuspensionCoordinator(const bool isSuspended = true) noexcept
        : isSuspendedDesired_(isSuspended) {}

    // 调用线程：创建 Controller 的 GUI STA。
    [[nodiscard]] SuspensionStep controllerReady() noexcept {
        isControllerAvailable_ = true;
        actualState_ = ActualState::Active;
        inFlight_ = InFlight::None;
        return settle(true);
    }

    // 调用线程：GUI 主线程；Controller 尚未创建时只保存最终目标。
    void setDesired(const bool isSuspended) noexcept {
        isSuspendedDesired_ = isSuspended;
    }

    // 调用线程：GUI 主线程；重复目标或已有异步动作时不创建新动作。
    [[nodiscard]] SuspensionStep request(const bool isSuspended) noexcept {
        const bool didChange = isSuspendedDesired_ != isSuspended;
        isSuspendedDesired_ = isSuspended;
        if (!isControllerAvailable_ || !didChange || inFlight_ != InFlight::None) {
            return {};
        }
        return settle(true);
    }

    // 调用线程：TrySuspend 完成回调所在的 GUI STA。
    [[nodiscard]] SuspensionStep completeTrySuspend(
        const std::uint64_t requestSerial,
        const bool didCallSucceed,
        const bool didSuspend) noexcept {
        if (inFlight_ != InFlight::Suspending || requestSerial != activeRequestSerial_) {
            return {};
        }
        inFlight_ = InFlight::None;
        actualState_ = didCallSucceed && didSuspend ? ActualState::Suspended
                                                   : ActualState::Active;
        return settle(didCallSucceed && didSuspend);
    }

    // 调用线程：执行同步 Resume 的 GUI STA。
    [[nodiscard]] SuspensionStep completeResume(
        const std::uint64_t requestSerial,
        const bool didSucceed) noexcept {
        if (inFlight_ != InFlight::Resuming || requestSerial != activeRequestSerial_) {
            return {};
        }
        inFlight_ = InFlight::None;
        actualState_ = didSucceed ? ActualState::Active : ActualState::Suspended;
        return settle(didSucceed);
    }

    // 调用线程：释放 Controller 的 GUI STA；使旧 serial 的完成回调失效。
    void invalidate() noexcept {
        ++nextRequestSerial_;
        activeRequestSerial_ = 0;
        isControllerAvailable_ = false;
        actualState_ = ActualState::Unavailable;
        inFlight_ = InFlight::None;
    }

    // 调用线程：GUI STA；未确认处于活动态前始终要求保持静音。
    [[nodiscard]] bool mustMute() const noexcept {
        return isSuspendedDesired_ || actualState_ == ActualState::Suspended ||
               inFlight_ != InFlight::None;
    }

 private:
    enum class ActualState {
        Unavailable,
        Active,
        Suspended,
    };

    enum class InFlight {
        None,
        Suspending,
        Resuming,
    };

    [[nodiscard]] SuspensionStep settle(const bool canStartAction) noexcept {
        if (!isControllerAvailable_ || inFlight_ != InFlight::None ||
            !canStartAction) {
            return {};
        }
        if (isSuspendedDesired_ && actualState_ == ActualState::Active) {
            return begin(InFlight::Suspending, SuspensionAction::TrySuspend);
        }
        if (!isSuspendedDesired_ && actualState_ == ActualState::Suspended) {
            return begin(InFlight::Resuming, SuspensionAction::Resume);
        }
        return {};
    }

    [[nodiscard]] SuspensionStep begin(const InFlight inFlight,
                                       const SuspensionAction action) noexcept {
        inFlight_ = inFlight;
        activeRequestSerial_ = ++nextRequestSerial_;
        return SuspensionStep{action, activeRequestSerial_};
    }

    bool isSuspendedDesired_{true};
    bool isControllerAvailable_{false};
    ActualState actualState_{ActualState::Unavailable};
    InFlight inFlight_{InFlight::None};
    std::uint64_t nextRequestSerial_{0};
    std::uint64_t activeRequestSerial_{0};
};

struct NavigationStart {
    std::uint64_t generation{0};
    bool shouldReport{false};
};

struct NavigationCompletion {
    std::uint64_t generation{0};
    bool shouldReport{false};
};

// 将 Navigate 请求代次按接受顺序绑定到 WebView2 后续分配的 navigationId。
class NavigationTracker final {
 public:
    explicit NavigationTracker(const std::uint64_t generation = 0) noexcept
        : currentGeneration_(generation) {}

    // 调用线程：创建或释放 Controller 的 GUI STA。
    void reset(const std::uint64_t generation) noexcept {
        currentGeneration_ = generation;
        pendingGenerations_.clear();
        generationsById_.clear();
        activeNavigationId_.reset();
        isNavigating_ = false;
    }

    // 调用线程：GUI 主线程；不创建显式导航待绑定项。
    void setCurrentGeneration(const std::uint64_t generation) noexcept {
        currentGeneration_ = generation;
    }

    // 调用线程：GUI 主线程；仅在 Navigate 返回成功后记录。
    void acceptNavigate(const std::uint64_t generation) {
        currentGeneration_ = generation;
        pendingGenerations_.push_back(generation);
        isNavigating_ = true;
    }

    // 调用线程：NavigationStarting 所在的 GUI STA。
    [[nodiscard]] NavigationStart start(const std::uint64_t navigationId) {
        const auto existing = generationsById_.find(navigationId);
        if (existing != generationsById_.end()) {
            activeNavigationId_ = navigationId;
            isNavigating_ = true;
            return NavigationStart{existing->second, false};
        }

        std::uint64_t generation = currentGeneration_;
        if (!pendingGenerations_.empty()) {
            generation = pendingGenerations_.front();
            pendingGenerations_.pop_front();
        }
        generationsById_.emplace(navigationId, generation);
        activeNavigationId_ = navigationId;
        isNavigating_ = true;
        return NavigationStart{generation, true};
    }

    // 调用线程：NavigationCompleted 所在的 GUI STA。
    [[nodiscard]] NavigationCompletion complete(
        const std::uint64_t navigationId) noexcept {
        const auto existing = generationsById_.find(navigationId);
        if (existing == generationsById_.end()) {
            return {};
        }
        const std::uint64_t generation = existing->second;
        generationsById_.erase(existing);
        if (!activeNavigationId_.has_value() ||
            *activeNavigationId_ != navigationId) {
            return NavigationCompletion{generation, false};
        }

        activeNavigationId_.reset();
        isNavigating_ = !pendingGenerations_.empty();
        return NavigationCompletion{generation, true};
    }

    // 调用线程：GUI STA。
    [[nodiscard]] bool isNavigating() const noexcept { return isNavigating_; }

 private:
    std::uint64_t currentGeneration_{0};
    std::deque<std::uint64_t> pendingGenerations_;
    std::unordered_map<std::uint64_t, std::uint64_t> generationsById_;
    std::optional<std::uint64_t> activeNavigationId_;
    bool isNavigating_{false};
};

}  // namespace mediahub::browser_webview2
