#include "theme_settings.h"

#include <QColor>
#include <algorithm>
#include <array>

namespace mediahub::gui {
namespace {

constexpr std::array<const char*, 6> kAccentKeys{
    "default", "blue", "green", "orange", "rose", "custom"};

bool isKnownAccent(const QString& accentKey) {
  return std::any_of(kAccentKeys.cbegin(), kAccentKeys.cend(),
                     [&accentKey](const char* const knownKey) {
                       return accentKey == QString::fromLatin1(knownKey);
                     });
}

}  // namespace

ThemeSettings normalizedThemeSettings(const ThemeSettings& settings) {
  ThemeSettings normalized = settings;
  normalized.accentKey = normalized.accentKey.trimmed().toLower();
  if (!isKnownAccent(normalized.accentKey)) {
    normalized.accentKey = QStringLiteral("default");
  }
  normalized.backgroundImagePath = normalized.backgroundImagePath.trimmed();
  normalized.backgroundBlur = std::clamp(normalized.backgroundBlur, 0, 100);
  normalized.backgroundOpacity =
      std::clamp(normalized.backgroundOpacity, 0, 100);
  normalized.appearanceMode = normalized.appearanceMode.trimmed().toLower();
  if (normalized.appearanceMode != QStringLiteral("light")) {
    normalized.appearanceMode = QStringLiteral("dark");
  }
  const QColor customAccent(normalized.customAccentColor.trimmed());
  normalized.customAccentColor = customAccent.isValid()
                                     ? customAccent.name(QColor::HexRgb)
                                     : QString{};
  if (normalized.accentKey == QStringLiteral("custom") &&
      normalized.customAccentColor.isEmpty()) {
    normalized.accentKey = QStringLiteral("default");
  }
  return normalized;
}

}  // namespace mediahub::gui
