#pragma once

#include <QWidget>
#include <QtGlobal>

#include "lyrics_service.h"

class QLabel;
class QResizeEvent;
class QStackedLayout;
class QTextBrowser;
class QToolButton;

namespace mediahub::gui {

// 纯音频歌词页：同步歌词显示当前行上下文，普通歌词保留手动滚动能力。
class LyricsView final : public QWidget {
  Q_OBJECT

 public:
  explicit LyricsView(QWidget* parent = nullptr);

  void clearLyrics();
  void showLoading();
  void setResult(const LyricsResult& result);
  void setMediaName(const QString& mediaName);
  void setPosition(qint64 positionMilliseconds);
  [[nodiscard]] qint64 timingOffsetMilliseconds() const noexcept;

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void showMessage(const QString& title, const QString& detail);
  void updateSynchronizedLines();
  void loadTimingOffset(const LyricsResult& result);
  void setTimingOffset(qint64 offsetMilliseconds, bool persist);
  void updateTimingOffsetLabel();
  void updateSynchronizedStyles();

  QLabel* mediaNameLabel_{nullptr};
  QLabel* sourceLabel_{nullptr};
  QLabel* messageTitleLabel_{nullptr};
  QLabel* messageDetailLabel_{nullptr};
  QWidget* timingControls_{nullptr};
  QLabel* timingOffsetLabel_{nullptr};
  QToolButton* timingEarlierButton_{nullptr};
  QToolButton* timingLaterButton_{nullptr};
  QToolButton* timingResetButton_{nullptr};
  QTextBrowser* plainLyricsBrowser_{nullptr};
  QStackedLayout* contentStack_{nullptr};
  QWidget* messagePage_{nullptr};
  QWidget* synchronizedPage_{nullptr};
  QWidget* plainPage_{nullptr};
  QVector<QLabel*> synchronizedLabels_;
  QVector<LyricLine> synchronizedLines_;
  qint64 positionMilliseconds_{0};
  qint64 timingOffsetMilliseconds_{0};
  QString timingSettingsKey_;
  int currentLineIndex_{-1};
};

}  // namespace mediahub::gui
