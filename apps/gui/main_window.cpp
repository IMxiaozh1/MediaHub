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
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedLayout>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>

#include "browser_backend.h"
#include "browser_event_listener.h"
#include "browser_page.h"
#include "lyrics_view.h"
#include "live_source_memo_dialog.h"
#include "live_url_history_dialog.h"
#include "playlist_model.h"
#include "seek_slider.h"
#include "shortcut_help_dialog.h"
#include "video_output_widget.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <qt_windows.h>
#endif

namespace mediahub::gui {
namespace {

class UnavailableBrowserBackend final : public BrowserBackend {
 public:
  void setEventListener(BrowserEventListener* listener) override {
    listener_ = listener;
  }

  void initialize(void*, const QString&, std::uint64_t generation) override {
    if (listener_ != nullptr) {
      listener_->onBrowserError(generation, BrowserErrorKind::RuntimeUnavailable,
                                0);
    }
  }

  void navigate(const QString&, std::uint64_t) override {}
  void goBack() override {}
  void goForward() override {}
  void reloadOrStop() override {}
  void setBounds(const QRect&) override {}
  void setVisible(bool) override {}
  void setAudioMuted(bool) override {}
  void setSuspended(bool) override {}
  void clearBrowsingData(std::uint64_t) override {}
  void answerPermission(std::uint64_t, BrowserPermissionDecision) override {}
  void chooseDownloadPath(std::uint64_t, const QString&) override {}
  void cancelDownload(std::uint64_t) override {}
  void answerExternalProtocol(std::uint64_t, bool) override {}
  void answerCertificateError(std::uint64_t,
                              BrowserCertificateDecision) override {}
  void exitFullScreen() override {}
  void closePopups() noexcept override {}
  void shutdown() noexcept override { listener_ = nullptr; }

 private:
  BrowserEventListener* listener_{nullptr};
};

constexpr int kNormalHorizontalMargin = 22;
constexpr int kNormalVerticalMargin = 18;
constexpr int kNormalSpacing = 12;
constexpr int kPlaylistMinimumWidth = 260;
constexpr int kPlaylistMaximumWidth = 440;
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
  History,
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
  const QColor ink(QStringLiteral("#20ad72"));
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
    case ControlIcon::History:
      painter.drawArc(QRectF(3.5, 3.5, 13, 13), 55 * 16, 285 * 16);
      painter.drawLine(QPointF(4, 4), QPointF(4, 8));
      painter.drawLine(QPointF(4, 4), QPointF(8, 4));
      painter.drawLine(QPointF(10, 6), QPointF(10, 10));
      painter.drawLine(QPointF(10, 10), QPointF(13, 12));
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

void setNativeDarkTitleBar(QWidget* const window, const bool isDark) {
#ifdef Q_OS_WIN
  const BOOL enabled = isDark ? TRUE : FALSE;
  const auto handle = reinterpret_cast<HWND>(window->winId());
  static_cast<void>(DwmSetWindowAttribute(
      handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled)));
#else
  Q_UNUSED(window);
  Q_UNUSED(isDark);
#endif
}

}  // namespace

MainWindow::MainWindow(BrowserBackend* const browserBackend,
                       QString browserProfileDirectory, QWidget* const parent,
                       BrowserDataStore* const browserDataStore)
    : QMainWindow(parent), windowIconManager_(this) {
  if (browserBackend == nullptr) {
    ownedBrowserBackend_ = std::make_unique<UnavailableBrowserBackend>();
    browserBackend_ = ownedBrowserBackend_.get();
  } else {
    browserBackend_ = browserBackend;
  }
  setWindowTitle(QStringLiteral("MediaHub"));
  windowIconManager_.apply();
  resize(1200, 800);
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
  recentLocalMediaMenu_ =
      fileMenu->addMenu(QStringLiteral("最近播放(&R)"));
  recentLocalMediaMenu_->setObjectName(
      QStringLiteral("recentLocalMediaMenu"));
  setRecentLocalMedia({});
  fileMenu->addSeparator();
  auto* const exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
  exitAction->setShortcut(QKeySequence::Quit);
  auto* const viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
  fullScreenAction_ = viewMenu->addAction(QStringLiteral("进入全屏(&F)"));
  fullScreenAction_->setObjectName(QStringLiteral("fullScreenAction"));
  auto* const fullScreenShortcut =
      new QShortcut(QKeySequence(Qt::Key_F11), this);
  fullScreenShortcut->setObjectName(QStringLiteral("fullScreenShortcut"));
  fullScreenShortcut->setContext(Qt::WindowShortcut);
  fullScreenShortcut->setAutoRepeat(false);
  auto* const exitFullScreenAction = new QAction(this);
  exitFullScreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(exitFullScreenAction);
  auto* const helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
  auto* const shortcutHelpAction =
      helpMenu->addAction(QStringLiteral("快捷键(&K)..."));
  shortcutHelpAction->setObjectName(QStringLiteral("shortcutHelpAction"));
  connect(shortcutHelpAction, &QAction::triggered, this,
          &MainWindow::showShortcutHelp);
  auto* const liveSourceMemoAction =
      helpMenu->addAction(QStringLiteral("直播源(&L)..."));
  liveSourceMemoAction->setObjectName(
      QStringLiteral("liveSourceMemoAction"));
  liveSourceMemoAction->setShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) |
                   static_cast<int>(Qt::Key_M)));
  liveSourceMemoAction->setShortcutContext(Qt::WindowShortcut);
  connect(liveSourceMemoAction, &QAction::triggered, this,
          &MainWindow::showLiveSourceMemo);

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
  auto* const muteShortcut = new QShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) |
                   static_cast<int>(Qt::Key_Down)),
      this);
  muteShortcut->setObjectName(QStringLiteral("muteShortcut"));
  muteShortcut->setContext(Qt::WindowShortcut);
  muteShortcut->setAutoRepeat(false);
  auto* const unmuteShortcut = new QShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) |
                   static_cast<int>(Qt::Key_Up)),
      this);
  unmuteShortcut->setObjectName(QStringLiteral("unmuteShortcut"));
  unmuteShortcut->setContext(Qt::WindowShortcut);
  unmuteShortcut->setAutoRepeat(false);
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
  centralSurface_ = new QWidget(this);
  centralSurface_->setObjectName(QStringLiteral("centralSurface"));
  auto* const outerLayout = new QVBoxLayout(centralSurface_);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);

  displayModePanel_ = new QFrame(centralSurface_);
  displayModePanel_->setObjectName(QStringLiteral("displayModePanel"));
  auto* const displayModeLayout = new QHBoxLayout(displayModePanel_);
  displayModeLayout->setContentsMargins(22, 12, 22, 8);
  displayModeLayout->setSpacing(8);
  const auto makeModeButton = [this](const QString& objectName,
                                     const QString& text) {
    auto* const button = new QToolButton(displayModePanel_);
    button->setObjectName(objectName);
    button->setText(text);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setMinimumSize(116, 34);
    return button;
  };
  localModeButton_ =
      makeModeButton(QStringLiteral("localModeButton"), QStringLiteral("本地"));
  liveModeButton_ =
      makeModeButton(QStringLiteral("liveModeButton"), QStringLiteral("直播"));
  webModeButton_ =
      makeModeButton(QStringLiteral("webModeButton"), QStringLiteral("网页"));
  displayModeLayout->addWidget(localModeButton_);
  displayModeLayout->addWidget(liveModeButton_);
  displayModeLayout->addWidget(webModeButton_);
  displayModeLayout->addStretch(1);
  outerLayout->addWidget(displayModePanel_);

  auto* const displayModeContainer = new QWidget(centralSurface_);
  displayModeStack_ = new QStackedLayout(displayModeContainer);
  displayModeStack_->setContentsMargins(0, 0, 0, 0);
  nativePlaybackPage_ = new QWidget(displayModeContainer);
  nativePlaybackPage_->setObjectName(QStringLiteral("nativePlaybackPage"));
  auto* const centralWidget = nativePlaybackPage_;
  rootLayout_ = new QVBoxLayout(centralWidget);
  rootLayout_->setContentsMargins(
      kNormalHorizontalMargin, kNormalVerticalMargin, kNormalHorizontalMargin,
      kNormalVerticalMargin);
  rootLayout_->setSpacing(kNormalSpacing);

  headerPanel_ = new QFrame(centralWidget);
  headerPanel_->setObjectName(QStringLiteral("headerPanel"));
  auto* const headerLayout = new QHBoxLayout(headerPanel_);
  headerLayout->setContentsMargins(18, 12, 18, 12);
  headerLayout->setSpacing(16);
  auto* const headerCopyLayout = new QVBoxLayout();
  headerCopyLayout->setContentsMargins(0, 0, 0, 0);
  headerCopyLayout->setSpacing(2);
  eyebrowLabel_ = new QLabel(headerPanel_);
  eyebrowLabel_->setObjectName(QStringLiteral("eyebrowLabel"));
  titleLabel_ = new QLabel(headerPanel_);
  titleLabel_->setObjectName(QStringLiteral("titleLabel"));
  subtitleLabel_ = new QLabel(headerPanel_);
  subtitleLabel_->setObjectName(QStringLiteral("subtitleLabel"));
  headerCopyLayout->addWidget(eyebrowLabel_);
  headerCopyLayout->addWidget(titleLabel_);
  headerCopyLayout->addWidget(subtitleLabel_);
  modeBadgeLabel_ = new QLabel(headerPanel_);
  modeBadgeLabel_->setObjectName(QStringLiteral("modeBadgeLabel"));
  modeBadgeLabel_->setAlignment(Qt::AlignCenter);
  headerLayout->addLayout(headerCopyLayout, 1);
  headerLayout->addWidget(modeBadgeLabel_, 0, Qt::AlignVCenter);
  rootLayout_->addWidget(headerPanel_);

  auto* const mediaWorkspace = new QHBoxLayout();
  mediaWorkspace->setSpacing(10);
  mediaDisplay_ = new QWidget(centralWidget);
  auto* const mediaDisplay = mediaDisplay_;
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
  playlistPanel_->setFixedWidth(kPlaylistMinimumWidth);
  auto* const playlistLayout = new QVBoxLayout(playlistPanel_);
  playlistLayout->setContentsMargins(16, 14, 16, 14);
  playlistLayout->setSpacing(10);
  playlistTitleLabel_ =
      new QLabel(QStringLiteral("播放列表"), playlistPanel_);
  playlistTitleLabel_->setObjectName(QStringLiteral("playlistTitleLabel"));
  playlistKindTabs_ = new QTabBar(playlistPanel_);
  playlistKindTabs_->setObjectName(QStringLiteral("playlistKindTabs"));
  playlistKindTabs_->setAccessibleName(QStringLiteral("播放列表类型"));
  playlistKindTabs_->setDocumentMode(true);
  playlistKindTabs_->setExpanding(true);
  playlistKindTabs_->setUsesScrollButtons(false);
  playlistKindTabs_->setElideMode(Qt::ElideNone);
  playlistKindTabs_->addTab(QStringLiteral("本地列表"));
  playlistKindTabs_->addTab(QStringLiteral("直播列表"));
  livePlaylistTools_ = new QFrame(playlistPanel_);
  livePlaylistTools_->setObjectName(QStringLiteral("livePlaylistTools"));
  auto* const livePlaylistToolsLayout = new QVBoxLayout(livePlaylistTools_);
  livePlaylistToolsLayout->setContentsMargins(9, 8, 9, 9);
  livePlaylistToolsLayout->setSpacing(6);
  auto* const livePlaylistSourceLabel =
      new QLabel(QStringLiteral("REMOTE PLAYLIST / SIGNAL SOURCE"),
                 livePlaylistTools_);
  livePlaylistSourceLabel->setObjectName(
      QStringLiteral("livePlaylistSourceLabel"));
  livePlaylistUrlEdit_ = new QLineEdit(livePlaylistTools_);
  livePlaylistUrlEdit_->setObjectName(QStringLiteral("livePlaylistUrlEdit"));
  livePlaylistUrlEdit_->setAccessibleName(QStringLiteral("远程直播清单 URL"));
  livePlaylistUrlEdit_->setPlaceholderText(
      QStringLiteral("https://example.com/list.m3u"));
  livePlaylistUrlEdit_->setClearButtonEnabled(true);
  livePlaylistHistoryButton_ = new QToolButton(livePlaylistTools_);
  livePlaylistHistoryButton_->setObjectName(
      QStringLiteral("livePlaylistHistoryButton"));
  configureTransportButton(livePlaylistHistoryButton_,
                           QStringLiteral("历史直播源"),
                           QStringLiteral("历史直播源"),
                           ControlIcon::History);
  livePlaylistHistoryButton_->setFixedSize(32, 32);
  livePlaylistLoadButton_ =
      new QPushButton(QStringLiteral("载入 / 刷新清单"), livePlaylistTools_);
  livePlaylistLoadButton_->setObjectName(
      QStringLiteral("livePlaylistLoadButton"));
  livePlaylistLoadButton_->setProperty("primary", true);
  livePlaylistLocateButton_ =
      new QPushButton(QStringLiteral("定位正在播放"), livePlaylistTools_);
  livePlaylistLocateButton_->setObjectName(
      QStringLiteral("livePlaylistLocateButton"));
  livePlaylistLocateButton_->setToolTip(
      QStringLiteral("滚动并选中当前正在播放的直播源"));
  livePlaylistStatusLabel_ =
      new QLabel(QStringLiteral("输入远程 M3U/M3U8 清单 URL"),
                 livePlaylistTools_);
  livePlaylistStatusLabel_->setObjectName(
      QStringLiteral("livePlaylistStatusLabel"));
  livePlaylistStatusLabel_->setWordWrap(true);
  livePlaylistSearchEdit_ = new QLineEdit(playlistPanel_);
  livePlaylistSearchEdit_->setObjectName(
      QStringLiteral("livePlaylistSearchEdit"));
  livePlaylistSearchEdit_->setAccessibleName(
      QStringLiteral("搜索当前直播清单"));
  livePlaylistSearchEdit_->setPlaceholderText(
      QStringLiteral("搜索当前清单中的频道"));
  livePlaylistSearchEdit_->setClearButtonEnabled(true);
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
  auto* const livePlaylistUrlRow = new QHBoxLayout();
  livePlaylistUrlRow->setSpacing(6);
  livePlaylistUrlRow->addWidget(livePlaylistUrlEdit_, 1);
  livePlaylistUrlRow->addWidget(livePlaylistHistoryButton_);
  auto* const livePlaylistButtonRow = new QHBoxLayout();
  livePlaylistButtonRow->setSpacing(6);
  livePlaylistButtonRow->addWidget(livePlaylistLoadButton_, 7);
  livePlaylistButtonRow->addWidget(livePlaylistLocateButton_, 5);
  livePlaylistToolsLayout->addWidget(livePlaylistSourceLabel);
  livePlaylistToolsLayout->addLayout(livePlaylistUrlRow);
  livePlaylistToolsLayout->addLayout(livePlaylistButtonRow);
  livePlaylistToolsLayout->addWidget(livePlaylistStatusLabel_);

  playlistLayout->addWidget(playlistTitleLabel_);
  playlistLayout->addWidget(playlistKindTabs_);
  playlistLayout->addWidget(livePlaylistTools_);
  playlistLayout->addWidget(livePlaylistSearchEdit_);
  playlistLayout->addWidget(playlistView_, 1);
  playlistLayout->addWidget(openButton_);
  mediaWorkspace->addWidget(playlistPanel_, 1);
  rootLayout_->addLayout(mediaWorkspace, 1);

  mediaCard_ = new QFrame(centralWidget);
  auto* const mediaCard = mediaCard_;
  mediaCard->setObjectName(QStringLiteral("mediaCard"));
  auto* const cardLayout = new QVBoxLayout(mediaCard);
  cardLayout->setContentsMargins(18, 12, 18, 12);
  cardLayout->setSpacing(6);

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

  transportPanel_ = new QFrame(centralWidget);
  auto* const transportPanel = transportPanel_;
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

  browserPage_ = new BrowserPage(*browserBackend_,
                                 std::move(browserProfileDirectory),
                                 displayModeContainer, browserDataStore);
  connect(browserPage_, &BrowserPage::fullScreenChanged, this,
          &MainWindow::handleWebFullScreenChanged);
  displayModeStack_->addWidget(nativePlaybackPage_);
  displayModeStack_->addWidget(browserPage_);
  displayModeStack_->setCurrentWidget(nativePlaybackPage_);
  outerLayout->addWidget(displayModeContainer, 1);

  fullScreenChrome_ = {displayModePanel_, headerPanel_, mediaCard_};

  setCentralWidget(centralSurface_);
  setStyleSheet(mainWindowStyleSheet());

  connect(openAction_, &QAction::triggered, this, &MainWindow::chooseLocalFile);
  connect(openNetworkAction_, &QAction::triggered, this,
          &MainWindow::chooseNetworkUrl);
  connect(openButton_, &QPushButton::clicked, this,
          &MainWindow::chooseLocalFile);
  connect(localModeButton_, &QToolButton::clicked, this,
          [this] { emit displayModeSelected(DisplayMode::Local); });
  connect(liveModeButton_, &QToolButton::clicked, this,
          [this] { emit displayModeSelected(DisplayMode::Live); });
  connect(webModeButton_, &QToolButton::clicked, this,
          [this] { emit displayModeSelected(DisplayMode::Web); });
  connect(playlistKindTabs_, &QTabBar::currentChanged, this,
          [this](const int kindIndex) {
            showPlaylistKind(kindIndex);
            emit displayModeSelected(kindIndex == 1 ? DisplayMode::Live
                                                     : DisplayMode::Local);
          });
  const auto requestLivePlaylistLoad = [this] {
    if (isLivePlaylistLoading_) {
      emit livePlaylistLoadCancelRequested();
      return;
    }
    emit livePlaylistLoadRequested(livePlaylistUrlEdit_->text().trimmed());
  };
  connect(livePlaylistLoadButton_, &QPushButton::clicked, this,
          requestLivePlaylistLoad);
  connect(livePlaylistUrlEdit_, &QLineEdit::returnPressed, this,
          requestLivePlaylistLoad);
  connect(livePlaylistSearchEdit_, &QLineEdit::textChanged, this,
          &MainWindow::applyLivePlaylistFilter);
  connect(livePlaylistHistoryButton_, &QToolButton::clicked, this,
          &MainWindow::showLiveUrlHistory);
  connect(livePlaylistLocateButton_, &QPushButton::clicked, this, [this] {
    if (!isLivePlaylistActive_ || currentLivePlaybackIndex_ < 0) {
      return;
    }
    if (playlistView_->isRowHidden(currentLivePlaybackIndex_)) {
      livePlaylistSearchEdit_->clear();
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
  connect(muteShortcut, &QShortcut::activated, this,
          [this] { emit muteStateRequested(true); });
  connect(unmuteShortcut, &QShortcut::activated, this,
          [this] { emit muteStateRequested(false); });
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
  connect(fullScreenShortcut, &QShortcut::activated, this,
          &MainWindow::toggleFullScreen);
  connect(exitFullScreenAction, &QAction::triggered, this,
          &MainWindow::exitFullScreen);
  connect(videoOutput_, &VideoOutputWidget::surfaceReady, this,
          &MainWindow::videoSurfaceReady);
  connect(exitAction, &QAction::triggered, this, &MainWindow::close);

  nativePlaybackShortcuts_ = {
      playbackShortcut, networkRefreshShortcut, volumeUpShortcut,
      volumeDownShortcut, muteShortcut, unmuteShortcut, seekBackwardShortcut,
      previousShortcut, nextShortcut, fullScreenShortcut};

  PlayerViewState initialState;
  initialState.mediaName = QStringLiteral("未选择媒体");
  initialState.statusText = QStringLiteral("未打开媒体");
  applyViewState(initialState);
  showDisplayMode(DisplayMode::Local);
  updatePlaylistResponsiveStyle();
}

MainWindow::~MainWindow() {
  delete browserPage_;
  browserPage_ = nullptr;
}

void MainWindow::showDisplayMode(const DisplayMode mode) {
  const bool leavesWeb = displayMode_ == DisplayMode::Web &&
                         mode != DisplayMode::Web;
  if (leavesWeb) {
    browserPage_->deactivate();
  }

  displayMode_ = mode;
  const bool showsWeb = mode == DisplayMode::Web;
  displayModeStack_->setCurrentWidget(showsWeb
                                          ? static_cast<QWidget*>(browserPage_)
                                          : nativePlaybackPage_);
  localModeButton_->setChecked(mode == DisplayMode::Local);
  liveModeButton_->setChecked(mode == DisplayMode::Live);
  webModeButton_->setChecked(showsWeb);
  // Ctrl+L 在网页模式交给地址栏，避免与“打开网络地址”菜单动作冲突。
  if (openNetworkAction_ != nullptr) {
    openNetworkAction_->setEnabled(!showsWeb);
  }
  for (QShortcut* const shortcut : nativePlaybackShortcuts_) {
    shortcut->setEnabled(!showsWeb);
  }

  if (showsWeb) {
    browserPage_->activate();
  } else {
    showPlaylistKind(mode == DisplayMode::Live ? 1 : 0);
  }
}

void MainWindow::applyPresentationMode(const UiPresentationMode mode) {
  if (presentationMode_.has_value() && *presentationMode_ == mode) {
    return;
  }
  presentationMode_ = mode;
  const QString modeKey = presentationModeKey(mode);

  QList<QWidget*> themedWidgets = findChildren<QWidget*>();
  themedWidgets.push_front(this);
  for (auto* const widget : themedWidgets) {
    widget->setProperty("themeMode", modeKey);
  }
  switch (mode) {
    case UiPresentationMode::LocalAudio:
      eyebrowLabel_->setText(QStringLiteral("LOCAL MUSIC / LISTEN"));
      titleLabel_->setText(QStringLiteral("沉浸音乐"));
      subtitleLabel_->setText(
          QStringLiteral("让波形、歌词与播放列表围绕正在播放的声音展开。"));
      modeBadgeLabel_->setText(QStringLiteral("MUSIC"));
      break;
    case UiPresentationMode::LocalVideo:
      eyebrowLabel_->setText(QStringLiteral("LOCAL VIDEO / CINEMA"));
      titleLabel_->setText(QStringLiteral("沉浸播放"));
      subtitleLabel_->setText(
          QStringLiteral("深色影院画布聚焦本地视频，控制与信息保持在手边。"));
      modeBadgeLabel_->setText(QStringLiteral("VIDEO"));
      break;
    case UiPresentationMode::Live:
      eyebrowLabel_->setText(QStringLiteral("LIVE STREAM / CONTROL"));
      titleLabel_->setText(QStringLiteral("直播控制台"));
      subtitleLabel_->setText(
          QStringLiteral("紧凑查看频道、连接状态与当前直播，快速刷新或切换。"));
      modeBadgeLabel_->setText(QStringLiteral("LIVE"));
      break;
  }
  videoOutput_->setPresentationMode(mode);
  setNativeDarkTitleBar(this, mode != UiPresentationMode::LocalAudio);

  // 动态属性只影响视觉；重新抛光现有控件，不重建对象或信号连接。
  for (auto* const widget : themedWidgets) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }
}

void MainWindow::applyViewState(const PlayerViewState& viewState) {
  const QSignalBlocker progressBlocker(progressSlider_);
  const QSignalBlocker volumeBlocker(volumeSlider_);
  const QSignalBlocker lyricsBlocker(lyricsButton_);
  applyPresentationMode(presentationModeFor(viewState));
  openAction_->setEnabled(viewState.canOpen);
  openNetworkAction_->setEnabled(viewState.canOpen &&
                                 displayMode_ != DisplayMode::Web);
  showPlaylistKind(viewState.isLivePlaylistActive ? 1 : 0);
  openButton_->setEnabled(viewState.canOpen);
  isLivePlaylistLoading_ = viewState.isLivePlaylistLoading;
  livePlaylistUrlEdit_->setEnabled(!viewState.isLivePlaylistLoading);
  livePlaylistHistoryButton_->setEnabled(!viewState.isLivePlaylistLoading);
  livePlaylistLoadButton_->setEnabled(true);
  livePlaylistLoadButton_->setText(viewState.isLivePlaylistLoading
                                       ? QStringLiteral("取消载入")
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
  if (livePlaylistModel_ != nullptr) {
    connect(livePlaylistModel_, &QAbstractItemModel::modelReset, this,
            &MainWindow::applyLivePlaylistFilter, Qt::UniqueConnection);
  }
  showPlaylistKind(isLivePlaylistActive_ ? 1 : 0);
}

void MainWindow::showPlaylistKind(const int kindIndex) {
  const bool showsLivePlaylist = kindIndex == 1;
  if (!showsLivePlaylist && isLivePlaylistActive_ &&
      playlistView_->model() == livePlaylistModel_) {
    for (int row = 0; row < playlistView_->model()->rowCount(); ++row) {
      playlistView_->setRowHidden(row, false);
    }
  }
  isLivePlaylistActive_ = showsLivePlaylist;
  if (displayMode_ != DisplayMode::Web) {
    displayMode_ = showsLivePlaylist ? DisplayMode::Live : DisplayMode::Local;
    localModeButton_->setChecked(!showsLivePlaylist);
    liveModeButton_->setChecked(showsLivePlaylist);
  }
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
  playlistTitleLabel_->setText(showsLivePlaylist
                                   ? QStringLiteral("直播频道")
                                   : QStringLiteral("播放列表"));
  livePlaylistTools_->setVisible(showsLivePlaylist);
  livePlaylistUrlEdit_->setVisible(showsLivePlaylist);
  livePlaylistHistoryButton_->setVisible(showsLivePlaylist);
  livePlaylistLoadButton_->setVisible(showsLivePlaylist);
  livePlaylistLocateButton_->setVisible(showsLivePlaylist);
  livePlaylistStatusLabel_->setVisible(showsLivePlaylist);
  livePlaylistSearchEdit_->setVisible(showsLivePlaylist);
  openButton_->setVisible(!showsLivePlaylist);
  if (showsLivePlaylist) {
    applyLivePlaylistFilter();
  }
}

void MainWindow::applyLivePlaylistFilter() {
  if (!isLivePlaylistActive_ || playlistView_->model() == nullptr ||
      playlistView_->model() != livePlaylistModel_) {
    return;
  }
  const QString query = livePlaylistSearchEdit_->text().trimmed();
  for (int row = 0; row < livePlaylistModel_->rowCount(); ++row) {
    const QString displayName =
        livePlaylistModel_->index(row, 0).data(Qt::UserRole).toString();
    playlistView_->setRowHidden(
        row, !query.isEmpty() &&
                 !displayName.contains(query, Qt::CaseInsensitive));
  }
}

void MainWindow::closeEvent(QCloseEvent* const event) {
  if (browserPage_ != nullptr) {
    browserPage_->shutdown();
  }
  emit closing();
  QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* const event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange) {
    updateFullScreenText();
  }
}

void MainWindow::resizeEvent(QResizeEvent* const event) {
  QMainWindow::resizeEvent(event);
  // 主窗口保留高度门槛保护播放器控制行；网页页按自身可用宽度独立分档。
  updatePlaylistResponsiveStyle();
}

void MainWindow::updatePlaylistResponsiveStyle() {
  if (playlistPanel_ == nullptr) {
    return;
  }

  const int responsiveWidth = std::clamp(
      width() * 3 / 10, kPlaylistMinimumWidth, kPlaylistMaximumWidth);
  if (playlistPanel_->width() != responsiveWidth) {
    playlistPanel_->setFixedWidth(responsiveWidth);
  }

  QString sizeKey;
  if (width() >= 1600 && height() >= 900) {
    sizeKey = QStringLiteral("extraLarge");
  } else if (width() >= 1200 && height() >= 800) {
    sizeKey = QStringLiteral("large");
  } else if (width() >= 900 && height() >= 700) {
    sizeKey = QStringLiteral("normal");
  } else {
    sizeKey = QStringLiteral("compact");
  }
  if (playlistResponsiveSize_ == sizeKey) {
    return;
  }
  playlistResponsiveSize_ = sizeKey;

  QList<QWidget*> playlistWidgets = playlistPanel_->findChildren<QWidget*>();
  playlistWidgets.push_front(playlistPanel_);
  for (auto* const widget : playlistWidgets) {
    widget->setProperty("responsiveSize", sizeKey);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->updateGeometry();
    widget->update();
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
  const auto* const watchedWidget = qobject_cast<QWidget*>(watched);
  if (watchedWidget == nullptr || watchedWidget->window() != this) {
    return QMainWindow::eventFilter(watched, event);
  }
  if (displayMode_ == DisplayMode::Web && browserPage_ != nullptr &&
      (watchedWidget == browserPage_ ||
       browserPage_->isAncestorOf(watchedWidget))) {
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
          "媒体与直播清单 (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.mp4 "
          "*.mkv *.avi *.mov *.webm *.m3u *.m3u8);;所有文件 (*.*)"));
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

QString MainWindow::livePlaylistUrl() const {
  return livePlaylistUrlEdit_->text().trimmed();
}

void MainWindow::setLivePlaylistHistoryUrls(const QStringList& urls) {
  livePlaylistHistoryUrls_ = urls;
}

void MainWindow::setLiveSourceMemos(
    const QVector<LiveSourceMemo>& memos) {
  liveSourceMemos_ = memos;
}

void MainWindow::setRecentLocalMedia(
    const QVector<RecentLocalMediaItem>& items) {
  recentLocalMediaMenu_->clear();
  if (items.isEmpty()) {
    auto* const emptyAction =
        recentLocalMediaMenu_->addAction(QStringLiteral("暂无记录"));
    emptyAction->setObjectName(QStringLiteral("emptyRecentLocalMediaAction"));
    emptyAction->setEnabled(false);
  } else {
    for (int index = 0; index < items.size(); ++index) {
      const RecentLocalMediaItem& item = items.at(index);
      auto* const action = recentLocalMediaMenu_->addAction(item.label);
      action->setObjectName(
          QStringLiteral("recentLocalMediaAction%1").arg(index));
      connect(action, &QAction::triggered, this,
              [this, filePath = item.filePath] {
                emit recentLocalMediaSelected(filePath);
              });
    }
    recentLocalMediaMenu_->addSeparator();
  }
  auto* const clearAction =
      recentLocalMediaMenu_->addAction(QStringLiteral("清空最近播放"));
  clearAction->setObjectName(QStringLiteral("clearRecentLocalMediaAction"));
  clearAction->setEnabled(!items.isEmpty());
  connect(clearAction, &QAction::triggered, this,
          &MainWindow::recentLocalMediaClearRequested);
}

void* MainWindow::prepareVideoSurface() {
  return videoOutput_->beginVideoSurfaceSession();
}

void MainWindow::releaseVideoSurface(void* const nativeHandle) {
  videoOutput_->releaseVideoSurface(nativeHandle);
}

void MainWindow::showLiveUrlHistory() {
  LiveUrlHistoryDialog dialog(livePlaylistHistoryUrls_, this);
  const int result = dialog.exec();
  const QStringList& updatedHistory = dialog.historyUrls();
  bool historyChanged = updatedHistory.size() != livePlaylistHistoryUrls_.size();
  for (int index = 0; !historyChanged && index < updatedHistory.size();
       ++index) {
    historyChanged = updatedHistory.at(index) != livePlaylistHistoryUrls_.at(index);
  }
  if (historyChanged) {
    livePlaylistHistoryUrls_ = dialog.historyUrls();
    emit livePlaylistHistoryChanged(livePlaylistHistoryUrls_);
  }
  if (result == QDialog::Accepted && !dialog.selectedUrl().isEmpty()) {
    livePlaylistUrlEdit_->setText(dialog.selectedUrl());
    livePlaylistUrlEdit_->setFocus();
  }
}

void MainWindow::showLiveSourceMemo() {
  LiveSourceMemoDialog dialog(liveSourceMemos_, this);
  connect(&dialog, &LiveSourceMemoDialog::memosSaved, this,
          [this](const QVector<LiveSourceMemo>& memos) {
            liveSourceMemos_ = memos;
            emit liveSourceMemosChanged(liveSourceMemos_);
          });
  dialog.exec();
}

void MainWindow::showShortcutHelp() {
  ShortcutHelpDialog dialog(this);
  dialog.exec();
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
  if (displayMode_ == DisplayMode::Web && browserPage_ != nullptr &&
      browserPage_->isWebFullScreen()) {
    browserPage_->exitWebFullScreen();
    return;
  }
  if (isFullScreen()) {
    showNormal();
    updateFullScreenText();
  }
}

void MainWindow::handleWebFullScreenChanged(const bool isFullScreen) {
  if (isFullScreen) {
    if (!webFullScreenPreviousWindowState_.has_value()) {
      webFullScreenPreviousWindowState_ = windowState();
    }
    if (!this->isFullScreen()) {
      showFullScreen();
    }
  } else if (webFullScreenPreviousWindowState_.has_value()) {
    const Qt::WindowStates previousState = *webFullScreenPreviousWindowState_;
    webFullScreenPreviousWindowState_.reset();
    setWindowState(previousState);
  }
  updateFullScreenText();
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
