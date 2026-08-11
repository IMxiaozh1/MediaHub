#pragma once

#include <QString>

#include "browser_types.h"

namespace mediahub::gui {

// 规范化用户主动输入的顶层地址；本函数不执行导航或启动外部程序。
[[nodiscard]] BrowserAddress normalizeBrowserAddress(const QString& input);

}  // namespace mediahub::gui
