#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

#include "live_source_memo.h"
#include "mediahub/core/media_types.h"

class QSettings;

namespace mediahub::gui {

// 保存可跨进程恢复的界面状态；恢复内容不得隐式触发播放或网络请求。
struct AppStateSnapshot {
  std::vector<core::MediaItem> localPlaylist;
  QString lastLivePlaylistUrl;
  QStringList livePlaylistUrlHistory;
  QVector<LiveSourceMemo> liveSourceMemos;
};

// GUI 持久化边界。测试可注入内存实现，正式程序使用 QSettings 实现。
class AppStateStore {
 public:
  virtual ~AppStateStore() = default;

  [[nodiscard]] virtual AppStateSnapshot load() = 0;
  virtual void save(const AppStateSnapshot& snapshot) = 0;
};

// 把用户状态保存到当前操作系统的 MediaHub 配置中。
class QSettingsAppStateStore final : public AppStateStore {
 public:
  QSettingsAppStateStore();
  // 指定 INI 文件仅用于隔离自动化测试和诊断。
  explicit QSettingsAppStateStore(const QString& settingsFilePath);
  ~QSettingsAppStateStore() override;

  [[nodiscard]] AppStateSnapshot load() override;
  void save(const AppStateSnapshot& snapshot) override;

 private:
  std::unique_ptr<QSettings> settings_;
};

}  // namespace mediahub::gui
