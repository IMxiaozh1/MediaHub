#pragma once

#include <QString>

namespace mediahub::gui {

inline constexpr int kProgressMaximum = 1000;

// 主窗口渲染所需的完整快照；控件不自行推导任何播放规则。
struct PlayerViewState {
  QString mediaName;
  QString statusText;
  QString videoPlaceholder{QStringLiteral("打开媒体后，画面会出现在这里")};
  QString positionText{QStringLiteral("00:00 / --:--")};
  QString volumeText{QStringLiteral("音量 100%")};
  int progressValue{0};
  int volumeValue{100};
  int currentPlaylistIndex{-1};
  int playbackModeIndex{0};
  double playbackRate{1.0};
  bool isTemporaryFastPlayback{false};
  bool canOpen{true};
  bool canPlay{false};
  bool canPause{false};
  bool canStop{false};
  bool canSeek{false};
  bool canGoPrevious{false};
  bool canGoNext{false};
  bool canRemovePlaylistItem{false};
  bool isMuted{false};
  bool isVideoSurfaceActive{false};
  bool isAudioVisualizationActive{false};
  bool isAudioVisualizationPlaying{false};
  bool canToggleFullscreen{false};
};

}  // namespace mediahub::gui
