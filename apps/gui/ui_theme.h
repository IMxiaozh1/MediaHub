#pragma once

#include <QString>

namespace mediahub::gui {

struct PlayerViewState;

// 界面展示模式只描述视觉语义，不参与播放状态或命令路由。
enum class UiPresentationMode {
  LocalAudio,
  LocalVideo,
  Live,
};

[[nodiscard]] UiPresentationMode presentationModeFor(
    const PlayerViewState& viewState);
[[nodiscard]] QString presentationModeKey(UiPresentationMode mode);
[[nodiscard]] const QString& mainWindowStyleSheet();

}  // namespace mediahub::gui
