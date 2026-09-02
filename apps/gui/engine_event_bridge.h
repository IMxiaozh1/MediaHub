#pragma once

#include <QMetaType>
#include <QObject>
#include <atomic>
#include <chrono>
#include <optional>

#include "mediahub/core/player_engine.h"

namespace mediahub::gui {

using OptionalDuration = std::optional<std::chrono::milliseconds>;

// 把任意内核线程产生的值类型事件转交给 Qt 信号系统，不承载业务逻辑。
class EngineEventBridge final : public QObject,
                                public core::PlayerEventListener {
  Q_OBJECT

 public:
  explicit EngineEventBridge(QObject* parent = nullptr);

  // 调用线程：GUI 主线程。关闭后到达的内核事件会被直接丢弃。
  void deactivate() noexcept;

  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onOpenStarted(core::OpenRequestId requestId) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onStateChanged(core::OpenRequestId requestId,
                      core::PlaybackState state) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onPositionChanged(core::OpenRequestId requestId,
                         core::PlaybackPosition position) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onDurationChanged(core::OpenRequestId requestId,
                         OptionalDuration duration) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onBufferingChanged(core::OpenRequestId requestId,
                          int percentage) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onAudioWaveformChanged(core::OpenRequestId requestId,
                              core::AudioWaveform waveform) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onEndReached(core::OpenRequestId requestId) noexcept override;
  // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
  void onError(core::OpenRequestId requestId,
               core::PlaybackError error) noexcept override;
  // 调用线程：旧播放器回收线程。函数只投递已释放的嵌入句柄。
  void onVideoSurfaceReleased(void* nativeHandle) noexcept override;

 signals:
  void openStarted(mediahub::core::OpenRequestId requestId);
  void stateChanged(mediahub::core::OpenRequestId requestId,
                    core::PlaybackState state);
  void positionChanged(mediahub::core::OpenRequestId requestId,
                       core::PlaybackPosition position);
  void durationChanged(mediahub::core::OpenRequestId requestId,
                       mediahub::gui::OptionalDuration duration);
  void bufferingChanged(mediahub::core::OpenRequestId requestId,
                        int percentage);
  void audioWaveformChanged(mediahub::core::OpenRequestId requestId,
                            core::AudioWaveform waveform);
  void endReached(mediahub::core::OpenRequestId requestId);
  void errorOccurred(mediahub::core::OpenRequestId requestId,
                     core::PlaybackError error);
  void videoSurfaceReleased(void* nativeHandle);

 private:
  std::atomic_bool isActive_{true};
};

}  // namespace mediahub::gui

Q_DECLARE_METATYPE(mediahub::core::PlaybackState)
Q_DECLARE_METATYPE(mediahub::core::PlaybackPosition)
Q_DECLARE_METATYPE(mediahub::core::PlaybackError)
Q_DECLARE_METATYPE(mediahub::core::AudioWaveform)
Q_DECLARE_METATYPE(mediahub::gui::OptionalDuration)
