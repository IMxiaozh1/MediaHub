#pragma once

#include <QString>

namespace mediahub::gui {

// 保存播放器外壳的个性化主题；背景路径只写入用户本机配置。
struct ThemeSettings {
  QString accentKey{QStringLiteral("default")};
  QString backgroundImagePath;
  int backgroundBlur{0};
  int backgroundOpacity{55};
  QString appearanceMode{QStringLiteral("dark")};
  QString customAccentColor;

  friend bool operator==(const ThemeSettings&, const ThemeSettings&) = default;
};

[[nodiscard]] ThemeSettings normalizedThemeSettings(
    const ThemeSettings& settings);

}  // namespace mediahub::gui
