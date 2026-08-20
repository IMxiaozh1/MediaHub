#include "native_window_theme.h"

#include <QVariant>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <qt_windows.h>
#endif

namespace mediahub::gui {

void setNativeDarkTitleBar(QWidget* const window, const bool isDark) {
  if (window == nullptr) {
    return;
  }
  window->setProperty("nativeDarkTitleBar", isDark);
#ifdef Q_OS_WIN
  const BOOL enabled = isDark ? TRUE : FALSE;
  const auto handle = reinterpret_cast<HWND>(window->winId());
  static_cast<void>(DwmSetWindowAttribute(
      handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled)));
#else
  Q_UNUSED(isDark);
#endif
}

}  // namespace mediahub::gui
