#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "mediahub/core/player_engine.h"

namespace mediahub::engine_vlc {

// 以下选项仅供自动化测试使用；正式程序使用默认值。
struct VlcPlayerEngineOptions {
  bool useDummyAudioOutput{false};
  bool useDummyVideoOutput{false};
  // 仅供自动化观察实际提交给 libVLC 的视频句柄，正式程序保持为空。
  std::function<void(void*)> videoSurfaceObserver;
  // 仅供自动化统计 libVLC 实例创建次数，正式程序保持为空。
  std::function<void()> instanceCreatedObserver;
  // 仅供自动化观察 libVLC 初始化参数，正式程序保持为空。
  std::function<void(std::string_view)> initializationArgumentObserver;
  // 仅供自动化观察逐媒体提交的 libVLC 选项，正式程序保持为空。
  std::function<void(std::string_view)> mediaOptionObserver;
  // 仅供自动化控制旧播放器停止时序，正式程序保持为空。
  std::function<void()> beforeRetiredPlayerStop;
};

// libVLC 播放实现。线程、异步请求和监听器生命周期约定继承 PlayerEngine。
class VlcPlayerEngine final : public core::PlayerEngine {
 public:
  explicit VlcPlayerEngine(VlcPlayerEngineOptions options = {});
  ~VlcPlayerEngine() override;

  VlcPlayerEngine(const VlcPlayerEngine&) = delete;
  VlcPlayerEngine& operator=(const VlcPlayerEngine&) = delete;
  VlcPlayerEngine(VlcPlayerEngine&&) = delete;
  VlcPlayerEngine& operator=(VlcPlayerEngine&&) = delete;

  core::OpenRequestId open(core::MediaItem item,
                           void* nativeVideoHandle = nullptr) override;
  void play() override;
  void pause() override;
  void stop() override;
  void seek(std::chrono::milliseconds position) override;
  void setVolume(int volume) override;
  void setMuted(bool isMuted) override;
  void setPlaybackRate(double rate) override;
  void setVideoSurface(void* nativeHandle) override;

  [[nodiscard]] core::PlaybackState state() const override;
  [[nodiscard]] core::PlaybackPosition position() const override;
  [[nodiscard]] std::optional<std::chrono::milliseconds> duration()
      const override;
  [[nodiscard]] bool isSeekable() const override;
  void setEventListener(core::PlayerEventListener* listener) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mediahub::engine_vlc
