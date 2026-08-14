#pragma once

#include <QColor>
#include <QIcon>

namespace mediahub::gui {

enum class BrowserIcon {
    Back,
    Forward,
    Reload,
    Stop,
    Home,
    Favorite,
    FavoriteFilled,
    Audio,
    AudioMuted,
    Download,
    More,
    TabSearch,
    NewTab,
    Close,
    SiteControl,
    History,
    Settings,
    Permissions,
    ClearData,
    ZoomIn,
    ZoomOut,
    Group,
    FindPrevious,
    FindNext,
};

// 生成并缓存适配高 DPI 的浏览器矢量图标，不依赖外部图标资源。
class BrowserIconProvider final {
 public:
    [[nodiscard]] static QIcon icon(BrowserIcon type, const QColor& color,
                                    int logicalSize = 18);
};

}  // namespace mediahub::gui
