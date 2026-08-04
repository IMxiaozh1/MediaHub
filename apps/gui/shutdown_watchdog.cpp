#include "shutdown_watchdog.h"

#include <cstdlib>
#include <utility>

namespace mediahub::gui {

ShutdownWatchdog::ShutdownWatchdog(const std::chrono::milliseconds timeout,
                                   TimeoutAction timeoutAction)
    : timeout_(timeout), timeoutAction_(std::move(timeoutAction)) {
  if (!timeoutAction_) {
    timeoutAction_ = [] { std::_Exit(EXIT_SUCCESS); };
  }
}

ShutdownWatchdog::~ShutdownWatchdog() {
  complete();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void ShutdownWatchdog::arm() noexcept {
  if (worker_.joinable()) {
    return;
  }

  try {
    worker_ = std::thread([this] {
      std::unique_lock lock(mutex_);
      if (completed_.wait_for(lock, timeout_, [this] { return isComplete_; })) {
        return;
      }
      const auto timeoutAction = timeoutAction_;
      lock.unlock();
      try {
        timeoutAction();
      } catch (...) {
        // 退出兜底不能让回调异常越过后台线程边界。
      }
    });
  } catch (...) {
    // 无法创建计时线程时仍继续常规清理流程。
  }
}

void ShutdownWatchdog::complete() noexcept {
  {
    const std::lock_guard lock(mutex_);
    isComplete_ = true;
  }
  completed_.notify_all();
}

}  // namespace mediahub::gui
