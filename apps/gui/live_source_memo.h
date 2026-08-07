#pragma once

#include <QString>

namespace mediahub::gui {

// 用户手工保存的直播源备忘，只记录地址和说明，不触发解析或播放。
struct LiveSourceMemo {
  QString sourceUrl;
  QString note;

  [[nodiscard]] bool operator==(const LiveSourceMemo&) const = default;
};

}  // namespace mediahub::gui
