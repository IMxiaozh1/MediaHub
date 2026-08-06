#include "main_window.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedLayout>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <array>
#include <initializer_list>

#include "lyrics_view.h"
#include "playlist_model.h"
#include "seek_slider.h"
#include "video_output_widget.h"

namespace mediahub::gui {
namespace {

constexpr int kNormalHorizontalMargin = 36;
constexpr int kNormalVerticalMargin = 28;
constexpr int kNormalSpacing = 16;
constexpr int kKeyboardVolumeStep = 5;
constexpr int kDefaultKeyboardSeekStepSeconds = 5;
constexpr int kRightKeyHoldThresholdMilliseconds = 350;
constexpr std::array<double, 6> kPlaybackRates{0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
constexpr std::array<int, 4> kSeekSteps{5, 10, 15, 20};

enum class ControlIcon {
  Previous,
  Next,
  Play,
  Pause,
  Stop,
  Refresh,
  Volume,
  Muted,
  FullScreen,
  ExitFullScreen,
  Sequential,
  LoopAll,
  LoopOne,
  Shuffle,
  CollapseRight,
  ExpandLeft,
};

QPolygonF makePolygon(const std::initializer_list<QPointF> points) {
  QPolygonF polygon;
  for (const QPointF& point : points) {
    polygon << point;
  }
  return polygon;
}

QIcon controlIcon(const ControlIcon icon) {
  QPixmap pixmap(20, 20);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor ink(QStringLiteral("#174f4b"));
  QPen pen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  const auto drawChevron = [&painter, &pen](const bool pointsRight) {
    QPainterPath path;
    path.moveTo(pointsRight ? 7 : 13, 5);
    path.lineTo(pointsRight ? 13 : 7, 10);
    path.lineTo(pointsRight ? 7 : 13, 15);
    painter.setPen(pen);
    painter.drawPath(path);
  };

  switch (icon) {
    case ControlIcon::Previous:
      painter.drawLine(QPointF(5, 4), QPointF(5, 16));
      painter.setBrush(ink);
      painter.drawPolygon(
          makePolygon({QPointF(15, 4), QPointF(7, 10), QPointF(15, 16)}));
      break;
    case ControlIcon::Next:
      painter.drawLine(QPointF(15, 4), QPointF(15, 16));
      painter.setBrush(ink);
      painter.drawPolygon(
          makePolygon({QPointF(5, 4), QPointF(13, 10), QPointF(5, 16)}));
      break;
    case ControlIcon::Play:
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawPolygon(
          makePolygon({QPointF(6, 4), QPointF(16, 10), QPointF(6, 16)}));
      break;
    case ControlIcon::Pause:
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawRoundedRect(QRectF(5, 4, 3.5, 12), 1, 1);
      painter.drawRoundedRect(QRectF(11.5, 4, 3.5, 12), 1, 1);
      break;
    case ControlIcon::Stop:
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawRoundedRect(QRectF(5, 5, 10, 10), 1.5, 1.5);
      break;
    case ControlIcon::Refresh:
      painter.drawArc(QRectF(3.5, 3.5, 13, 13), 35 * 16, 285 * 16);
      painter.drawLine(QPointF(15.5, 3.5), QPointF(15.5, 8));
      painter.drawLine(QPointF(15.5, 3.5), QPointF(11, 3.5));
      break;
    case ControlIcon::Volume:
    case ControlIcon::Muted: {
      painter.setBrush(ink);
      painter.drawPolygon(
          makePolygon({QPointF(3, 8), QPointF(7, 8), QPointF(11, 4),
                       QPointF(11, 16), QPointF(7, 12), QPointF(3, 12)}));
      painter.setBrush(Qt::NoBrush);
      if (icon == ControlIcon::Volume) {
        painter.drawArc(QRectF(8, 6, 7, 8), -55 * 16, 110 * 16);
        painter.drawArc(QRectF(8, 3, 11, 14), -50 * 16, 100 * 16);
      } else {
        painter.drawLine(QPointF(13, 7), QPointF(18, 12));
        painter.drawLine(QPointF(18, 7), QPointF(13, 12));
      }
      break;
    }
    case ControlIcon::FullScreen:
    case ControlIcon::ExitFullScreen: {
      const bool inward = icon == ControlIcon::ExitFullScreen;
      const qreal outer = inward ? 4 : 3;
      const qreal inner = inward ? 8 : 7;
      painter.drawLine(QPointF(outer, inner), QPointF(inner, inner));
      painter.drawLine(QPointF(inner, inner), QPointF(inner, outer));
      painter.drawLine(QPointF(20 - outer, inner), QPointF(20 - inner, inner));
      painter.drawLine(QPointF(20 - inner, inner), QPointF(20 - inner, outer));
      painter.drawLine(QPointF(outer, 20 - inner), QPointF(inner, 20 - inner));
      painter.drawLine(QPointF(inner, 20 - inner), QPointF(inner, 20 - outer));
      painter.drawLine(QPointF(20 - outer, 20 - inner),
                       QPointF(20 - inner, 20 - inner));
      painter.drawLine(QPointF(20 - inner, 20 - inner),
                       QPointF(20 - inner, 20 - outer));
      break;
    }
    case ControlIcon::Sequential:
      painter.drawLine(QPointF(3, 6), QPointF(15, 6));
      painter.drawLine(QPointF(3, 10), QPointF(15, 10));
      painter.drawLine(QPointF(3, 14), QPointF(15, 14));
      painter.drawLine(QPointF(13, 3), QPointF(17, 6));
      painter.drawLine(QPointF(17, 6), QPointF(13, 9));
      break;
    case ControlIcon::LoopAll:
    case ControlIcon::LoopOne:
      painter.drawLine(QPointF(5, 5), QPointF(16, 5));
      painter.drawLine(QPointF(16, 5), QPointF(13, 2));
      painter.drawLine(QPointF(16, 5), QPointF(13, 8));
      painter.drawLine(QPointF(15, 15), QPointF(4, 15));
      painter.drawLine(QPointF(4, 15), QPointF(7, 12));
      painter.drawLine(QPointF(4, 15), QPointF(7, 18));
      if (icon == ControlIcon::LoopOne) {
        QFont numberFont = painter.font();
        numberFont.setBold(true);
        numberFont.setPixelSize(8);
        painter.setFont(numberFont);
        painter.drawText(QRectF(7, 6, 6, 8), Qt::AlignCenter,
                         QStringLiteral("1"));
      }
      break;
    case ControlIcon::Shuffle: {
      QPainterPath upperPath;
      upperPath.moveTo(3, 5);
      upperPath.cubicTo(9, 5, 10, 15, 16, 15);
      painter.drawPath(upperPath);
      QPainterPath lowerPath;
      lowerPath.moveTo(3, 15);
      lowerPath.cubicTo(9, 15, 10, 5, 16, 5);
      painter.drawPath(lowerPath);
      painter.drawLine(QPointF(13, 2), QPointF(17, 5));
      painter.drawLine(QPointF(17, 5), QPointF(13, 8));
      painter.drawLine(QPointF(13, 12), QPointF(17, 15));
      painter.drawLine(QPointF(17, 15), QPointF(13, 18));
      break;
    }
    case ControlIcon::CollapseRight:
      drawChevron(true);
      break;
    case ControlIcon::ExpandLeft:
      drawChevron(false);
      break;
  }
  return QIcon(pixmap);
}

void configureTransportButton(QToolButton* const button,
                              const QString& accessibleName,
                              const QString& toolTip, const ControlIcon icon) {
  button->setAccessibleName(accessibleName);
  button->setToolTip(toolTip);
  button->setProperty("transportControl", true);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setIconSize(QSize(20, 20));
  button->setIcon(controlIcon(icon));
  button->setFixedSize(36, 36);
}

QString playbackModeName(const int modeIndex) {
  switch (modeIndex) {
    case 1:
      return QStringLiteral("列表循环");
    case 2:
      return QStringLiteral("单曲循环");
    case 3:
      return QStringLiteral("随机播放");
    default:
      return QStringLiteral("顺序播放");
  }
}

ControlIcon playbackModeIcon(const int modeIndex) {
  switch (modeIndex) {
    case 1:
      return ControlIcon::LoopAll;
    case 2:
      return ControlIcon::LoopOne;
    case 3:
      return ControlIcon::Shuffle;
    default:
      return ControlIcon::Sequential;
  }
}

class HoverOptionButton final : public QToolButton {
 public:
  explicit HoverOptionButton(QWidget* const parent = nullptr)
      : QToolButton(parent),
        openTimer_(this),
        closeTimer_(this),
        hoverMonitorTimer_(this) {
    openTimer_.setSingleShot(true);
    openTimer_.setInterval(140);
    closeTimer_.setSingleShot(true);
    closeTimer_.setInterval(120);
    hoverMonitorTimer_.setInterval(40);
    connect(&openTimer_, &QTimer::timeout, this, [this] {
      if (!isEnabled() || !containsCursor() || hoverPopup_ == nullptr ||
          hoverPopup_->isVisible()) {
        return;
      }
      showPopupAbove();
    });
    connect(&closeTimer_, &QTimer::timeout, this, [this] {
      if (hoverPopup_ != nullptr && !containsCursor()) {
        hoverPopup_->hide();
      }
    });
    connect(&hoverMonitorTimer_, &QTimer::timeout, this, [this] {
      if (hoverPopup_ == nullptr || !hoverPopup_->isVisible()) {
        hoverMonitorTimer_.stop();
        return;
      }
      positionPopupAbove();
      if (containsCursor()) {
        closeTimer_.stop();
      } else if (!closeTimer_.isActive()) {
        closeTimer_.start();
      }
    });
    connect(this, &QToolButton::clicked, this, [this] {
      if (clickOpensMenu_) {
        showPopupAbove();
      }
    });
  }

  void setHoverMenu(QMenu* const optionMenu) {
    optionMenu->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                               Qt::NoDropShadowWindowHint);
    optionMenu->setAttribute(Qt::WA_ShowWithoutActivating);
    setMenu(optionMenu);
    setHoverPopup(optionMenu);
    connect(optionMenu, &QMenu::aboutToHide, this,
            [this] { hoverMonitorTimer_.stop(); });
    connect(optionMenu, &QMenu::triggered, optionMenu,
            [optionMenu] { optionMenu->hide(); });
  }

  void setHoverPopup(QWidget* const popup) {
    hoverPopup_ = popup;
    popup->installEventFilter(this);
  }

  void setClickOpensMenu(const bool enabled) { clickOpensMenu_ = enabled; }

  void setCenteredPopupPlacement(const int gap, const int horizontalOffset) {
    centersPopup_ = true;
    popupGap_ = gap;
    popupHorizontalOffset_ = horizontalOffset;
  }

  void setCloseDelay(const int milliseconds) {
    closeTimer_.setInterval(std::max(milliseconds, 0));
  }

 protected:
  void enterEvent(QEvent* const event) override {
    pointerInsideButton_ = true;
    QToolButton::enterEvent(event);
    closeTimer_.stop();
    openTimer_.start();
  }

  void leaveEvent(QEvent* const event) override {
    pointerInsideButton_ = false;
    QToolButton::leaveEvent(event);
    openTimer_.stop();
    closeTimer_.start();
  }

  bool eventFilter(QObject* const watched, QEvent* const event) override {
    if (watched == hoverPopup_) {
      if (event->type() == QEvent::Enter) {
        pointerInsidePopup_ = true;
        closeTimer_.stop();
      } else if (event->type() == QEvent::Leave) {
        pointerInsidePopup_ = false;
        closeTimer_.start();
      } else if (event->type() == QEvent::Hide) {
        pointerInsidePopup_ = false;
      }
    }
    return QToolButton::eventFilter(watched, event);
  }

  void mousePressEvent(QMouseEvent* const event) override {
    if (event->button() == Qt::LeftButton) {
      openTimer_.stop();
      QAbstractButton::mousePressEvent(event);
      return;
    }
    QToolButton::mousePressEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* const event) override {
    if (event->button() == Qt::LeftButton) {
      QAbstractButton::mouseReleaseEvent(event);
      return;
    }
    QToolButton::mouseReleaseEvent(event);
  }

 private:
  void showPopupAbove() {
    if (!isEnabled() || hoverPopup_ == nullptr || hoverPopup_->isVisible()) {
      return;
    }
    closeTimer_.stop();
    positionPopupAbove();
    hoverPopup_->show();
    hoverPopup_->raise();
    hoverMonitorTimer_.start();
  }

  void positionPopupAbove() {
    if (hoverPopup_ == nullptr) {
      return;
    }
    hoverPopup_->ensurePolished();
    hoverPopup_->adjustSize();
    const QSize popupSize = hoverPopup_->size();
    const QPoint buttonTopLeft = mapToGlobal(QPoint(0, 0));
    const int popupX =
        centersPopup_ ? buttonTopLeft.x() + (width() - popupSize.width()) / 2 +
                            popupHorizontalOffset_
                      : buttonTopLeft.x() + width() - popupSize.width();
    const QPoint popupPosition(
        popupX, buttonTopLeft.y() - popupSize.height() - popupGap_);
    if (hoverPopup_->pos() != popupPosition) {
      hoverPopup_->move(popupPosition);
    }
  }

  [[nodiscard]] bool containsCursor() const {
    return pointerInsideButton_ ||
           (hoverPopup_ != nullptr && hoverPopup_->isVisible() &&
            pointerInsidePopup_);
  }

  QWidget* hoverPopup_{nullptr};
  QTimer openTimer_;
  QTimer closeTimer_;
  QTimer hoverMonitorTimer_;
  bool clickOpensMenu_{false};
  bool centersPopup_{false};
  int popupGap_{6};
  int popupHorizontalOffset_{0};
  bool pointerInsideButton_{false};
  bool pointerInsidePopup_{false};
};

QString playbackRateText(const double rate) {
  if (qFuzzyCompare(rate + 1.0, 1.75)) {
    return QStringLiteral("0.75×");
  }
  return QStringLiteral("%1×").arg(rate, 0, 'f', 1);
}

void selectPlaybackRate(QToolButton* const button, const double rate) {
  button->setText(playbackRateText(rate));
  const auto actions = button->menu()->findChildren<QAction*>();
  for (auto* const action : actions) {
    if (action->isCheckable()) {
      action->setChecked(
          qFuzzyCompare(action->data().toDouble() + 1.0, rate + 1.0));
    }
  }
}

void selectKeyboardSeekStep(QToolButton* const button, const int seconds) {
  button->setText(QStringLiteral("%1 秒").arg(seconds));
  const auto actions = button->menu()->findChildren<QAction*>();
  for (auto* const action : actions) {
    if (action->isCheckable()) {
      action->setChecked(action->data().toInt() == seconds);
    }
  }
}

void selectPlaybackMode(QToolButton* const button, const int modeIndex) {
  button->setProperty("playbackModeIndex", modeIndex);
  button->setIcon(controlIcon(playbackModeIcon(modeIndex)));
  const QString modeName = playbackModeName(modeIndex);
  button->setAccessibleName(QStringLiteral("播放模式：%1").arg(modeName));
  button->setToolTip(
      QStringLiteral("当前为%1；悬停选择，点击切换下一种").arg(modeName));
  const auto actions = button->menu()->findChildren<QAction*>();
  for (auto* const action : actions) {
    if (action->isCheckable()) {
      action->setChecked(action->data().toInt() == modeIndex);
    }
  }
}

QStringList localFilePaths(const QMimeData* const mimeData) {
  QStringList paths;
  if (mimeData == nullptr || !mimeData->hasUrls()) {
    return paths;
  }
  for (const auto& url : mimeData->urls()) {
    if (url.isLocalFile()) {
      const QString path = url.toLocalFile();
      if (!path.isEmpty()) {
        paths.push_back(path);
      }
    }
  }
  return paths;
}

}  // namespace

MainWindow::MainWindow(QWidget* const parent)
    : QMainWindow(parent), windowIconManager_(this) {
  setWindowTitle(QStringLiteral("MediaHub"));
  windowIconManager_.apply();
  resize(960, 720);
  setMinimumSize(760, 640);
  setAcceptDrops(true);
  qApp->installEventFilter(this);
  rightKeyHoldTimer_ = new QTimer(this);
  rightKeyHoldTimer_->setObjectName(QStringLiteral("rightKeyHoldTimer"));
  rightKeyHoldTimer_->setSingleShot(true);
  rightKeyHoldTimer_->setInterval(kRightKeyHoldThresholdMilliseconds);
  connect(rightKeyHoldTimer_, &QTimer::timeout, this, [this] {
    if (isRightKeyPressed_) {
      isRightKeyHoldActive_ = true;
      emit temporaryFastPlaybackRequested(true);
    }
  });

  auto* const fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
  openAction_ = fileMenu->addAction(QStringLiteral("打开媒体文件(&O)..."));
  openAction_->setObjectName(QStringLiteral("openFileAction"));
  openAction_->setShortcut(QKeySequence::Open);
  openNetworkAction_ =
      fileMenu->addAction(QStringLiteral("打开网络地址(&N)..."));
  openNetworkAction_->setObjectName(QStringLiteral("openNetworkAction"));
  openNetworkAction_->setShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::Key_L)));
  fileMenu->addSeparator();
  auto* const exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
  exitAction->setShortcut(QKeySequence::Quit);
  auto* const viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
  fullScreenAction_ = viewMenu->addAction(QStringLiteral("进入全屏(&F)"));
  fullScreenAction_->setObjectName(QStringLiteral("fullScreenAction"));
  fullScreenAction_->setShortcut(QKeySequence(Qt::Key_F11));
  auto* const exitFullScreenAction = new QAction(this);
  exitFullScreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(exitFullScreenAction);

  auto* const playbackShortcut =
      new QShortcut(QKeySequence(Qt::Key_Space), this);
  playbackShortcut->setObjectName(QStringLiteral("playbackToggleShortcut"));
  playbackShortcut->setContext(Qt::WindowShortcut);
  auto* const networkRefreshShortcut =
      new QShortcut(QKeySequence(Qt::Key_F5), this);
  networkRefreshShortcut->setObjectName(
      QStringLiteral("networkRefreshShortcut"));
  networkRefreshShortcut->setContext(Qt::WindowShortcut);
  networkRefreshShortcut->setAutoRepeat(false);
  auto* const volumeUpShortcut = new QShortcut(QKeySequence(Qt::Key_Up), this);
  volumeUpShortcut->setObjectName(QStringLiteral("volumeUpShortcut"));
  volumeUpShortcut->setContext(Qt::WindowShortcut);
  auto* const volumeDownShortcut =
      new QShortcut(QKeySequence(Qt::Key_Down), this);
  volumeDownShortcut->setObjectName(QStringLiteral("volumeDownShortcut"));
  volumeDownShortcut->setContext(Qt::WindowShortcut);
  auto* const seekBackwardShortcut =
      new QShortcut(QKeySequence(Qt::Key_Left), this);
  seekBackwardShortcut->setObjectName(QStringLiteral("seekBackwardShortcut"));
  seekBackwardShortcut->setContext(Qt::WindowShortcut);
  auto* const previousShortcut = new QShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::Key_Left)),
      this);
  previousShortcut->setObjectName(QStringLiteral("previousShortcut"));
  previousShortcut->setContext(Qt::WindowShortcut);
  auto* const nextShortcut =
      new QShortcut(QKeySequence(static_cast<int>(Qt::CTRL) |
                                 static_cast<int>(Qt::Key_Right)),
                    this);
  nextShortcut->setObjectName(QStringLiteral("nextShortcut"));
  nextShortcut->setContext(Qt::WindowShortcut);
  auto* const centralWidget = new QWidget(this);
  centralWidget->setObjectName(QStringLiteral("centralSurface"));
  rootLayout_ = new QVBoxLayout(centralWidget);
  rootLayout_->setContentsMargins(
      kNormalHorizontalMargin, kNormalVerticalMargin, kNormalHorizontalMargin,
      kNormalVerticalMargin);
  rootLayout_->setSpacing(kNormalSpacing);

  auto* const eyebrow =
      new QLabel(QStringLiteral("LOCAL MEDIA / 01"), centralWidget);
  eyebrow->setObjectName(QStringLiteral("eyebrowLabel"));
  auto* const title =
      new QLabel(QStringLiteral("让本地声音重新流动"), centralWidget);
  title->setObjectName(QStringLiteral("titleLabel"));
  auto* const subtitle =
      new QLabel(QStringLiteral("打开本地音视频，声音与画面都留在你的设备上。"),
                 centralWidget);
  subtitle->setObjectName(QStringLiteral("subtitleLabel"));
  rootLayout_->addWidget(eyebrow);
  rootLayout_->addWidget(title);
  rootLayout_->addWidget(subtitle);

  auto* const mediaWorkspace = new QHBoxLayout();
  mediaWorkspace->setSpacing(10);
  auto* const mediaDisplay = new QWidget(centralWidget);
  mediaDisplay->setObjectName(QStringLiteral("mediaDisplay"));
  mediaDisplayStack_ = new QStackedLayout(mediaDisplay);
  mediaDisplayStack_->setContentsMargins(0, 0, 0, 0);
  videoOutput_ = new VideoOutputWidget(mediaDisplay);
  lyricsView_ = new LyricsView(mediaDisplay);
  mediaDisplayStack_->addWidget(videoOutput_);
  mediaDisplayStack_->addWidget(lyricsView_);
  mediaDisplayStack_->setCurrentWidget(videoOutput_);
  mediaWorkspace->addWidget(mediaDisplay, 3);

  playlistToggleButton_ = new QToolButton(centralWidget);
  playlistToggleButton_->setObjectName(QStringLiteral("playlistToggleButton"));
  playlistToggleButton_->setProperty("playlistToggle", true);
  configureTransportButton(
      playlistToggleButton_, QStringLiteral("收起播放列表"),
      QStringLiteral("收起播放列表"), ControlIcon::CollapseRight);
  playlistToggleButton_->setFixedSize(28, 52);
  mediaWorkspace->addWidget(playlistToggleButton_, 0, Qt::AlignVCenter);

  playlistPanel_ = new QFrame(centralWidget);
  playlistPanel_->setObjectName(QStringLiteral("playlistPanel"));
  playlistPanel_->setMinimumWidth(220);
  playlistPanel_->setMaximumWidth(320);
  auto* const playlistLayout = new QVBoxLayout(playlistPanel_);
  playlistLayout->setContentsMargins(16, 14, 16, 14);
  playlistLayout->setSpacing(10);
  auto* const playlistTitle =
      new QLabel(QStringLiteral("播放列表"), playlistPanel_);
  playlistTitle->setObjectName(QStringLiteral("playlistTitleLabel"));
  playlistKindTabs_ = new QTabBar(playlistPanel_);
  playlistKindTabs_->setObjectName(QStringLiteral("playlistKindTabs"));
  playlistKindTabs_->setAccessibleName(QStringLiteral("播放列表类型"));
  playlistKindTabs_->setDocumentMode(true);
  playlistKindTabs_->setExpanding(true);
  playlistKindTabs_->addTab(QStringLiteral("本地播放列表"));
  playlistKindTabs_->addTab(QStringLiteral("直播列表"));
  livePlaylistUrlEdit_ = new QLineEdit(playlistPanel_);
  livePlaylistUrlEdit_->setObjectName(QStringLiteral("livePlaylistUrlEdit"));
  livePlaylistUrlEdit_->setAccessibleName(QStringLiteral("远程直播清单 URL"));
  livePlaylistUrlEdit_->setPlaceholderText(
      QStringLiteral("https://example.com/list.m3u"));
  livePlaylistUrlEdit_->setClearButtonEnabled(true);
  livePlaylistLoadButton_ =
      new QPushButton(QStringLiteral("载入 / 刷新清单"), playlistPanel_);
  livePlaylistLoadButton_->setObjectName(
      QStringLiteral("livePlaylistLoadButton"));
  livePlaylistLoadButton_->setProperty("primary", true);
  livePlaylistLocateButton_ =
      new QPushButton(QStringLiteral("定位正在播放"), playlistPanel_);
  livePlaylistLocateButton_->setObjectName(
      QStringLiteral("livePlaylistLocateButton"));
  livePlaylistLocateButton_->setToolTip(
      QStringLiteral("滚动并选中当前正在播放的直播源"));
  livePlaylistStatusLabel_ =
      new QLabel(QStringLiteral("输入远程 M3U/M3U8 清单 URL"), playlistPanel_);
  livePlaylistStatusLabel_->setObjectName(
      QStringLiteral("livePlaylistStatusLabel"));
  livePlaylistStatusLabel_->setWordWrap(true);
  playlistView_ = new QListView(playlistPanel_);
  playlistView_->setObjectName(QStringLiteral("playlistView"));
  playlistView_->setAccessibleName(QStringLiteral("播放列表"));
  playlistView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  playlistView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  playlistView_->setContextMenuPolicy(Qt::CustomContextMenu);
  playlistView_->viewport()->installEventFilter(this);
  openButton_ = new QPushButton(QStringLiteral("打开文件"), playlistPanel_);
  openButton_->setObjectName(QStringLiteral("openFileButton"));
  openButton_->setProperty("primary", true);
  playlistContextMenu_ = new QMenu(this);
  playlistContextMenu_->setObjectName(QStringLiteral("playlistContextMenu"));
  playlistPlayAction_ = playlistContextMenu_->addAction(QStringLiteral("播放"));
  playlistPlayAction_->setObjectName(QStringLiteral("playlistPlayAction"));
  playlistPauseAction_ =
      playlistContextMenu_->addAction(QStringLiteral("暂停"));
  playlistPauseAction_->setObjectName(QStringLiteral("playlistPauseAction"));
  playlistStopAction_ = playlistContextMenu_->addAction(QStringLiteral("停止"));
  playlistStopAction_->setObjectName(QStringLiteral("playlistStopAction"));
  playlistContextMenu_->addSeparator();
  playlistRenameAction_ =
      playlistContextMenu_->addAction(QStringLiteral("重命名此文件"));
  playlistRenameAction_->setObjectName(QStringLiteral("playlistRenameAction"));
  playlistRenameAction_->setStatusTip(
      QStringLiteral("只修改播放列表显示名，不会更改电脑中的文件名"));
  playlistContextMenu_->addSeparator();
  playlistMoveTopAction_ =
      playlistContextMenu_->addAction(QStringLiteral("置顶"));
  playlistMoveTopAction_->setObjectName(
      QStringLiteral("playlistMoveTopAction"));
  playlistMoveUpAction_ =
      playlistContextMenu_->addAction(QStringLiteral("向上移动"));
  playlistMoveUpAction_->setObjectName(QStringLiteral("playlistMoveUpAction"));
  playlistMoveDownAction_ =
      playlistContextMenu_->addAction(QStringLiteral("向下移动"));
  playlistMoveDownAction_->setObjectName(
      QStringLiteral("playlistMoveDownAction"));
  playlistContextMenu_->addSeparator();
  playlistRemoveAction_ = playlistContextMenu_->addAction(
      QStringLiteral("删除此文件（仅移出列表）"));
  playlistRemoveAction_->setObjectName(QStringLiteral("playlistRemoveAction"));
  livePlaylistContextMenu_ = new QMenu(this);
  livePlaylistContextMenu_->setObjectName(
      QStringLiteral("livePlaylistContextMenu"));
  livePlaylistPlaybackAction_ =
      livePlaylistContextMenu_->addAction(QStringLiteral("播放"));
  livePlaylistPlaybackAction_->setObjectName(
      QStringLiteral("livePlaylistPlaybackAction"));
  livePlaylistStopAction_ =
      livePlaylistContextMenu_->addAction(QStringLiteral("停止"));
  livePlaylistStopAction_->setObjectName(
      QStringLiteral("livePlaylistStopAction"));
  livePlaylistMarkAction_ =
      livePlaylistContextMenu_->addAction(QStringLiteral("标记"));
  livePlaylistMarkAction_->setObjectName(
      QStringLiteral("livePlaylistMarkAction"));
  livePlaylistFavoriteAction_ =
      livePlaylistContextMenu_->addAction(QStringLiteral("收藏"));
  livePlaylistFavoriteAction_->setObjectName(
      QStringLiteral("livePlaylistFavoriteAction"));
  playlistLayout->addWidget(playlistTitle);
  playlistLayout->addWidget(playlistKindTabs_);
  playlistLayout->addWidget(livePlaylistUrlEdit_);
  playlistLayout->addWidget(livePlaylistLoadButton_);
  playlistLayout->addWidget(livePlaylistLocateButton_);
  playlistLayout->addWidget(livePlaylistStatusLabel_);
  playlistLayout->addWidget(playlistView_, 1);
  playlistLayout->addWidget(openButton_);
  mediaWorkspace->addWidget(playlistPanel_, 1);
  rootLayout_->addLayout(mediaWorkspace, 1);

  auto* const mediaCard = new QFrame(centralWidget);
  mediaCard->setObjectName(QStringLiteral("mediaCard"));
  auto* const cardLayout = new QVBoxLayout(mediaCard);
  cardLayout->setContentsMargins(28, 24, 28, 24);
  cardLayout->setSpacing(14);

  auto* const mediaCaption = new QLabel(QStringLiteral("当前媒体"), mediaCard);
  mediaCaption->setObjectName(QStringLiteral("captionLabel"));
  mediaNameLabel_ = new QLabel(QStringLiteral("未选择媒体"), mediaCard);
  mediaNameLabel_->setObjectName(QStringLiteral("currentMediaLabel"));
  mediaNameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mediaNameLabel_->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);

  statusLabel_ = new QLabel(QStringLiteral("未打开媒体"), mediaCard);
  statusLabel_->setObjectName(QStringLiteral("playbackStatusLabel"));
  statusLabel_->setAlignment(Qt::AlignCenter);

  auto* const mediaRow = new QHBoxLayout();
  mediaRow->setSpacing(18);
  mediaRow->addWidget(mediaNameLabel_, 1);
  mediaRow->addWidget(statusLabel_);
  cardLayout->addWidget(mediaCaption);
  cardLayout->addLayout(mediaRow);

  errorLabel_ = new QLabel(mediaCard);
  errorLabel_->setObjectName(QStringLiteral("playbackErrorLabel"));
  errorLabel_->setWordWrap(true);
  errorLabel_->hide();
  cardLayout->addWidget(errorLabel_);
  rootLayout_->addWidget(mediaCard);

  auto* const transportPanel = new QFrame(centralWidget);
  transportPanel->setObjectName(QStringLiteral("transportPanel"));
  auto* const transportLayout = new QVBoxLayout(transportPanel);
  transportLayout->setContentsMargins(20, 14, 20, 14);
  transportLayout->setSpacing(10);

  auto* const timelineHeader = new QHBoxLayout();
  auto* const progressCaption =
      new QLabel(QStringLiteral("播放进度"), transportPanel);
  progressCaption->setObjectName(QStringLiteral("transportCaptionLabel"));
  positionLabel_ = new QLabel(QStringLiteral("00:00 / --:--"), transportPanel);
  positionLabel_->setObjectName(QStringLiteral("positionLabel"));
  timelineHeader->addWidget(progressCaption);
  timelineHeader->addStretch(1);
  timelineHeader->addWidget(positionLabel_);
  transportLayout->addLayout(timelineHeader);

  progressSlider_ = new SeekSlider(Qt::Horizontal, transportPanel);
  progressSlider_->setObjectName(QStringLiteral("progressSlider"));
  progressSlider_->setAccessibleName(QStringLiteral("播放进度"));
  progressSlider_->setRange(0, kProgressMaximum);
  progressSlider_->setPageStep(50);
  progressSlider_->setEnabled(false);
  progressSlider_->setToolTip(
      QStringLiteral("可拖动或单击定位，左右方向键调整进度"));

  auto* const timelineRow = new QHBoxLayout();
  timelineRow->setSpacing(6);
  timelineRow->addWidget(progressSlider_, 1);

  volumeButton_ = new HoverOptionButton(transportPanel);
  volumeButton_->setObjectName(QStringLiteral("volumeButton"));
  volumeButton_->setProperty("volumeSelector", true);
  configureTransportButton(volumeButton_, QStringLiteral("音量 100%"),
                           QStringLiteral("音量 100%；悬停调节，点击静音"),
                           ControlIcon::Volume);
  auto* const volumePopup =
      new QFrame(volumeButton_, Qt::Tool | Qt::FramelessWindowHint |
                                    Qt::NoDropShadowWindowHint);
  volumePopup->setObjectName(QStringLiteral("volumePopup"));
  volumePopup->setAttribute(Qt::WA_ShowWithoutActivating);
  volumePopup->setAttribute(Qt::WA_TranslucentBackground);
  auto* const volumeLayout = new QVBoxLayout(volumePopup);
  volumeLayout->setContentsMargins(12, 10, 12, 12);
  volumeLayout->setSpacing(8);
  volumeLabel_ = new QLabel(QStringLiteral("100%"), volumePopup);
  volumeLabel_->setObjectName(QStringLiteral("volumeLabel"));
  volumeLabel_->setAlignment(Qt::AlignCenter);
  volumeSlider_ = new QSlider(Qt::Vertical, volumePopup);
  volumeSlider_->setObjectName(QStringLiteral("volumeSlider"));
  volumeSlider_->setAccessibleName(QStringLiteral("音量"));
  volumeSlider_->setRange(0, 100);
  volumeSlider_->setSingleStep(1);
  volumeSlider_->setPageStep(10);
  volumeSlider_->setValue(100);
  volumeSlider_->setFixedHeight(132);
  volumeSlider_->setToolTip(QStringLiteral("上下滑动调节音量"));
  volumeLayout->addWidget(volumeLabel_);
  volumeLayout->addWidget(volumeSlider_, 1, Qt::AlignHCenter);
  auto* const volumeHoverButton =
      static_cast<HoverOptionButton*>(volumeButton_);
  volumeHoverButton->setHoverPopup(volumePopup);
  volumeHoverButton->setCenteredPopupPlacement(-4, -16);
  volumeHoverButton->setCloseDelay(500);

  lyricsButton_ = new QToolButton(transportPanel);
  lyricsButton_->setObjectName(QStringLiteral("lyricsButton"));
  lyricsButton_->setProperty("transportControl", true);
  lyricsButton_->setProperty("lyricsControl", true);
  lyricsButton_->setText(QStringLiteral("词"));
  lyricsButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  lyricsButton_->setCheckable(true);
  lyricsButton_->setFixedSize(36, 36);
  lyricsButton_->setAccessibleName(QStringLiteral("显示歌词"));
  lyricsButton_->setToolTip(QStringLiteral("显示歌词"));

  keyboardSeekStepButton_ = new HoverOptionButton(transportPanel);
  keyboardSeekStepButton_->setObjectName(
      QStringLiteral("keyboardSeekStepButton"));
  keyboardSeekStepButton_->setAccessibleName(
      QStringLiteral("左右方向键快进后退秒数"));
  keyboardSeekStepButton_->setProperty("optionSelector", true);
  static_cast<HoverOptionButton*>(keyboardSeekStepButton_)
      ->setClickOpensMenu(true);
  keyboardSeekStepButton_->setToolTip(
      QStringLiteral("悬停或点击，选择左右方向键每次跳转的秒数"));
  auto* const seekStepMenu = new QMenu(keyboardSeekStepButton_);
  seekStepMenu->setObjectName(QStringLiteral("optionPopup"));
  auto* const seekStepGroup = new QActionGroup(seekStepMenu);
  seekStepGroup->setExclusive(true);
  const auto addSeekStepAction = [this, seekStepGroup](QMenu* const menu,
                                                       const int seconds) {
    auto* const action = menu->addAction(QStringLiteral("%1 秒").arg(seconds));
    action->setObjectName(
        QStringLiteral("keyboardSeekStepAction%1").arg(seconds));
    action->setData(seconds);
    action->setCheckable(true);
    seekStepGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, seconds] {
      keyboardSeekStepSeconds_ = seconds;
      selectKeyboardSeekStep(keyboardSeekStepButton_, seconds);
    });
  };
  for (const int seconds : kSeekSteps) {
    addSeekStepAction(seekStepMenu, seconds);
  }
  static_cast<HoverOptionButton*>(keyboardSeekStepButton_)
      ->setHoverMenu(seekStepMenu);
  selectKeyboardSeekStep(keyboardSeekStepButton_,
                         kDefaultKeyboardSeekStepSeconds);

  playbackRateButton_ = new HoverOptionButton(transportPanel);
  playbackRateButton_->setObjectName(QStringLiteral("playbackRateButton"));
  playbackRateButton_->setAccessibleName(QStringLiteral("当前播放倍速"));
  playbackRateButton_->setProperty("optionSelector", true);
  static_cast<HoverOptionButton*>(playbackRateButton_)->setClickOpensMenu(true);
  playbackRateButton_->setToolTip(
      QStringLiteral("悬停或点击，选择当前媒体的播放倍速"));
  auto* const playbackRateMenu = new QMenu(playbackRateButton_);
  playbackRateMenu->setObjectName(QStringLiteral("optionPopup"));
  auto* const playbackRateGroup = new QActionGroup(playbackRateMenu);
  playbackRateGroup->setExclusive(true);
  for (const double rate : kPlaybackRates) {
    auto* const action = playbackRateMenu->addAction(playbackRateText(rate));
    action->setObjectName(QStringLiteral("playbackRateAction%1")
                              .arg(static_cast<int>(rate * 100)));
    action->setData(rate);
    action->setCheckable(true);
    playbackRateGroup->addAction(action);
    connect(action, &QAction::triggered, this,
            [this, rate] { emit playbackRateRequested(rate); });
  }
  static_cast<HoverOptionButton*>(playbackRateButton_)
      ->setHoverMenu(playbackRateMenu);
  selectPlaybackRate(playbackRateButton_, 1.0);

  previousButton_ = new QToolButton(transportPanel);
  previousButton_->setObjectName(QStringLiteral("previousButton"));
  configureTransportButton(previousButton_, QStringLiteral("上一首"),
                           QStringLiteral("上一首（Ctrl+左）"),
                           ControlIcon::Previous);
  playPauseButton_ = new QToolButton(transportPanel);
  playPauseButton_->setObjectName(QStringLiteral("playPauseButton"));
  playPauseButton_->setProperty("primaryTransport", true);
  configureTransportButton(playPauseButton_, QStringLiteral("播放"),
                           QStringLiteral("播放（空格）"), ControlIcon::Play);
  nextButton_ = new QToolButton(transportPanel);
  nextButton_->setObjectName(QStringLiteral("nextButton"));
  configureTransportButton(nextButton_, QStringLiteral("下一首"),
                           QStringLiteral("下一首（Ctrl+右）"),
                           ControlIcon::Next);
  stopButton_ = new QToolButton(transportPanel);
  stopButton_->setObjectName(QStringLiteral("stopButton"));
  configureTransportButton(stopButton_, QStringLiteral("停止"),
                           QStringLiteral("停止播放"), ControlIcon::Stop);
  networkRefreshButton_ = new QToolButton(transportPanel);
  networkRefreshButton_->setObjectName(QStringLiteral("networkRefreshButton"));
  configureTransportButton(networkRefreshButton_, QStringLiteral("刷新直播"),
                           QStringLiteral("刷新当前直播（F5）"),
                           ControlIcon::Refresh);
  networkRefreshButton_->hide();

  playbackModeButton_ = new HoverOptionButton(transportPanel);
  playbackModeButton_->setObjectName(QStringLiteral("playbackModeButton"));
  configureTransportButton(playbackModeButton_, QStringLiteral("顺序播放"),
                           QStringLiteral("当前为顺序播放，悬停或点击切换"),
                           ControlIcon::Sequential);
  auto* const playbackModeMenu = new QMenu(playbackModeButton_);
  playbackModeMenu->setObjectName(QStringLiteral("optionPopup"));
  auto* const playbackModeGroup = new QActionGroup(playbackModeMenu);
  playbackModeGroup->setExclusive(true);
  const auto addPlaybackModeAction =
      [this, playbackModeGroup](QMenu* const menu, const int modeIndex) {
        auto* const action =
            menu->addAction(controlIcon(playbackModeIcon(modeIndex)),
                            playbackModeName(modeIndex));
        action->setObjectName(
            QStringLiteral("playbackModeAction%1").arg(modeIndex));
        action->setData(modeIndex);
        action->setCheckable(true);
        playbackModeGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, modeIndex] { emit playbackModeRequested(modeIndex); });
      };
  for (int modeIndex = 0; modeIndex < 4; ++modeIndex) {
    addPlaybackModeAction(playbackModeMenu, modeIndex);
  }
  static_cast<HoverOptionButton*>(playbackModeButton_)
      ->setHoverMenu(playbackModeMenu);
  selectPlaybackMode(playbackModeButton_, 0);
  connect(playbackModeButton_, &QToolButton::clicked, this, [this] {
    const int currentMode =
        playbackModeButton_->property("playbackModeIndex").toInt();
    emit playbackModeRequested((currentMode + 1) % 4);
  });

  fullScreenButton_ = new QToolButton(transportPanel);
  fullScreenButton_->setObjectName(QStringLiteral("fullScreenButton"));
  configureTransportButton(fullScreenButton_, QStringLiteral("进入全屏"),
                           QStringLiteral("进入全屏（F11）"),
                           ControlIcon::FullScreen);

  timelineRow->addWidget(previousButton_);
  timelineRow->addWidget(playPauseButton_);
  timelineRow->addWidget(nextButton_);
  timelineRow->addWidget(stopButton_);
  timelineRow->addWidget(networkRefreshButton_);
  timelineRow->addWidget(volumeButton_);
  timelineRow->addWidget(lyricsButton_);
  timelineRow->addWidget(playbackRateButton_);
  timelineRow->addWidget(keyboardSeekStepButton_);
  timelineRow->addWidget(playbackModeButton_);
  timelineRow->addWidget(fullScreenButton_);
  transportLayout->addLayout(timelineRow);
  rootLayout_->addWidget(transportPanel);

  fullScreenChrome_ = {eyebrow, title, subtitle, mediaCard};

  setCentralWidget(centralWidget);
  setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#centralSurface {
            background: #f4f0e6;
            color: #173c3a;
        }
        QMenuBar {
            background: #f4f0e6;
            color: #173c3a;
            padding: 4px 8px;
        }
        QMenuBar::item:selected, QMenu::item:selected {
            background: #dce7df;
        }
        QLabel#eyebrowLabel {
            color: #cc5a36;
            font-family: "Bahnschrift SemiCondensed";
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#titleLabel {
            color: #102f2d;
            font-family: "Microsoft YaHei UI";
            font-size: 34px;
            font-weight: 700;
        }
        QLabel#subtitleLabel {
            color: #57706b;
            font-family: "Microsoft YaHei UI";
            font-size: 14px;
        }
        QFrame#mediaCard {
            background: #fffdf7;
            border: 1px solid #d9d4c8;
            border-left: 5px solid #1f7770;
            border-radius: 8px;
        }
        QFrame#playlistPanel {
            background: #fffdf7;
            border: 1px solid #d9d4c8;
            border-radius: 8px;
        }
        QLabel#playlistTitleLabel {
            color: #173c3a;
            font-size: 15px;
            font-weight: 700;
        }
        QTabBar#playlistKindTabs::tab {
            background: #e9eee8;
            border: 1px solid #c7d1ca;
            color: #48645f;
            min-width: 80px;
            padding: 7px 4px;
        }
        QTabBar#playlistKindTabs::tab:first {
            border-top-left-radius: 5px;
            border-bottom-left-radius: 5px;
        }
        QTabBar#playlistKindTabs::tab:last {
            border-top-right-radius: 5px;
            border-bottom-right-radius: 5px;
        }
        QTabBar#playlistKindTabs::tab:selected {
            background: #1f7770;
            border-color: #1f7770;
            color: #fffdf7;
            font-weight: 700;
        }
        QLineEdit#livePlaylistUrlEdit {
            background: #f7f4eb;
            border: 1px solid #b6c8c0;
            border-radius: 5px;
            color: #173c3a;
            padding: 7px 8px;
        }
        QLineEdit#livePlaylistUrlEdit:focus {
            border-color: #1f7770;
        }
        QLabel#livePlaylistStatusLabel {
            color: #667973;
            font-size: 12px;
        }
        QListView#playlistView {
            background: #f7f4eb;
            border: 1px solid #d9d4c8;
            border-radius: 5px;
            color: #294b47;
            outline: none;
            padding: 4px;
        }
        QListView#playlistView::item {
            border-radius: 4px;
            padding: 7px 6px;
        }
        QListView#playlistView::item:selected {
            background: #dce7df;
            color: #174f4b;
        }
        QListView#playlistView::item:disabled {
            background: #fff0e8;
            color: #9b2f1f;
            font-weight: 700;
        }
        QToolButton[optionSelector="true"] {
            background: #edf3ee;
            border: 1px solid #b6c8c0;
            border-radius: 14px;
            color: #174f4b;
            font-size: 12px;
            font-weight: 700;
            min-width: 58px;
            padding: 7px 12px;
        }
        QToolButton[optionSelector="true"]:hover {
            background: #dcebe3;
            border-color: #1f7770;
            color: #103f3b;
        }
        QToolButton[optionSelector="true"]::menu-indicator {
            image: none;
            width: 0px;
        }
        QToolButton[transportControl="true"] {
            background: #edf3ee;
            border: 1px solid #b6c8c0;
            border-radius: 18px;
            padding: 0px;
        }
        QToolButton[transportControl="true"]:hover:enabled {
            background: #dcebe3;
            border-color: #1f7770;
        }
        QToolButton[transportControl="true"]:pressed:enabled {
            background: #c8ddd2;
        }
        QToolButton[transportControl="true"]:disabled {
            background: #e9e5dc;
            border-color: #d4cfc5;
        }
        QToolButton[lyricsControl="true"] {
            color: #174f4b;
            font-family: "Microsoft YaHei UI";
            font-size: 15px;
            font-weight: 800;
        }
        QToolButton[lyricsControl="true"]:checked {
            color: #8e3d25;
            background: #f4d8c9;
            border-color: #cc805d;
        }
        QToolButton[primaryTransport="true"] {
            background: #f4d8c9;
            border-color: #cc5a36;
        }
        QToolButton[playlistToggle="true"] {
            border-radius: 14px;
        }
        QMenu#optionPopup {
            background: #fffdf7;
            border: 1px solid #b9c9c2;
            border-radius: 11px;
            color: #294b47;
            padding: 7px;
        }
        QMenu#optionPopup::item {
            border-radius: 7px;
            margin: 2px;
            min-width: 86px;
            padding: 8px 22px;
        }
        QMenu#optionPopup::item:selected {
            background: #e0ece5;
            color: #174f4b;
        }
        QMenu#optionPopup::item:checked {
            background: #1f7770;
            color: #ffffff;
        }
        QFrame#volumePopup {
            background: #fffdf7;
            border: 1px solid #b9c9c2;
            border-radius: 11px;
            min-width: 92px;
        }
        QLabel#captionLabel {
            color: #778984;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#currentMediaLabel {
            color: #173c3a;
            font-size: 20px;
            font-weight: 600;
        }
        QLabel#playbackStatusLabel {
            background: #dce7df;
            border-radius: 12px;
            color: #1f625d;
            font-size: 12px;
            font-weight: 700;
            min-width: 92px;
            padding: 6px 12px;
        }
        QLabel#playbackErrorLabel {
            background: #fae7df;
            border-radius: 5px;
            color: #983f28;
            padding: 9px 12px;
        }
        QFrame#transportPanel {
            background: #fffdf7;
            border: 1px solid #d9d4c8;
            border-radius: 8px;
        }
        QLabel#transportCaptionLabel, QLabel#positionLabel,
        QLabel#volumeLabel {
            color: #49645f;
            font-size: 12px;
            font-weight: 700;
        }
        QSlider::groove:horizontal {
            background: #d9e1dc;
            border-radius: 3px;
            height: 6px;
        }
        QSlider::sub-page:horizontal {
            background: #cc5a36;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #fffdf7;
            border: 2px solid #1f7770;
            border-radius: 7px;
            margin: -5px 0;
            width: 14px;
        }
        QSlider::groove:horizontal:disabled {
            background: #e2dfd7;
        }
        QSlider::sub-page:horizontal:disabled {
            background: #d88a70;
        }
        QSlider::handle:horizontal:disabled {
            background: #fffaf0;
            border-color: #78918b;
        }
        QSlider::groove:vertical {
            background: #d9e1dc;
            border-radius: 3px;
            width: 6px;
        }
        QSlider::add-page:vertical {
            background: #cc5a36;
            border-radius: 3px;
        }
        QSlider::sub-page:vertical {
            background: #d9e1dc;
            border-radius: 3px;
        }
        QSlider::handle:vertical {
            background: #fffdf7;
            border: 2px solid #1f7770;
            border-radius: 7px;
            height: 14px;
            margin: 0 -5px;
        }
        QPushButton {
            background: #fffdf7;
            border: 1px solid #aebdb7;
            border-radius: 6px;
            color: #173c3a;
            font-size: 14px;
            font-weight: 600;
            min-width: 72px;
            padding: 10px 14px;
        }
        QPushButton:hover:enabled {
            background: #e6eee9;
            border-color: #1f7770;
        }
        QPushButton[primary="true"] {
            background: #1f7770;
            border-color: #1f7770;
            color: #ffffff;
        }
        QPushButton[primary="true"]:hover:enabled {
            background: #185f5a;
        }
        QPushButton:disabled {
            background: #e9e5dc;
            border-color: #d4cfc5;
            color: #a09d95;
        }
    )"));

  connect(openAction_, &QAction::triggered, this, &MainWindow::chooseLocalFile);
  connect(openNetworkAction_, &QAction::triggered, this,
          &MainWindow::chooseNetworkUrl);
  connect(openButton_, &QPushButton::clicked, this,
          &MainWindow::chooseLocalFile);
  connect(playlistKindTabs_, &QTabBar::currentChanged, this,
          [this](const int kindIndex) {
            showPlaylistKind(kindIndex);
            emit playlistKindSelected(kindIndex);
          });
  const auto requestLivePlaylistLoad = [this] {
    emit livePlaylistLoadRequested(livePlaylistUrlEdit_->text().trimmed());
  };
  connect(livePlaylistLoadButton_, &QPushButton::clicked, this,
          requestLivePlaylistLoad);
  connect(livePlaylistUrlEdit_, &QLineEdit::returnPressed, this,
          requestLivePlaylistLoad);
  connect(livePlaylistLocateButton_, &QPushButton::clicked, this, [this] {
    if (!isLivePlaylistActive_ || currentLivePlaybackIndex_ < 0) {
      return;
    }
    selectPlaylistRow(currentLivePlaybackIndex_);
    playlistView_->scrollTo(
        playlistView_->model()->index(currentLivePlaybackIndex_, 0),
        QAbstractItemView::PositionAtCenter);
  });
  connect(playPauseButton_, &QToolButton::clicked, this,
          &MainWindow::playbackToggleRequested);
  connect(stopButton_, &QToolButton::clicked, this, &MainWindow::stopRequested);
  connect(networkRefreshButton_, &QToolButton::clicked, this,
          &MainWindow::networkRefreshRequested);
  connect(playbackShortcut, &QShortcut::activated, this,
          &MainWindow::playbackToggleRequested);
  connect(networkRefreshShortcut, &QShortcut::activated, this,
          &MainWindow::networkRefreshRequested);
  connect(volumeUpShortcut, &QShortcut::activated, this,
          [this] { emit volumeStepRequested(kKeyboardVolumeStep); });
  connect(volumeDownShortcut, &QShortcut::activated, this,
          [this] { emit volumeStepRequested(-kKeyboardVolumeStep); });
  connect(seekBackwardShortcut, &QShortcut::activated, this,
          [this] { emit seekRelativeRequested(-keyboardSeekStepSeconds_); });
  connect(previousShortcut, &QShortcut::activated, this,
          &MainWindow::previousRequested);
  connect(nextShortcut, &QShortcut::activated, this,
          &MainWindow::nextRequested);
  connect(previousButton_, &QToolButton::clicked, this,
          &MainWindow::previousRequested);
  connect(nextButton_, &QToolButton::clicked, this, &MainWindow::nextRequested);
  connect(progressSlider_, &QSlider::sliderPressed, this,
          &MainWindow::seekStarted);
  connect(progressSlider_, &QSlider::sliderMoved, this,
          &MainWindow::seekPreviewRequested);
  connect(progressSlider_, &QSlider::sliderReleased, this,
          [this] { emit seekRequested(progressSlider_->value()); });
  connect(progressSlider_, &QSlider::valueChanged, this,
          [this](const int value) {
            if (!progressSlider_->isSliderDown()) {
              emit seekRequested(value);
            }
          });
  connect(volumeSlider_, &QSlider::valueChanged, this,
          &MainWindow::volumeRequested);
  connect(volumeButton_, &QToolButton::clicked, this, &MainWindow::muteToggled);
  connect(lyricsButton_, &QToolButton::clicked, this,
          &MainWindow::lyricsToggled);
  connect(playlistView_, &QListView::doubleClicked, this,
          [this](const QModelIndex& index) {
            emit playlistItemActivated(index.row());
          });
  connect(playlistView_, &QListView::customContextMenuRequested, this,
          &MainWindow::showPlaylistContextMenu);
  connect(playlistPlayAction_, &QAction::triggered, this, [this] {
    if (playlistContextRows_.size() == 1) {
      const int row = playlistContextRows_.front();
      if (row == currentPlaylistIndex_) {
        emit playRequested();
      } else {
        emit playlistItemActivated(row);
      }
    }
  });
  connect(playlistPauseAction_, &QAction::triggered, this,
          &MainWindow::pauseRequested);
  connect(playlistStopAction_, &QAction::triggered, this,
          &MainWindow::stopRequested);
  connect(playlistRenameAction_, &QAction::triggered, this,
          &MainWindow::renameContextPlaylistItem);
  connect(playlistMoveTopAction_, &QAction::triggered, this, [this] {
    if (playlistContextRows_.size() == 1) {
      emit playlistItemMoveRequested(playlistContextRows_.front(), 0);
      selectPlaylistRow(0);
    }
  });
  connect(playlistMoveUpAction_, &QAction::triggered, this, [this] {
    if (playlistContextRows_.size() == 1) {
      const int targetRow = playlistContextRows_.front() - 1;
      emit playlistItemMoveRequested(playlistContextRows_.front(), targetRow);
      selectPlaylistRow(targetRow);
    }
  });
  connect(playlistMoveDownAction_, &QAction::triggered, this, [this] {
    if (playlistContextRows_.size() == 1) {
      const int targetRow = playlistContextRows_.front() + 1;
      emit playlistItemMoveRequested(playlistContextRows_.front(), targetRow);
      selectPlaylistRow(targetRow);
    }
  });
  connect(playlistRemoveAction_, &QAction::triggered, this, [this] {
    if (!isLivePlaylistActive_ && !playlistContextRows_.isEmpty()) {
      emit playlistItemsRemoveRequested(playlistContextRows_);
    }
  });
  connect(livePlaylistPlaybackAction_, &QAction::triggered, this, [this] {
    if (!isLivePlaylistActive_ || playlistContextRows_.size() != 1) {
      return;
    }
    const int row = playlistContextRows_.front();
    const bool pausesCurrent = isCurrentPlaybackInActivePlaylist_ &&
                               row == currentPlaylistIndex_ &&
                               canPauseCurrentItem_;
    if (pausesCurrent) {
      emit pauseRequested();
    } else {
      emit playlistItemActivated(row);
    }
  });
  connect(livePlaylistStopAction_, &QAction::triggered, this, [this] {
    if (!isLivePlaylistActive_ || playlistContextRows_.size() != 1) {
      return;
    }
    const int row = playlistContextRows_.front();
    if (isCurrentPlaybackInActivePlaylist_ && row == currentPlaylistIndex_ &&
        canStopCurrentItem_) {
      emit stopRequested();
    }
  });
  // Qt 可能在数据角色刷新时把视图滚回当前索引，因此动作结束后显式恢复位置。
  const auto restoreLivePlaylistScroll = [this](const int scrollPosition) {
    playlistView_->verticalScrollBar()->setValue(scrollPosition);
    QTimer::singleShot(0, playlistView_, [this, scrollPosition] {
      if (isLivePlaylistActive_) {
        playlistView_->verticalScrollBar()->setValue(scrollPosition);
      }
    });
  };
  connect(livePlaylistMarkAction_, &QAction::triggered, this,
          [this, restoreLivePlaylistScroll] {
    if (isLivePlaylistActive_ && playlistContextRows_.size() == 1) {
      const int scrollPosition = playlistView_->verticalScrollBar()->value();
      emit livePlaylistMarkToggled(playlistContextRows_.front());
      restoreLivePlaylistScroll(scrollPosition);
    }
  });
  connect(livePlaylistFavoriteAction_, &QAction::triggered, this,
          [this, restoreLivePlaylistScroll] {
    if (isLivePlaylistActive_ && playlistContextRows_.size() == 1) {
      const int scrollPosition = playlistView_->verticalScrollBar()->value();
      emit livePlaylistFavoriteToggled(playlistContextRows_.front());
      restoreLivePlaylistScroll(scrollPosition);
    }
  });
  connect(playlistToggleButton_, &QToolButton::clicked, this,
          &MainWindow::togglePlaylistVisibility);
  connect(fullScreenButton_, &QToolButton::clicked, this,
          &MainWindow::toggleFullScreen);
  connect(fullScreenAction_, &QAction::triggered, this,
          &MainWindow::toggleFullScreen);
  connect(exitFullScreenAction, &QAction::triggered, this,
          &MainWindow::exitFullScreen);
  connect(videoOutput_, &VideoOutputWidget::surfaceReady, this,
          &MainWindow::videoSurfaceReady);
  connect(exitAction, &QAction::triggered, this, &MainWindow::close);

  PlayerViewState initialState;
  initialState.mediaName = QStringLiteral("未选择媒体");
  initialState.statusText = QStringLiteral("未打开媒体");
  applyViewState(initialState);
}

void MainWindow::applyViewState(const PlayerViewState& viewState) {
  const QSignalBlocker progressBlocker(progressSlider_);
  const QSignalBlocker volumeBlocker(volumeSlider_);
  const QSignalBlocker lyricsBlocker(lyricsButton_);
  openAction_->setEnabled(viewState.canOpen);
  openNetworkAction_->setEnabled(viewState.canOpen);
  showPlaylistKind(viewState.isLivePlaylistActive ? 1 : 0);
  openButton_->setEnabled(viewState.canOpen);
  livePlaylistUrlEdit_->setEnabled(!viewState.isLivePlaylistLoading);
  livePlaylistLoadButton_->setEnabled(!viewState.isLivePlaylistLoading);
  livePlaylistLoadButton_->setText(viewState.isLivePlaylistLoading
                                       ? QStringLiteral("正在载入...")
                                       : QStringLiteral("载入 / 刷新清单"));
  livePlaylistStatusLabel_->setText(viewState.livePlaylistStatusText);
  playPauseButton_->setEnabled(viewState.canPlay || viewState.canPause);
  const bool showsPause = viewState.canPause;
  playPauseButton_->setIcon(
      controlIcon(showsPause ? ControlIcon::Pause : ControlIcon::Play));
  playPauseButton_->setAccessibleName(showsPause ? QStringLiteral("暂停")
                                                 : QStringLiteral("播放"));
  playPauseButton_->setToolTip(showsPause ? QStringLiteral("暂停（空格）")
                                          : QStringLiteral("播放（空格）"));
  stopButton_->setEnabled(viewState.canStop);
  networkRefreshButton_->setVisible(viewState.canRefreshNetwork);
  networkRefreshButton_->setEnabled(viewState.canRefreshNetwork);
  previousButton_->setEnabled(viewState.canGoPrevious);
  nextButton_->setEnabled(viewState.canGoNext);
  canEditPlaylist_ =
      viewState.isPlaylistEditable && viewState.canRemovePlaylistItem;
  currentPlaylistIndex_ = viewState.currentPlaylistIndex;
  canPlayCurrentItem_ = viewState.canPlay;
  canPauseCurrentItem_ = viewState.canPause;
  canStopCurrentItem_ = viewState.canStop;
  isCurrentPlaybackInActivePlaylist_ =
      viewState.isCurrentPlaybackInActivePlaylist;
  currentLivePlaybackIndex_ = viewState.currentLivePlaybackIndex;
  livePlaylistLocateButton_->setEnabled(currentLivePlaybackIndex_ >= 0);
  if (!canEditPlaylist_) {
    playlistPlayAction_->setEnabled(false);
    playlistPauseAction_->setEnabled(false);
    playlistStopAction_->setEnabled(false);
    playlistRenameAction_->setEnabled(false);
    playlistMoveTopAction_->setEnabled(false);
    playlistMoveUpAction_->setEnabled(false);
    playlistMoveDownAction_->setEnabled(false);
    playlistRemoveAction_->setEnabled(false);
  }
  progressSlider_->setEnabled(viewState.canSeek);
  progressSlider_->setValue(viewState.progressValue);
  volumeSlider_->setValue(viewState.volumeValue);
  selectPlaybackMode(playbackModeButton_, viewState.playbackModeIndex);
  selectPlaybackRate(playbackRateButton_, viewState.isTemporaryFastPlayback
                                              ? 2.0
                                              : viewState.playbackRate);
  fullScreenAction_->setEnabled(viewState.canToggleFullscreen);
  fullScreenButton_->setEnabled(viewState.canToggleFullscreen);
  mediaNameLabel_->setText(viewState.mediaName);
  statusLabel_->setText(viewState.statusText);
  positionLabel_->setText(viewState.positionText);
  volumeLabel_->setText(QStringLiteral("%1%").arg(viewState.volumeValue));
  volumeButton_->setIcon(controlIcon(viewState.isMuted ? ControlIcon::Muted
                                                       : ControlIcon::Volume));
  volumeButton_->setAccessibleName(viewState.isMuted ? QStringLiteral("已静音")
                                                     : viewState.volumeText);
  volumeButton_->setToolTip(
      QStringLiteral("%1；悬停后上下滑动调节，点击%2")
          .arg(viewState.volumeText, viewState.isMuted
                                         ? QStringLiteral("恢复声音")
                                         : QStringLiteral("静音")));
  lyricsButton_->setEnabled(viewState.canShowLyrics);
  lyricsButton_->setChecked(viewState.isLyricsVisible);
  lyricsButton_->setAccessibleName(viewState.isLyricsVisible
                                       ? QStringLiteral("隐藏歌词")
                                       : QStringLiteral("显示歌词"));
  lyricsButton_->setToolTip(viewState.isLyricsVisible
                                ? QStringLiteral("返回音频波形")
                                : QStringLiteral("显示歌词"));
  mediaDisplayStack_->setCurrentWidget(
      viewState.isLyricsVisible ? static_cast<QWidget*>(lyricsView_)
                                : static_cast<QWidget*>(videoOutput_));
  lyricsView_->setMediaName(viewState.mediaName);
  lyricsView_->setPosition(viewState.positionMilliseconds);
  const bool hasPlaylistSelection =
      playlistView_->selectionModel() != nullptr &&
      !playlistView_->selectionModel()->selectedRows().isEmpty();
  if (!hasPlaylistSelection && playlistView_->model() != nullptr &&
      viewState.currentPlaylistIndex >= 0) {
    playlistView_->setCurrentIndex(
        playlistView_->model()->index(viewState.currentPlaylistIndex, 0));
  } else if (!hasPlaylistSelection) {
    playlistView_->setCurrentIndex(QModelIndex{});
  }
  videoOutput_->setPresentation(
      viewState.isVideoSurfaceActive, viewState.isAudioVisualizationActive,
      viewState.isAudioVisualizationPlaying, viewState.progressValue,
      viewState.mediaName, viewState.videoPlaceholder);
  if (!viewState.canToggleFullscreen && isFullScreen()) {
    showNormal();
  }
}

void MainWindow::setAudioWaveform(core::AudioWaveform waveform) {
  videoOutput_->setAudioWaveform(std::move(waveform));
}

void MainWindow::showLyricsLoading() { lyricsView_->showLoading(); }

void MainWindow::setLyricsResult(const LyricsResult& result) {
  lyricsView_->setResult(result);
}

void MainWindow::clearLyrics() { lyricsView_->clearLyrics(); }

void MainWindow::showPlaybackError(const QString& message) {
  errorLabel_->setText(message);
  errorLabel_->setVisible(!message.isEmpty());
}

void MainWindow::clearPlaybackError() {
  errorLabel_->clear();
  errorLabel_->hide();
}

void MainWindow::showPlaylistContextMenu(const QPoint& position) {
  if (playlistView_->model() == nullptr ||
      playlistView_->selectionModel() == nullptr) {
    return;
  }
  const QModelIndex clickedIndex = playlistView_->indexAt(position);
  if (!clickedIndex.isValid()) {
    playlistContextRows_.clear();
    return;
  }

  if (isLivePlaylistActive_) {
    playlistContextRows_ = {clickedIndex.row()};
    const bool isMarked =
        clickedIndex.data(PlaylistModel::kMarkedRole).toBool();
    const bool isFavorite =
        clickedIndex.data(PlaylistModel::kFavoriteRole).toBool();
    const bool targetsCurrentItem =
        isCurrentPlaybackInActivePlaylist_ &&
        clickedIndex.row() == currentPlaylistIndex_;
    const bool showsPause = targetsCurrentItem && canPauseCurrentItem_;
    livePlaylistPlaybackAction_->setText(
        showsPause ? QStringLiteral("暂停") : QStringLiteral("播放"));
    livePlaylistPlaybackAction_->setEnabled(
        !isMarked &&
        (showsPause || !targetsCurrentItem || canPlayCurrentItem_));
    livePlaylistStopAction_->setEnabled(
        !isMarked && targetsCurrentItem && canStopCurrentItem_);
    livePlaylistMarkAction_->setText(
        isMarked ? QStringLiteral("取消标记") : QStringLiteral("标记"));
    livePlaylistMarkAction_->setEnabled(true);
    livePlaylistFavoriteAction_->setText(
        isFavorite ? QStringLiteral("取消收藏") : QStringLiteral("收藏"));
    livePlaylistFavoriteAction_->setEnabled(true);
    livePlaylistContextMenu_->popup(
        playlistView_->viewport()->mapToGlobal(position));
    return;
  }

  auto* const selection = playlistView_->selectionModel();
  if (!selection->isSelected(clickedIndex)) {
    selection->setCurrentIndex(
        clickedIndex,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  } else {
    selection->setCurrentIndex(clickedIndex, QItemSelectionModel::NoUpdate);
  }

  playlistContextRows_.clear();
  const QModelIndexList selectedRows = selection->selectedRows();
  playlistContextRows_.reserve(selectedRows.size());
  for (const QModelIndex& index : selectedRows) {
    playlistContextRows_.append(index.row());
  }
  std::sort(playlistContextRows_.begin(), playlistContextRows_.end());

  const bool hasSingleItem = playlistContextRows_.size() == 1;
  const int clickedRow = clickedIndex.row();
  const int itemCount = playlistView_->model()->rowCount();
  const bool targetsCurrentItem =
      hasSingleItem && isCurrentPlaybackInActivePlaylist_ &&
      clickedRow == currentPlaylistIndex_;
  playlistPlayAction_->setEnabled(canEditPlaylist_ && hasSingleItem &&
                                  (!targetsCurrentItem || canPlayCurrentItem_));
  playlistPauseAction_->setEnabled(canEditPlaylist_ && targetsCurrentItem &&
                                   canPauseCurrentItem_);
  playlistStopAction_->setEnabled(canEditPlaylist_ && targetsCurrentItem &&
                                  canStopCurrentItem_);
  playlistRenameAction_->setEnabled(canEditPlaylist_ && hasSingleItem);
  playlistMoveTopAction_->setEnabled(canEditPlaylist_ && hasSingleItem &&
                                     clickedRow > 0);
  playlistMoveUpAction_->setEnabled(canEditPlaylist_ && hasSingleItem &&
                                    clickedRow > 0);
  playlistMoveDownAction_->setEnabled(canEditPlaylist_ && hasSingleItem &&
                                      clickedRow + 1 < itemCount);
  playlistRemoveAction_->setEnabled(canEditPlaylist_ &&
                                    !playlistContextRows_.isEmpty());
  playlistContextMenu_->popup(playlistView_->viewport()->mapToGlobal(position));
}

void MainWindow::renameContextPlaylistItem() {
  if (playlistContextRows_.size() != 1 || playlistView_->model() == nullptr) {
    return;
  }
  const int row = playlistContextRows_.front();
  const QModelIndex index = playlistView_->model()->index(row, 0);
  if (!index.isValid()) {
    return;
  }

  bool wasAccepted = false;
  const QString currentName = index.data(Qt::UserRole).toString();
  const QString newName =
      QInputDialog::getText(
          this, QStringLiteral("重命名列表项"),
          QStringLiteral("仅修改列表显示名，不会更改电脑中的文件名："),
          QLineEdit::Normal, currentName, &wasAccepted)
          .trimmed();
  if (!wasAccepted || newName.isEmpty() || newName == currentName) {
    return;
  }
  emit playlistItemRenameRequested(row, newName);
  selectPlaylistRow(row);
}

void MainWindow::selectPlaylistRow(const int row) {
  if (playlistView_->model() == nullptr ||
      playlistView_->selectionModel() == nullptr || row < 0 ||
      row >= playlistView_->model()->rowCount()) {
    return;
  }
  const QModelIndex index = playlistView_->model()->index(row, 0);
  playlistView_->selectionModel()->setCurrentIndex(
      index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  playlistView_->scrollTo(index);
}

void MainWindow::setPlaylistModels(QAbstractItemModel* const localModel,
                                   QAbstractItemModel* const liveModel) {
  localPlaylistModel_ = localModel;
  livePlaylistModel_ = liveModel;
  showPlaylistKind(isLivePlaylistActive_ ? 1 : 0);
}

void MainWindow::showPlaylistKind(const int kindIndex) {
  const bool showsLivePlaylist = kindIndex == 1;
  isLivePlaylistActive_ = showsLivePlaylist;
  const QSignalBlocker blocker(playlistKindTabs_);
  playlistKindTabs_->setCurrentIndex(showsLivePlaylist ? 1 : 0);
  QAbstractItemModel* const model =
      showsLivePlaylist ? livePlaylistModel_ : localPlaylistModel_;
  if (playlistView_->model() != model) {
    playlistView_->setModel(model);
  }
  playlistView_->setAccessibleName(showsLivePlaylist
                                       ? QStringLiteral("直播列表")
                                       : QStringLiteral("本地播放列表"));
  playlistView_->setSelectionMode(showsLivePlaylist
                                      ? QAbstractItemView::SingleSelection
                                      : QAbstractItemView::ExtendedSelection);
  playlistView_->setContextMenuPolicy(Qt::CustomContextMenu);
  livePlaylistUrlEdit_->setVisible(showsLivePlaylist);
  livePlaylistLoadButton_->setVisible(showsLivePlaylist);
  livePlaylistLocateButton_->setVisible(showsLivePlaylist);
  livePlaylistStatusLabel_->setVisible(showsLivePlaylist);
  openButton_->setVisible(!showsLivePlaylist);
}

void MainWindow::closeEvent(QCloseEvent* const event) {
  emit closing();
  QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* const event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange) {
    updateFullScreenText();
  }
}

bool MainWindow::eventFilter(QObject* const watched, QEvent* const event) {
  if (playlistView_ != nullptr && watched == playlistView_->viewport() &&
      event->type() == QEvent::MouseButtonDblClick) {
    const auto* const mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() == Qt::RightButton) {
      showPlaylistContextMenu(mouseEvent->pos());
      return true;
    }
  }
  if (event->type() != QEvent::KeyPress &&
      event->type() != QEvent::KeyRelease) {
    return QMainWindow::eventFilter(watched, event);
  }

  auto* const keyEvent = static_cast<QKeyEvent*>(event);
  if (keyEvent->key() != Qt::Key_Right ||
      keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
    return QMainWindow::eventFilter(watched, event);
  }
  if (keyEvent->isAutoRepeat()) {
    return true;
  }

  if (event->type() == QEvent::KeyPress) {
    if (!isRightKeyPressed_) {
      isRightKeyPressed_ = true;
      isRightKeyHoldActive_ = false;
      rightKeyHoldTimer_->start();
    }
    return true;
  }

  rightKeyHoldTimer_->stop();
  if (isRightKeyHoldActive_) {
    emit temporaryFastPlaybackRequested(false);
  } else if (isRightKeyPressed_) {
    emit seekRelativeRequested(keyboardSeekStepSeconds_);
  }
  isRightKeyPressed_ = false;
  isRightKeyHoldActive_ = false;
  return true;
}

void MainWindow::chooseLocalFile() {
  const QStringList filePaths = QFileDialog::getOpenFileNames(
      this, QStringLiteral("打开本地媒体"), {},
      QStringLiteral(
          "媒体文件 (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.mp4 *.mkv "
          "*.avi *.mov *.webm);;所有文件 (*.*)"));
  if (!filePaths.isEmpty()) {
    emit localFilesSelected(filePaths);
  }
}

void MainWindow::chooseNetworkUrl() {
  bool wasAccepted = false;
  QString address;
  if (recentNetworkUrls_.isEmpty()) {
    address = QInputDialog::getText(
        this, QStringLiteral("打开网络地址"),
        QStringLiteral("输入直播流或 M3U/M3U8 清单 URL："), QLineEdit::Normal,
        {}, &wasAccepted);
  } else {
    address = QInputDialog::getItem(
        this, QStringLiteral("打开网络地址"),
        QStringLiteral("输入或选择直播流、M3U/M3U8 清单 URL："),
        recentNetworkUrls_, 0, true, &wasAccepted);
  }
  address = address.trimmed();
  if (wasAccepted) {
    emit networkUrlSelected(address);
  }
}

void MainWindow::setRecentNetworkUrls(const QStringList& urls) {
  recentNetworkUrls_ = urls;
}

const QStringList& MainWindow::recentNetworkUrls() const noexcept {
  return recentNetworkUrls_;
}

void MainWindow::setLivePlaylistUrl(const QString& url) {
  livePlaylistUrlEdit_->setText(url);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* const event) {
  if (!localFilePaths(event->mimeData()).isEmpty()) {
    event->acceptProposedAction();
    return;
  }
  event->ignore();
}

void MainWindow::dropEvent(QDropEvent* const event) {
  const QStringList paths = localFilePaths(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  emit localFilesSelected(paths);
}

void MainWindow::toggleFullScreen() {
  if (isFullScreen()) {
    showNormal();
  } else {
    showFullScreen();
  }
  updateFullScreenText();
}

void MainWindow::exitFullScreen() {
  if (isFullScreen()) {
    showNormal();
    updateFullScreenText();
  }
}

void MainWindow::updateFullScreenText() {
  if (fullScreenAction_ == nullptr || fullScreenButton_ == nullptr) {
    return;
  }

  const bool isNowFullScreen = isFullScreen();
  menuBar()->setVisible(!isNowFullScreen);
  for (auto* const widget : fullScreenChrome_) {
    widget->setVisible(!isNowFullScreen);
  }
  playlistPanel_->setVisible(!isNowFullScreen && isPlaylistExpanded_);
  playlistToggleButton_->setVisible(!isNowFullScreen);
  if (isNowFullScreen) {
    rootLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout_->setSpacing(0);
  } else {
    rootLayout_->setContentsMargins(
        kNormalHorizontalMargin, kNormalVerticalMargin, kNormalHorizontalMargin,
        kNormalVerticalMargin);
    rootLayout_->setSpacing(kNormalSpacing);
  }

  const QString actionText = isNowFullScreen ? QStringLiteral("退出全屏(&F)")
                                             : QStringLiteral("进入全屏(&F)");
  fullScreenAction_->setText(actionText);
  fullScreenButton_->setIcon(controlIcon(
      isNowFullScreen ? ControlIcon::ExitFullScreen : ControlIcon::FullScreen));
  fullScreenButton_->setAccessibleName(isNowFullScreen
                                           ? QStringLiteral("退出全屏")
                                           : QStringLiteral("进入全屏"));
  fullScreenButton_->setToolTip(isNowFullScreen
                                    ? QStringLiteral("退出全屏（F11 或 Esc）")
                                    : QStringLiteral("进入全屏（F11）"));
}

void MainWindow::togglePlaylistVisibility() {
  isPlaylistExpanded_ = !isPlaylistExpanded_;
  playlistPanel_->setVisible(isPlaylistExpanded_);
  updatePlaylistToggleAppearance();
  videoOutput_->updateGeometry();
}

void MainWindow::updatePlaylistToggleAppearance() {
  const QString actionText = isPlaylistExpanded_
                                 ? QStringLiteral("收起播放列表")
                                 : QStringLiteral("展开播放列表");
  playlistToggleButton_->setIcon(controlIcon(isPlaylistExpanded_
                                                 ? ControlIcon::CollapseRight
                                                 : ControlIcon::ExpandLeft));
  playlistToggleButton_->setAccessibleName(actionText);
  playlistToggleButton_->setToolTip(actionText);
}

}  // namespace mediahub::gui
