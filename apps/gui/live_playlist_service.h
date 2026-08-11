#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>
#include <cstddef>
#include <cstdint>

#include "mediahub/core/live_channel.h"

class QNetworkAccessManager;
class QNetworkReply;
class QFutureWatcherBase;
class QTimer;

namespace mediahub::gui {

struct LivePlaylistLimits {
  int timeoutMilliseconds{10000};
  int maximumRedirects{5};
  qint64 maximumResponseBytes{2 * 1024 * 1024};
  std::size_t maximumEntries{5000};
};

enum class LivePlaylistLoadError {
  InvalidUrl,
  LocalFileUnreadable,
  NetworkFailure,
  Timeout,
  TooManyRedirects,
  UnsafeRedirect,
  ResponseTooLarge,
  InvalidUtf8,
  InvalidFormat,
  HlsMediaManifest,
  TooManyEntries,
  NoPlayableEntries,
};

struct LivePlaylistLoadResult {
  core::LiveChannelLibrary library;
  std::size_t skippedChannelCount{0};
  std::size_t duplicateChannelCount{0};
};

// 在 GUI 主线程异步读取本地或远程清单，并在解析前强制执行资源与重定向限制。
class LivePlaylistService : public QObject {
  Q_OBJECT

 public:
  explicit LivePlaylistService(QObject* parent = nullptr,
                               LivePlaylistLimits limits = {},
                               QNetworkAccessManager* networkManager = nullptr);
  ~LivePlaylistService() override;

  // 调用线程：GUI 主线程。新请求会取消旧请求，迟到结果不会继续发出信号。
  virtual void load(const QString& playlistUrl);
  // 调用线程：GUI 主线程。本地文件在线程池读取，不阻塞界面事件循环。
  virtual void loadLocalFile(const QString& filePath);
  // 调用线程：GUI 主线程。取消当前请求但不报告失败。
  virtual void cancel() noexcept;

 signals:
  void loadSucceeded(mediahub::gui::LivePlaylistLoadResult result);
  void loadFailed(mediahub::gui::LivePlaylistLoadError error);

 private:
  void startRequest(const QUrl& url, std::uint64_t generation);
  void startNativeRequest(const QUrl& url, std::uint64_t generation);
  void startLocalFileRequest(const QString& filePath,
                             std::uint64_t generation);
  void consumeReplyData(QNetworkReply* reply, std::uint64_t generation);
  void handleReplyFinished(QNetworkReply* reply, std::uint64_t generation);
  void finishResponse(QByteArray responseBody, const QUrl& finalUrl,
                      std::uint64_t generation);
  void finishFailure(LivePlaylistLoadError error, std::uint64_t generation);
  [[nodiscard]] bool isCurrentReply(const QNetworkReply* reply,
                                    std::uint64_t generation) const noexcept;

  LivePlaylistLimits limits_;
  QNetworkAccessManager* networkManager_{nullptr};
  QTimer* timeoutTimer_{nullptr};
  QNetworkReply* currentReply_{nullptr};
  QFutureWatcherBase* backgroundRequestWatcher_{nullptr};
  QByteArray responseBody_;
  std::uint64_t generation_{0};
  int followedRedirects_{0};
  bool usesNativeNetworking_{false};
};

}  // namespace mediahub::gui

Q_DECLARE_METATYPE(mediahub::gui::LivePlaylistLoadError)
Q_DECLARE_METATYPE(mediahub::gui::LivePlaylistLoadResult)
