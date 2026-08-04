#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace mediahub::gui {

// 用户请求退出后限制第三方组件的清理时长，避免窗口消失后留下无响应进程。
class ShutdownWatchdog final {
 public:
  using TimeoutAction = std::function<void()>;

  explicit ShutdownWatchdog(
      std::chrono::milliseconds timeout = std::chrono::seconds(3),
      TimeoutAction timeoutAction = {});
  ~ShutdownWatchdog();

  ShutdownWatchdog(const ShutdownWatchdog&) = delete;
  ShutdownWatchdog& operator=(const ShutdownWatchdog&) = delete;

  // 调用线程：GUI 主线程。重复调用只会保留第一次启动的计时。
  void arm() noexcept;
  // 调用线程：GUI 主线程。正常清理完成后取消超时动作。
  void complete() noexcept;

 private:
  std::chrono::milliseconds timeout_;
  TimeoutAction timeoutAction_;
  std::mutex mutex_;
  std::condition_variable completed_;
  std::thread worker_;
  bool isComplete_{false};
};

}  // namespace mediahub::gui
