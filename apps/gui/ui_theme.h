#pragma once

#include <QColor>
#include <QString>

#include "theme_settings.h"

class QTableWidget;

namespace mediahub::gui {

struct PlayerViewState;

// 界面展示模式只描述视觉语义，不参与播放状态或命令路由。
enum class UiPresentationMode {
  LocalAudio,
  LocalVideo,
  Live,
};

// 一套主题包含完整的界面语义色，而不是只替换单个强调色。
struct UiThemePalette {
  QColor window;
  QColor chrome;
  QColor panel;
  QColor panelAlt;
  QColor canvas;
  QColor text;
  QColor mutedText;
  QColor border;
  QColor hover;
  QColor accent;
  QColor accentHover;
  QColor secondary;
  bool isDark{true};
};

[[nodiscard]] UiPresentationMode presentationModeFor(
    const PlayerViewState& viewState);
[[nodiscard]] QString presentationModeKey(UiPresentationMode mode);
[[nodiscard]] const QString& mainWindowStyleSheet();
[[nodiscard]] UiThemePalette resolvedThemePalette(
    const ThemeSettings& settings);
[[nodiscard]] QString themeOverrideStyleSheet(const ThemeSettings& settings);

// 将表格、视口和表头的调色板统一到同一套主题，避免平台样式漏出浅色底。
void applyTablePalette(QTableWidget* table, const UiThemePalette& palette);

}  // namespace mediahub::gui
