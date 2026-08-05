#include "lyrics_view.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QStackedLayout>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <memory>

namespace mediahub::gui {
namespace {

constexpr int kVisibleLineCount = 5;
constexpr int kCurrentLineOffset = 2;
constexpr qint64 kTimingStepMilliseconds = 500;
constexpr qint64 kMaximumTimingOffsetMilliseconds = 10000;
constexpr int kTimingOffsetSemanticsVersion = 2;

std::unique_ptr<QSettings> createTimingSettings() {
  const QString organization = QCoreApplication::organizationName().isEmpty()
                                   ? QStringLiteral("MediaHub")
                                   : QCoreApplication::organizationName();
  const QString application = QCoreApplication::applicationName().isEmpty()
                                  ? QStringLiteral("MediaHub")
                                  : QCoreApplication::applicationName();
  return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                     organization, application);
}

struct ResponsiveLyricFontSizes {
  int current{24};
  int context{16};
  int plain{16};
  int controls{11};
  int header{15};
  int source{11};
};

ResponsiveLyricFontSizes responsiveLyricFontSizes(const int width,
                                                  const int height) {
  constexpr int kReservedHorizontalSpace = 84;
  constexpr int kReservedVerticalSpace = 150;
  const int contentWidth = std::max(width - kReservedHorizontalSpace, 320);
  const int contentHeight = std::max(height - kReservedVerticalSpace, 220);
  const int current = std::clamp(
      qRound(std::min(contentWidth * 0.06, contentHeight * 0.105)), 24, 104);
  return {
      current,
      std::clamp(qRound(current * 0.62), 16, 64),
      std::clamp(qRound(current * 0.52), 16, 42),
      std::clamp(qRound(current * 0.30), 11, 32),
      std::clamp(qRound(current * 0.36), 15, 38),
      std::clamp(qRound(current * 0.26), 11, 28),
  };
}

QString synchronizedLabelStyle(const bool isCurrent, const int fontPixels) {
  if (isCurrent) {
    return QStringLiteral(
               "color: #cc5a36; font-size: %1px; font-weight: 700;"
               "background: transparent;")
        .arg(fontPixels);
  }
  return QStringLiteral(
             "color: rgba(23, 60, 58, 145); font-size: %1px; font-weight: 500;"
             "background: transparent;")
      .arg(fontPixels);
}

}  // namespace

LyricsView::LyricsView(QWidget* const parent) : QWidget(parent) {
  setObjectName(QStringLiteral("lyricsView"));
  setMinimumSize(320, 220);

  auto* const rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(34, 24, 34, 28);
  rootLayout->setSpacing(16);

  auto* const headerLayout = new QHBoxLayout();
  headerLayout->setSpacing(12);
  mediaNameLabel_ = new QLabel(QStringLiteral("未选择媒体"), this);
  mediaNameLabel_->setObjectName(QStringLiteral("lyricsMediaNameLabel"));
  mediaNameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  sourceLabel_ = new QLabel(this);
  sourceLabel_->setObjectName(QStringLiteral("lyricsSourceLabel"));
  sourceLabel_->hide();
  headerLayout->addWidget(mediaNameLabel_, 1);
  headerLayout->addWidget(sourceLabel_, 0, Qt::AlignTop);
  rootLayout->addLayout(headerLayout);

  timingControls_ = new QWidget(this);
  timingControls_->setObjectName(QStringLiteral("lyricsTimingControls"));
  auto* const timingLayout = new QHBoxLayout(timingControls_);
  timingLayout->setContentsMargins(8, 4, 8, 4);
  timingLayout->setSpacing(6);
  timingOffsetLabel_ = new QLabel(timingControls_);
  timingOffsetLabel_->setObjectName(QStringLiteral("lyricsTimingOffsetLabel"));
  timingSlowButton_ = new QToolButton(timingControls_);
  timingSlowButton_->setObjectName(QStringLiteral("lyricsTimingSlowButton"));
  timingSlowButton_->setText(QStringLiteral("-0.5s"));
  timingSlowButton_->setAccessibleName(QStringLiteral("歌词慢 0.5 秒"));
  timingFastButton_ = new QToolButton(timingControls_);
  timingFastButton_->setObjectName(QStringLiteral("lyricsTimingFastButton"));
  timingFastButton_->setText(QStringLiteral("+0.5s"));
  timingFastButton_->setAccessibleName(QStringLiteral("歌词快 0.5 秒"));
  timingResetButton_ = new QToolButton(timingControls_);
  timingResetButton_->setObjectName(QStringLiteral("lyricsTimingResetButton"));
  timingResetButton_->setText(QStringLiteral("重置"));
  timingResetButton_->setAccessibleName(QStringLiteral("重置歌词同步"));
  timingLayout->addWidget(timingOffsetLabel_);
  timingLayout->addWidget(timingSlowButton_);
  timingLayout->addWidget(timingFastButton_);
  timingLayout->addWidget(timingResetButton_);
  rootLayout->addWidget(timingControls_, 0, Qt::AlignRight);

  connect(timingSlowButton_, &QToolButton::clicked, this, [this] {
    setTimingOffset(timingOffsetMilliseconds_ - kTimingStepMilliseconds, true);
  });
  connect(timingFastButton_, &QToolButton::clicked, this, [this] {
    setTimingOffset(timingOffsetMilliseconds_ + kTimingStepMilliseconds, true);
  });
  connect(timingResetButton_, &QToolButton::clicked, this,
          [this] { setTimingOffset(0, true); });

  auto* const contentHost = new QWidget(this);
  contentHost->setObjectName(QStringLiteral("lyricsContentHost"));
  contentStack_ = new QStackedLayout(contentHost);
  contentStack_->setContentsMargins(0, 0, 0, 0);

  messagePage_ = new QWidget(contentHost);
  auto* const messageLayout = new QVBoxLayout(messagePage_);
  messageLayout->setContentsMargins(20, 20, 20, 20);
  messageLayout->addStretch(1);
  messageTitleLabel_ = new QLabel(messagePage_);
  messageTitleLabel_->setObjectName(QStringLiteral("lyricsMessageTitle"));
  messageTitleLabel_->setAlignment(Qt::AlignCenter);
  messageDetailLabel_ = new QLabel(messagePage_);
  messageDetailLabel_->setObjectName(QStringLiteral("lyricsMessageDetail"));
  messageDetailLabel_->setAlignment(Qt::AlignCenter);
  messageDetailLabel_->setWordWrap(true);
  messageLayout->addWidget(messageTitleLabel_);
  messageLayout->addWidget(messageDetailLabel_);
  messageLayout->addStretch(1);

  synchronizedPage_ = new QWidget(contentHost);
  auto* const synchronizedLayout = new QVBoxLayout(synchronizedPage_);
  synchronizedLayout->setContentsMargins(8, 8, 8, 8);
  synchronizedLayout->setSpacing(13);
  synchronizedLayout->addStretch(1);
  for (int index = 0; index < kVisibleLineCount; ++index) {
    auto* const label = new QLabel(synchronizedPage_);
    label->setObjectName(QStringLiteral("synchronizedLyricLine%1").arg(index));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMinimumHeight(index == kCurrentLineOffset ? 58 : 32);
    synchronizedLabels_.push_back(label);
    synchronizedLayout->addWidget(label);
  }
  synchronizedLayout->addStretch(1);

  plainPage_ = new QWidget(contentHost);
  auto* const plainLayout = new QVBoxLayout(plainPage_);
  plainLayout->setContentsMargins(0, 0, 0, 0);
  plainLyricsBrowser_ = new QTextBrowser(plainPage_);
  plainLyricsBrowser_->setObjectName(QStringLiteral("plainLyricsBrowser"));
  plainLyricsBrowser_->setFrameShape(QFrame::NoFrame);
  plainLyricsBrowser_->setOpenExternalLinks(false);
  plainLayout->addWidget(plainLyricsBrowser_);

  contentStack_->addWidget(messagePage_);
  contentStack_->addWidget(synchronizedPage_);
  contentStack_->addWidget(plainPage_);
  rootLayout->addWidget(contentHost, 1);

  setStyleSheet(QStringLiteral(R"(
      QWidget#lyricsView {
          background: qradialgradient(cx:0.5, cy:0.82, radius:1.0,
                                      fx:0.5, fy:0.82,
                                      stop:0 #e3eee7, stop:0.58 #edf1e8,
                                      stop:1 #d8e5df);
          border: 1px solid #b6c8c0;
          border-radius: 18px;
      }
      QLabel#lyricsMediaNameLabel {
          color: #174f4b;
          font-family: "Microsoft YaHei UI";
          font-size: 15px;
          font-weight: 700;
      }
      QLabel#lyricsSourceLabel {
          color: #72543f;
          background: rgba(244, 216, 201, 190);
          border: 1px solid #ddb69f;
          border-radius: 10px;
          padding: 3px 9px;
          font-size: 11px;
          font-weight: 700;
      }
      QLabel#lyricsMessageTitle {
          color: #174f4b;
          font-size: 22px;
          font-weight: 700;
      }
      QLabel#lyricsMessageDetail {
          color: #66817c;
          font-size: 13px;
          margin-top: 8px;
      }
      QWidget#lyricsTimingControls {
          background: rgba(255, 255, 255, 105);
          border: 1px solid rgba(182, 200, 192, 175);
          border-radius: 12px;
      }
      QLabel#lyricsTimingOffsetLabel {
          color: #496b66;
          border: none;
          font-size: 11px;
          font-weight: 700;
          padding: 0 4px;
      }
      QToolButton#lyricsTimingSlowButton,
      QToolButton#lyricsTimingFastButton,
      QToolButton#lyricsTimingResetButton {
          color: #245b56;
          background: #f7faf7;
          border: 1px solid #b9cbc3;
          border-radius: 9px;
          min-height: 22px;
          padding: 0 7px;
          font-size: 11px;
          font-weight: 700;
      }
      QToolButton#lyricsTimingSlowButton:hover,
      QToolButton#lyricsTimingFastButton:hover,
      QToolButton#lyricsTimingResetButton:hover {
          background: #f4d8c9;
          border-color: #d4a78e;
      }
      QTextBrowser#plainLyricsBrowser {
          color: #244f4b;
          background: rgba(255, 255, 255, 95);
          border: 1px solid rgba(182, 200, 192, 150);
          border-radius: 14px;
          padding: 18px 22px;
          font-family: "Microsoft YaHei UI";
          font-size: 16px;
          line-height: 1.6;
          selection-background-color: #f4d8c9;
      }
  )"));

  clearLyrics();
}

void LyricsView::clearLyrics() {
  synchronizedLines_.clear();
  currentLineIndex_ = -1;
  positionMilliseconds_ = 0;
  timingOffsetMilliseconds_ = 0;
  timingSettingsKey_.clear();
  timingSettingsVersionKey_.clear();
  timingControls_->hide();
  sourceLabel_->clear();
  sourceLabel_->hide();
  plainLyricsBrowser_->clear();
  showMessage(QStringLiteral("歌词尚未打开"),
              QStringLiteral("点击控制栏中的“词”开始查找"));
}

void LyricsView::showLoading() {
  sourceLabel_->clear();
  sourceLabel_->hide();
  timingControls_->hide();
  showMessage(QStringLiteral("正在查找歌词"),
              QStringLiteral("优先读取内嵌歌词，然后查询在线歌词源…"));
}

void LyricsView::setResult(const LyricsResult& result) {
  synchronizedLines_.clear();
  currentLineIndex_ = -1;
  sourceLabel_->setText(
      result.sourceName.isEmpty()
          ? QString{}
          : QStringLiteral("来源 · %1").arg(result.sourceName));
  sourceLabel_->setVisible(!sourceLabel_->text().isEmpty());
  timingControls_->hide();

  if (result.kind != LyricsResultKind::Ready) {
    const QString title = result.kind == LyricsResultKind::Error
                              ? QStringLiteral("歌词加载失败")
                              : QStringLiteral("暂未找到歌词");
    showMessage(title, result.message);
    return;
  }

  if (!result.synchronizedLines.isEmpty()) {
    synchronizedLines_ = result.synchronizedLines;
    loadTimingOffset(result);
    timingControls_->show();
    contentStack_->setCurrentWidget(synchronizedPage_);
    updateSynchronizedLines();
    return;
  }

  if (!result.plainText.trimmed().isEmpty()) {
    plainLyricsBrowser_->setPlainText(result.plainText.trimmed());
    plainLyricsBrowser_->verticalScrollBar()->setValue(0);
    contentStack_->setCurrentWidget(plainPage_);
    return;
  }

  showMessage(QStringLiteral("暂未找到歌词"),
              QStringLiteral("当前歌曲只有元数据，没有可显示的歌词"));
}

void LyricsView::setMediaName(const QString& mediaName) {
  mediaNameLabel_->setText(mediaName);
}

void LyricsView::setPosition(const qint64 positionMilliseconds) {
  positionMilliseconds_ = std::max<qint64>(positionMilliseconds, 0);
  if (!synchronizedLines_.isEmpty()) {
    updateSynchronizedLines();
  }
}

qint64 LyricsView::timingOffsetMilliseconds() const noexcept {
  return timingOffsetMilliseconds_;
}

void LyricsView::resizeEvent(QResizeEvent* const event) {
  QWidget::resizeEvent(event);
  updateSynchronizedStyles();
}

void LyricsView::showMessage(const QString& title, const QString& detail) {
  messageTitleLabel_->setText(title);
  messageDetailLabel_->setText(detail);
  contentStack_->setCurrentWidget(messagePage_);
}

void LyricsView::updateSynchronizedLines() {
  if (synchronizedLines_.isEmpty()) {
    return;
  }

  const qint64 adjustedPosition =
      std::max<qint64>(positionMilliseconds_ + timingOffsetMilliseconds_, 0);
  const auto nextLine = std::upper_bound(
      synchronizedLines_.cbegin(), synchronizedLines_.cend(), adjustedPosition,
      [](const qint64 position, const LyricLine& line) {
        return position < line.timeMilliseconds;
      });
  const int newIndex =
      nextLine == synchronizedLines_.cbegin()
          ? -1
          : static_cast<int>(nextLine - synchronizedLines_.cbegin()) - 1;
  if (newIndex == currentLineIndex_ &&
      !synchronizedLabels_[kCurrentLineOffset]->text().isEmpty()) {
    return;
  }
  currentLineIndex_ = newIndex;

  for (int labelIndex = 0; labelIndex < synchronizedLabels_.size();
       ++labelIndex) {
    const int lineIndex = currentLineIndex_ + labelIndex - kCurrentLineOffset;
    QLabel* const label = synchronizedLabels_[labelIndex];
    if (lineIndex < 0 || lineIndex >= synchronizedLines_.size()) {
      label->clear();
      continue;
    }
    label->setText(synchronizedLines_[lineIndex].text);
  }
  updateSynchronizedStyles();
}

void LyricsView::loadTimingOffset(const LyricsResult& result) {
  const QByteArray identity =
      QStringLiteral("%1|%2|%3")
          .arg(mediaNameLabel_->text(), result.title, result.artist)
          .toUtf8();
  const QString identityHash = QString::fromLatin1(
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
  timingSettingsKey_ =
      QStringLiteral("lyrics/timingOffsets/%1").arg(identityHash);
  timingSettingsVersionKey_ =
      QStringLiteral("lyrics/timingOffsetVersions/%1").arg(identityHash);
  const auto settings = createTimingSettings();
  qint64 savedOffset = settings->value(timingSettingsKey_, 0).toLongLong();
  if (settings->contains(timingSettingsKey_) &&
      settings->value(timingSettingsVersionKey_, 1).toInt() <
          kTimingOffsetSemanticsVersion) {
    savedOffset = -savedOffset;
    settings->setValue(timingSettingsKey_, savedOffset);
    settings->setValue(timingSettingsVersionKey_,
                       kTimingOffsetSemanticsVersion);
    settings->sync();
  }
  setTimingOffset(savedOffset, false);
}

void LyricsView::setTimingOffset(const qint64 offsetMilliseconds,
                                 const bool persist) {
  timingOffsetMilliseconds_ =
      std::clamp(offsetMilliseconds, -kMaximumTimingOffsetMilliseconds,
                 kMaximumTimingOffsetMilliseconds);
  if (persist && !timingSettingsKey_.isEmpty()) {
    const auto settings = createTimingSettings();
    if (timingOffsetMilliseconds_ == 0) {
      settings->remove(timingSettingsKey_);
      settings->remove(timingSettingsVersionKey_);
    } else {
      settings->setValue(timingSettingsKey_, timingOffsetMilliseconds_);
      settings->setValue(timingSettingsVersionKey_,
                         kTimingOffsetSemanticsVersion);
    }
    settings->sync();
  }
  updateTimingOffsetLabel();
  currentLineIndex_ = std::numeric_limits<int>::min();
  if (!synchronizedLines_.isEmpty()) {
    updateSynchronizedLines();
  }
}

void LyricsView::updateTimingOffsetLabel() {
  if (timingOffsetMilliseconds_ == 0) {
    timingOffsetLabel_->setText(QStringLiteral("歌词同步 0.0 秒"));
    timingResetButton_->setEnabled(false);
    return;
  }
  const QString direction = timingOffsetMilliseconds_ > 0
                                ? QStringLiteral("歌词快")
                                : QStringLiteral("歌词慢");
  timingOffsetLabel_->setText(
      QStringLiteral("%1 %2 秒")
          .arg(direction)
          .arg(std::abs(timingOffsetMilliseconds_) / 1000.0, 0, 'f', 1));
  timingResetButton_->setEnabled(true);
}

void LyricsView::updateSynchronizedStyles() {
  const ResponsiveLyricFontSizes fontSizes =
      responsiveLyricFontSizes(width(), height());
  for (int index = 0; index < synchronizedLabels_.size(); ++index) {
    const bool isCurrent = index == kCurrentLineOffset;
    const int fontPixels = isCurrent ? fontSizes.current : fontSizes.context;
    synchronizedLabels_[index]->setMinimumHeight(qRound(fontPixels * 1.35));
    synchronizedLabels_[index]->setStyleSheet(
        synchronizedLabelStyle(isCurrent, fontPixels));
  }
  plainLyricsBrowser_->setStyleSheet(
      QStringLiteral("font-size: %1px;").arg(fontSizes.plain));

  mediaNameLabel_->setStyleSheet(
      QStringLiteral("font-size: %1px;").arg(fontSizes.header));
  mediaNameLabel_->setMinimumHeight(qRound(fontSizes.header * 1.45));
  sourceLabel_->setStyleSheet(
      QStringLiteral("font-size: %1px; padding: %2px %3px;")
          .arg(fontSizes.source)
          .arg(std::max(3, qRound(fontSizes.source * 0.27)))
          .arg(std::max(9, qRound(fontSizes.source * 0.72))));
  sourceLabel_->setMinimumHeight(qRound(fontSizes.source * 1.7));

  timingOffsetLabel_->setStyleSheet(
      QStringLiteral("font-size: %1px;").arg(fontSizes.controls));
  const int timingButtonHeight = std::max(22, qRound(fontSizes.controls * 1.9));
  const QString timingButtonStyle =
      QStringLiteral("font-size: %1px; padding: 0 %2px;")
          .arg(fontSizes.controls)
          .arg(std::max(7, qRound(fontSizes.controls * 0.55)));
  timingSlowButton_->setStyleSheet(timingButtonStyle);
  timingFastButton_->setStyleSheet(timingButtonStyle);
  timingResetButton_->setStyleSheet(timingButtonStyle);
  timingSlowButton_->setMinimumHeight(timingButtonHeight);
  timingFastButton_->setMinimumHeight(timingButtonHeight);
  timingResetButton_->setMinimumHeight(timingButtonHeight);
}

}  // namespace mediahub::gui
