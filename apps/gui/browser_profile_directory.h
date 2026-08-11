#pragma once

#include <QString>

namespace mediahub::gui {

// 从绝对应用数据目录构造 MediaHub 专用 Profile；无效基目录返回空路径。
[[nodiscard]] QString makeBrowserProfileDirectory(
    const QString& appLocalDataLocation);

}  // namespace mediahub::gui
