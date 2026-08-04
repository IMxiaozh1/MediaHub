#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <memory>

namespace mediahub::gui {

struct LyricLine {
  qint64 timeMilliseconds{0};
  QString text;
};

enum class LyricsResultKind {
  Ready,
  NotFound,
  Error,
};

struct LyricsQuery {
  QString filePath;
  QString displayName;
  qint64 durationMilliseconds{-1};
};

struct LyricsResult {
  LyricsResultKind kind{LyricsResultKind::NotFound};
  QString title;
  QString artist;
  QString album;
  QString sourceName;
  QString message;
  QString plainText;
  QVector<LyricLine> synchronizedLines;
};

// 歌词服务只负责读取和查询，不接触播放器内核或界面控件。
class LyricsService : public QObject {
  Q_OBJECT

 public:
  explicit LyricsService(QObject* parent = nullptr) : QObject(parent) {}
  ~LyricsService() override = default;

  // 调用线程：GUI 主线程。新请求必须取消旧媒体仍在进行的查询。
  virtual void requestLyrics(const LyricsQuery& query) = 0;
  // 调用线程：GUI 主线程。取消后不得再发布旧媒体结果。
  virtual void cancel() noexcept = 0;

 signals:
  void resultReady(mediahub::gui::LyricsResult result);
};

// 默认在线实现：内嵌歌词优先，随后依次查询酷狗、网易云、LRCLIB 和 TheAudioDB。
class OnlineLyricsService final : public LyricsService {
  Q_OBJECT

 public:
  explicit OnlineLyricsService(QObject* parent = nullptr);
  ~OnlineLyricsService() override;

  void requestLyrics(const LyricsQuery& query) override;
  void cancel() noexcept override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

namespace lyrics_internal {

// 供在线响应、内嵌歌词和自动化测试复用的 LRC 时间轴解析器。
[[nodiscard]] QVector<LyricLine> parseSynchronizedLyrics(const QString& lyrics);
[[nodiscard]] QString plainTextFromLyrics(const QString& lyrics);

// 供候选选择和自动化测试共用，避免同名但时长不同的歌曲被误配。
[[nodiscard]] bool isAcceptableTrackMatch(const QString& expectedTitle,
                                          const QString& expectedArtist,
                                          qint64 expectedDurationMilliseconds,
                                          const QString& candidateTitle,
                                          const QString& candidateArtist,
                                          qint64 candidateDurationMilliseconds);
[[nodiscard]] QString bestKugouTrackIdentity(
    const QByteArray& payload, const QString& expectedTitle,
    const QString& expectedArtist, qint64 expectedDurationMilliseconds);
[[nodiscard]] QString decodeKugouLyricsPayload(const QByteArray& payload);

}  // namespace lyrics_internal

}  // namespace mediahub::gui

Q_DECLARE_METATYPE(mediahub::gui::LyricsResult)
