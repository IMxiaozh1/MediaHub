#pragma once

class QWidget;

namespace mediahub::gui {

// 调用线程：GUI 主线程。同步 Windows 原生标题栏的深浅外观。
void setNativeDarkTitleBar(QWidget* window, bool isDark);

}  // namespace mediahub::gui
