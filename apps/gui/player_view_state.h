#pragma once

#include <QString>

namespace mediahub::gui {

// 主窗口渲染所需的完整快照；控件不自行推导任何播放规则。
struct PlayerViewState {
    QString mediaName;
    QString statusText;
    QString videoPlaceholder{QStringLiteral("打开媒体后，画面会出现在这里")};
    bool canOpen{true};
    bool canPlay{false};
    bool canPause{false};
    bool canStop{false};
    bool isVideoSurfaceActive{false};
    bool canToggleFullscreen{false};
};

}  // namespace mediahub::gui
