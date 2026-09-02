#include "main_window.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFont>
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
#include "native_window_theme.h"
#include "playlist_model.h"
#include "seek_slider.h"
#include "shortcut_help_dialog.h"
#include "theme_background_widget.h"
#include "theme_settings_dialog.h"
#include "video_output_widget.h"

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
  void findInPage(const QString&, bool) override {}
  void stopFinding(bool) override {}
  void setBounds(const QRect&) override {}
  void setVisible(bool) override {}
  void setAudioMuted(bool) override {}
  void setTabAudioMuted(std::uint64_t, bool) override {}
  void setTabZoomFactor(std::uint64_t, double) override {}
  void setSuspended(bool) override {}
  void clearBrowsingData(std::uint64_t) override {}
  void answerPermission(std::uint64_t, BrowserPermissionDecision) override {}
  void chooseDownloadPath(std::uint64_t, const QString&) override {}
  void cancelDownload(std::uint64_t) override {}
  void answerExternalProtocol(std::uint64_t, bool) override {}
  void answerCertificateError(std::uint64_t,
                              BrowserCertificateDecision) override {}
  void exitFullScreen() override {}
  void shutdown() noexcept override { listener_ = nullptr; }

 private:
  BrowserEventListener* listener_{nullptr};
};

constexpr int kNormalHorizontalMargin = 16;
constexpr int kNormalVerticalMargin = 12;
constexpr int kNormalSpacing = 10;
constexpr int kPlaylistMinimumWidth = 280;
constexpr int kPlaylistMaximumWidth = 390;
constexpr int kKeyboardVolumeStep = 5;
constexpr int kDefaultKeyboardSeekStepSeconds = 5;
constexpr int kRightKeyHoldThresholdMilliseconds = 350;
constexpr QSize kMiniPlayerSize{600, 400};
constexpr QSize kMiniPlayerMinimumSize{480, 320};
constexpr std::array<double, 6> kPlaybackRates{0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
constexpr std::array<int, 4> kSeekSteps{5, 10, 15, 20};

enum class ControlIcon {
  File,
  View,
  Help,
  Theme,
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
  MiniPlayer,
  ExitMiniPlayer,
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

QIcon controlIcon(
    const ControlIcon icon,
    const QColor& ink = QColor(QStringLiteral("#d5d9df"))) {
  QPixmap pixmap(20, 20);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
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
    case ControlIcon::File: {
      QPainterPath page;
      page.moveTo(5, 2.5);
      page.lineTo(12.5, 2.5);
      page.lineTo(16, 6);
      page.lineTo(16, 17.5);
      page.lineTo(5, 17.5);
      page.closeSubpath();
      painter.drawPath(page);
      painter.drawLine(QPointF(12.5, 2.5), QPointF(12.5, 6));
      painter.drawLine(QPointF(12.5, 6), QPointF(16, 6));
      painter.drawLine(QPointF(7.5, 10), QPointF(13.5, 10));
      painter.drawLine(QPointF(7.5, 13), QPointF(12, 13));
      break;
    }
    case ControlIcon::View: {
      QPainterPath eye;
      eye.moveTo(2.5, 10);
      eye.cubicTo(6, 4.5, 14, 4.5, 17.5, 10);
      eye.cubicTo(14, 15.5, 6, 15.5, 2.5, 10);
      painter.drawPath(eye);
      painter.setBrush(ink);
      painter.drawEllipse(QPointF(10, 10), 2.2, 2.2);
      break;
    }
    case ControlIcon::Help:
      painter.drawEllipse(QRectF(3, 3, 14, 14));
      painter.drawArc(QRectF(7, 5.5, 6, 6), 15 * 16, 205 * 16);
      painter.drawLine(QPointF(10, 11), QPointF(10, 12.5));
      painter.setBrush(ink);
      painter.drawEllipse(QPointF(10, 15), 1.0, 1.0);
      break;
    case ControlIcon::Theme:
      painter.drawEllipse(QRectF(3, 3, 14, 14));
      painter.setBrush(ink);
      painter.drawEllipse(QPointF(7, 8), 1.2, 1.2);
      painter.drawEllipse(QPointF(10.5, 6.5), 1.2, 1.2);
      painter.drawEllipse(QPointF(13.5, 9), 1.2, 1.2);
      painter.setBrush(Qt::NoBrush);
      painter.drawArc(QRectF(7, 10, 7, 5), 195 * 16, 145 * 16);
      break;
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
    case ControlIcon::MiniPlayer:
      painter.drawRoundedRect(QRectF(3, 3.5, 14, 12), 1.5, 1.5);
      painter.setBrush(ink);
      painter.drawRoundedRect(QRectF(10, 10, 6, 4.5), 1, 1);
      break;
    case ControlIcon::ExitMiniPlayer:
      painter.drawRoundedRect(QRectF(3, 4, 14, 12), 1.5, 1.5);
      painter.drawLine(QPointF(7, 9), QPointF(3, 5));
      painter.drawLine(QPointF(7, 9), QPointF(3, 9));
      painter.drawLine(QPointF(13, 11), QPointF(17, 15));
      painter.drawLine(QPointF(13, 11), QPointF(17, 11));
      break;
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

void configureChromeButton(QToolButton* const button,
                           const QString& accessibleName,
                           const QString& toolTip, const ControlIcon icon) {
  button->setAccessibleName(accessibleName);
  button->setToolTip(toolTip);
  button->setProperty("topChromeButton", true);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setIconSize(QSize(20, 20));
  button->setIcon(controlIcon(icon));
  button->setCursor(Qt::PointingHandCursor);
  button->setFixedSize(32, 32);
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
  void enterEvent(QEnterEvent* const event) override {
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

void selectPlaybackMode(
    QToolButton* const button, const int modeIndex,
    const QColor& iconColor = QColor(QStringLiteral("#d5d9df"))) {
  button->setProperty("playbackModeIndex", modeIndex);
  button->setIcon(controlIcon(playbackModeIcon(modeIndex), iconColor));
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

MainWindow::MainWindow(BrowserBackend* const browserBackend,
                       QString browserProfileDirectory, QWidget* const parent,
                       BrowserDataStore* const browserDataStore,
                       BrowserSessionStore* const browserSessionStore,
                       BrowserStartupSettingsStore* const browserStartupSettingsStore,
                       BrowserPermissionStore* const browserPermissionStore)
    : QMainWindow(parent), windowIconManager_(this) {
  // 固定 Qt5 时期的 Windows 字体基线，避免 Qt6 平台字体度量放大整页。
  QFont compactFont = font();
  compactFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
  compactFont.setPixelSize(12);
  setFont(compactFont);
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

  fileMenu_ = new QMenu(QStringLiteral("文件"), this);
  fileMenu_->setObjectName(QStringLiteral("topFileMenu"));
  openAction_ = fileMenu_->addAction(QStringLiteral("打开媒体文件(&O)..."));
  openAction_->setObjectName(QStringLiteral("openFileAction"));
  openAction_->setShortcut(QKeySequence::Open);
  openNetworkAction_ =
      fileMenu_->addAction(QStringLiteral("打开网络地址(&N)..."));
  openNetworkAction_->setObjectName(QStringLiteral("openNetworkAction"));
  openNetworkAction_->setShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::Key_L)));
  recentLocalMediaMenu_ =
      fileMenu_->addMenu(QStringLiteral("最近播放(&R)"));
  recentLocalMediaMenu_->setObjectName(
      QStringLiteral("recentLocalMediaMenu"));
  setRecentLocalMedia({});
  fileMenu_->addSeparator();
  auto* const exitAction = fileMenu_->addAction(QStringLiteral("退出(&X)"));
  exitAction->setShortcut(QKeySequence::Quit);
  viewMenu_ = new QMenu(QStringLiteral("视图"), this);
  viewMenu_->setObjectName(QStringLiteral("topViewMenu"));
  fullScreenAction_ = viewMenu_->addAction(QStringLiteral("进入全屏(&F)"));
  fullScreenAction_->setObjectName(QStringLiteral("fullScreenAction"));
  auto* const fullScreenShortcut =
      new QShortcut(QKeySequence(Qt::Key_F11), this);
  fullScreenShortcut->setObjectName(QStringLiteral("fullScreenShortcut"));
  fullScreenShortcut->setContext(Qt::WindowShortcut);
  fullScreenShortcut->setAutoRepeat(false);
  auto* const exitFullScreenAction = new QAction(this);
  exitFullScreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(exitFullScreenAction);
  helpMenu_ = new QMenu(QStringLiteral("帮助"), this);
  helpMenu_->setObjectName(QStringLiteral("topHelpMenu"));
  auto* const shortcutHelpAction =
      helpMenu_->addAction(QStringLiteral("快捷键(&K)..."));
  shortcutHelpAction->setObjectName(QStringLiteral("shortcutHelpAction"));
  connect(shortcutHelpAction, &QAction::triggered, this,
          &MainWindow::showShortcutHelp);
  auto* const liveSourceMemoAction =
      helpMenu_->addAction(QStringLiteral("直播源(&L)..."));
  liveSourceMemoAction->setObjectName(
      QStringLiteral("liveSourceMemoAction"));
  liveSourceMemoAction->setShortcut(
      QKeySequence(static_cast<int>(Qt::CTRL) |
                   static_cast<int>(Qt::Key_M)));
  liveSourceMemoAction->setShortcutContext(Qt::WindowShortcut);
  connect(liveSourceMemoAction, &QAction::triggered, this,
          &MainWindow::showLiveSourceMemo);
  for (QAction* const action : {openAction_, openNetworkAction_, exitAction,
                                fullScreenAction_, shortcutHelpAction,
                                liveSourceMemoAction}) {
    addAction(action);
  }
  menuBar()->hide();

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
  themeBackground_ = new ThemeBackgroundWidget(this);
  centralSurface_ = themeBackground_;
  centralSurface_->setObjectName(QStringLiteral("centralSurface"));
  auto* const outerLayout = new QVBoxLayout(centralSurface_);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);

  displayModePanel_ = new QFrame(centralSurface_);
  displayModePanel_->setObjectName(QStringLiteral("displayModePanel"));
  displayModePanel_->setFixedHeight(52);
  auto* const displayModeLayout = new QHBoxLayout(displayModePanel_);
  displayModeLayout->setContentsMargins(16, 8, 16, 8);
  displayModeLayout->setSpacing(18);
  auto* const brandLabel = new QLabel(QStringLiteral("MediaHub"),
                                      displayModePanel_);
  brandLabel->setObjectName(QStringLiteral("brandLabel"));
  auto* const displayModeRail = new QFrame(displayModePanel_);
  displayModeRail->setObjectName(QStringLiteral("displayModeRail"));
  auto* const displayModeRailLayout = new QHBoxLayout(displayModeRail);
  displayModeRailLayout->setContentsMargins(0, 0, 0, 0);
  displayModeRailLayout->setSpacing(4);
  const auto makeModeButton = [displayModeRail](const QString& objectName,
                                                const QString& text) {
    auto* const button = new QToolButton(displayModeRail);
    button->setObjectName(objectName);
    button->setText(text);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setProperty("modeSegment", true);
    button->setAccessibleName(QStringLiteral("%1模式").arg(text));
    button->setToolTip(QStringLiteral("切换到%1模式").arg(text));
    button->setFixedSize(88, 32);
    return button;
  };
  localModeButton_ =
      makeModeButton(QStringLiteral("localModeButton"), QStringLiteral("本地"));
  liveModeButton_ =
      makeModeButton(QStringLiteral("liveModeButton"), QStringLiteral("直播"));
  webModeButton_ =
      makeModeButton(QStringLiteral("webModeButton"), QStringLiteral("网页"));
  updateWebAudibleTabCount(0);
  displayModeRailLayout->addWidget(localModeButton_);
  displayModeRailLayout->addWidget(liveModeButton_);
  displayModeRailLayout->addWidget(webModeButton_);
  displayModeLayout->addWidget(brandLabel);
  displayModeLayout->addWidget(displayModeRail);
  displayModeLayout->addStretch(1);
  auto* const topActions = new QWidget(displayModePanel_);
  auto* const topActionsLayout = new QHBoxLayout(topActions);
  topActionsLayout->setContentsMargins(0, 0, 0, 0);
  topActionsLayout->setSpacing(4);

  fileMenuButton_ = new QToolButton(displayModePanel_);
  fileMenuButton_->setObjectName(QStringLiteral("fileMenuButton"));
  configureChromeButton(fileMenuButton_, QStringLiteral("文件菜单"),
                        QStringLiteral("文件"), ControlIcon::File);
  fileMenuButton_->setMenu(fileMenu_);
  fileMenuButton_->setPopupMode(QToolButton::InstantPopup);
  topActionsLayout->addWidget(fileMenuButton_);

  viewMenuButton_ = new QToolButton(displayModePanel_);
  viewMenuButton_->setObjectName(QStringLiteral("viewMenuButton"));
  configureChromeButton(viewMenuButton_, QStringLiteral("视图菜单"),
                        QStringLiteral("视图"), ControlIcon::View);
  viewMenuButton_->setMenu(viewMenu_);
  viewMenuButton_->setPopupMode(QToolButton::InstantPopup);
  topActionsLayout->addWidget(viewMenuButton_);

  helpMenuButton_ = new QToolButton(displayModePanel_);
  helpMenuButton_->setObjectName(QStringLiteral("helpMenuButton"));
  configureChromeButton(helpMenuButton_, QStringLiteral("帮助菜单"),
                        QStringLiteral("帮助"), ControlIcon::Help);
  helpMenuButton_->setMenu(helpMenu_);
  helpMenuButton_->setPopupMode(QToolButton::InstantPopup);
  topActionsLayout->addWidget(helpMenuButton_);

  themeButton_ = new QToolButton(displayModePanel_);
  themeButton_->setObjectName(QStringLiteral("themeButton"));
  configureChromeButton(themeButton_, QStringLiteral("个性化主题"),
                        QStringLiteral("设置外观、配色和背景图片"),
                        ControlIcon::Theme);
  topActionsLayout->addWidget(themeButton_);
  displayModeLayout->addWidget(topActions);
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
  headerLayout->setContentsMargins(2, 2, 2, 4);
  headerLayout->setSpacing(12);
  auto* const headerCopyLayout = new QVBoxLayout();
  headerCopyLayout->setContentsMargins(0, 0, 0, 0);
  headerCopyLayout->setSpacing(1);
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
  eyebrowLabel_->hide();
  modeBadgeLabel_->hide();
  headerLayout->addLayout(headerCopyLayout, 1);
  headerLayout->addWidget(modeBadgeLabel_, 0, Qt::AlignVCenter);
  rootLayout_->addWidget(headerPanel_);

  auto* const mediaWorkspace = new QHBoxLayout();
  mediaWorkspace->setSpacing(8);
  mediaDisplay_ = new QWidget(centralWidget);
  auto* const mediaDisplay = mediaDisplay_;
  mediaDisplay->setObjectName(QStringLiteral("mediaDisplay"));
  mediaDisplayStack_ = new QStackedLayout(mediaDisplay);
  mediaDisplayStack_->setContentsMargins(0, 0, 0, 0);
  videoOutput_ = new VideoOutputWidget(mediaDisplay);
  videoOutput_->setThemeBackgroundSource(themeBackground_);
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
  playlistToggleButton_->setFixedSize(24, 48);
  mediaWorkspace->addWidget(playlistToggleButton_, 0, Qt::AlignVCenter);

  playlistPanel_ = new QFrame(centralWidget);
  playlistPanel_->setObjectName(QStringLiteral("playlistPanel"));
  playlistPanel_->setFixedWidth(kPlaylistMinimumWidth);
  auto* const playlistLayout = new QVBoxLayout(playlistPanel_);
  playlistLayout->setContentsMargins(14, 12, 14, 12);
  playlistLayout->setSpacing(8);
  auto* const playlistHeaderRow = new QHBoxLayout();
  playlistHeaderRow->setSpacing(8);
  playlistTitleLabel_ =
      new QLabel(QStringLiteral("播放列表"), playlistPanel_);
  playlistTitleLabel_->setObjectName(QStringLiteral("playlistTitleLabel"));
  playlistHeaderRow->addWidget(playlistTitleLabel_);
  playlistHeaderRow->addStretch(1);
  playlistKindTabs_ = new QTabBar(playlistPanel_);
  playlistKindTabs_->setObjectName(QStringLiteral("playlistKindTabs"));
  playlistKindTabs_->setAccessibleName(QStringLiteral("播放列表类型"));
  playlistKindTabs_->setDocumentMode(true);
  playlistKindTabs_->setExpanding(true);
  playlistKindTabs_->setUsesScrollButtons(false);
  playlistKindTabs_->setElideMode(Qt::ElideNone);
  playlistKindTabs_->addTab(QStringLiteral("本地列表"));
  playlistKindTabs_->addTab(QStringLiteral("直播列表"));
  playlistKindTabs_->hide();
  livePlaylistTools_ = new QFrame(playlistPanel_);
  livePlaylistTools_->setObjectName(QStringLiteral("livePlaylistTools"));
  auto* const livePlaylistToolsLayout = new QVBoxLayout(livePlaylistTools_);
  livePlaylistToolsLayout->setContentsMargins(0, 4, 0, 0);
  livePlaylistToolsLayout->setSpacing(6);
  auto* const livePlaylistSourceLabel =
      new QLabel(QStringLiteral("播放源"), livePlaylistTools_);
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
      new QPushButton(QStringLiteral("载入清单"), livePlaylistTools_);
  livePlaylistLoadButton_->setObjectName(
      QStringLiteral("livePlaylistLoadButton"));
  livePlaylistLoadButton_->setProperty("primary", true);
  livePlaylistLocateButton_ =
      new QPushButton(QStringLiteral("定位当前频道"), livePlaylistTools_);
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
      QStringLiteral("搜索频道"));
  livePlaylistSearchEdit_->setClearButtonEnabled(true);
  livePlaylistScopeTabs_ = new QTabBar(playlistPanel_);
  livePlaylistScopeTabs_->setObjectName(
      QStringLiteral("livePlaylistScopeTabs"));
  livePlaylistScopeTabs_->setAccessibleName(QStringLiteral("直播列表范围"));
  livePlaylistScopeTabs_->setDocumentMode(true);
  livePlaylistScopeTabs_->setExpanding(true);
  livePlaylistScopeTabs_->setUsesScrollButtons(false);
  livePlaylistScopeTabs_->addTab(QStringLiteral("全部直播"));
  livePlaylistScopeTabs_->addTab(QStringLiteral("我的收藏"));
  playlistView_ = new QListView(playlistPanel_);
  playlistView_->setObjectName(QStringLiteral("playlistView"));
  playlistView_->setAccessibleName(QStringLiteral("播放列表"));
  playlistView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  playlistView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  playlistView_->setContextMenuPolicy(Qt::CustomContextMenu);
  playlistView_->viewport()->installEventFilter(this);
  openButton_ = new QPushButton(QStringLiteral("添加媒体"), playlistPanel_);
  openButton_->setObjectName(QStringLiteral("openFileButton"));
  openButton_->setProperty("compactAction", true);
  playlistHeaderRow->addWidget(openButton_);
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

  playlistLayout->addLayout(playlistHeaderRow);
  playlistLayout->addWidget(playlistKindTabs_);
  playlistLayout->addWidget(livePlaylistTools_);
  playlistLayout->addWidget(livePlaylistScopeTabs_);
  playlistLayout->addWidget(livePlaylistSearchEdit_);
  playlistLayout->addWidget(playlistView_, 1);
  mediaWorkspace->addWidget(playlistPanel_, 1);
  rootLayout_->addLayout(mediaWorkspace, 1);

  auto* const playerDock = new QFrame(centralWidget);
  playerDock->setObjectName(QStringLiteral("playerDock"));
  auto* const playerDockLayout = new QVBoxLayout(playerDock);
  playerDockLayout->setContentsMargins(0, 0, 0, 0);
  playerDockLayout->setSpacing(0);

  mediaCard_ = new QFrame(playerDock);
  auto* const mediaCard = mediaCard_;
  mediaCard->setObjectName(QStringLiteral("mediaCard"));
  auto* const cardLayout = new QVBoxLayout(mediaCard);
  cardLayout->setContentsMargins(16, 8, 16, 7);
  cardLayout->setSpacing(4);

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
  mediaRow->setSpacing(10);
  mediaRow->addWidget(mediaCaption);
  mediaRow->addWidget(mediaNameLabel_, 1);
  mediaRow->addWidget(statusLabel_);
  cardLayout->addLayout(mediaRow);

  errorLabel_ = new QLabel(mediaCard);
  errorLabel_->setObjectName(QStringLiteral("playbackErrorLabel"));
  errorLabel_->setWordWrap(true);
  errorLabel_->hide();
  cardLayout->addWidget(errorLabel_);
  playerDockLayout->addWidget(mediaCard);

  transportPanel_ = new QFrame(playerDock);
  auto* const transportPanel = transportPanel_;
  transportPanel->setObjectName(QStringLiteral("transportPanel"));
  auto* const transportLayout = new QVBoxLayout(transportPanel);
  transportLayout->setContentsMargins(16, 7, 16, 10);
  transportLayout->setSpacing(6);

  auto* const progressCaption =
      new QLabel(QStringLiteral("播放进度"), transportPanel);
  progressCaption->setObjectName(QStringLiteral("transportCaptionLabel"));
  progressCaption->hide();
  positionLabel_ = new QLabel(QStringLiteral("00:00 / --:--"), transportPanel);
  positionLabel_->setObjectName(QStringLiteral("positionLabel"));

  progressSlider_ = new SeekSlider(Qt::Horizontal, transportPanel);
  progressSlider_->setObjectName(QStringLiteral("progressSlider"));
  progressSlider_->setAccessibleName(QStringLiteral("播放进度"));
  progressSlider_->setRange(0, kProgressMaximum);
  progressSlider_->setPageStep(50);
  progressSlider_->setEnabled(false);
  progressSlider_->setToolTip(
      QStringLiteral("可拖动或单击定位，左右方向键调整进度"));

  auto* const progressRow = new QHBoxLayout();
  progressRow->setSpacing(10);
  progressRow->addWidget(progressSlider_, 1);
  progressRow->addWidget(positionLabel_);
  transportLayout->addLayout(progressRow);

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
  playPauseButton_->setFixedSize(40, 40);
  playPauseButton_->setIconSize(QSize(22, 22));
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
  miniPlayerButton_ = new QToolButton(transportPanel);
  miniPlayerButton_->setObjectName(QStringLiteral("miniPlayerButton"));
  configureTransportButton(miniPlayerButton_, QStringLiteral("小窗口播放"),
                           QStringLiteral("切换到小窗口播放"),
                           ControlIcon::MiniPlayer);

  auto* const controlRow = new QHBoxLayout();
  controlRow->setSpacing(6);
  controlRow->addWidget(playbackModeButton_);
  controlRow->addStretch(1);
  controlRow->addWidget(previousButton_);
  controlRow->addWidget(playPauseButton_);
  controlRow->addWidget(nextButton_);
  controlRow->addWidget(stopButton_);
  controlRow->addWidget(networkRefreshButton_);
  controlRow->addStretch(1);
  controlRow->addWidget(volumeButton_);
  controlRow->addWidget(lyricsButton_);
  controlRow->addWidget(playbackRateButton_);
  controlRow->addWidget(keyboardSeekStepButton_);
  controlRow->addWidget(miniPlayerButton_);
  controlRow->addWidget(fullScreenButton_);
  transportLayout->addLayout(controlRow);
  playerDockLayout->addWidget(transportPanel);
  rootLayout_->addWidget(playerDock);

  browserPage_ = new BrowserPage(*browserBackend_,
                                 std::move(browserProfileDirectory),
                                 displayModeContainer, browserDataStore,
                                 browserSessionStore,
                                 browserStartupSettingsStore,
                                 browserPermissionStore);
  connect(browserPage_, &BrowserPage::fullScreenChanged, this,
          &MainWindow::handleWebFullScreenChanged);
  connect(browserPage_, &BrowserPage::audibleTabCountChanged, this,
          &MainWindow::updateWebAudibleTabCount);
  displayModeStack_->addWidget(nativePlaybackPage_);
  displayModeStack_->addWidget(browserPage_);
  displayModeStack_->setCurrentWidget(nativePlaybackPage_);
  outerLayout->addWidget(displayModeContainer, 1);

  fullScreenChrome_ = {displayModePanel_, headerPanel_, mediaCard_};
  miniPlayerHiddenControls_ = {
      playbackModeButton_, previousButton_, nextButton_, lyricsButton_,
      playbackRateButton_, keyboardSeekStepButton_, fullScreenButton_};

  setCentralWidget(centralSurface_);
  applyThemeSettings(themeSettings_);

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
  connect(themeButton_, &QToolButton::clicked, this,
          &MainWindow::showThemeSettings);
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
  connect(livePlaylistScopeTabs_, &QTabBar::currentChanged, this,
          &MainWindow::changeLivePlaylistScope);
  connect(livePlaylistHistoryButton_, &QToolButton::clicked, this,
          &MainWindow::showLiveUrlHistory);
  connect(livePlaylistLocateButton_, &QPushButton::clicked, this, [this] {
    if (!isLivePlaylistActive_ || currentLivePlaybackIndex_ < 0) {
      return;
    }
    if (playlistView_->isRowHidden(currentLivePlaybackIndex_)) {
      livePlaylistSearchEdit_->clear();
      livePlaylistScopeTabs_->setCurrentIndex(0);
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
  connect(miniPlayerButton_, &QToolButton::clicked, this,
          &MainWindow::toggleMiniPlayer);
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

void MainWindow::setThemeSettings(const ThemeSettings& settings) {
  applyThemeSettings(settings);
}

const ThemeSettings& MainWindow::themeSettings() const noexcept {
  return themeSettings_;
}

void MainWindow::showDisplayMode(const DisplayMode mode) {
  if (mode == DisplayMode::Web && isMiniPlayer_) {
    exitMiniPlayer();
  }
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

void MainWindow::updateWebAudibleTabCount(const int audibleTabCount) {
  audibleWebTabCount_ = std::max(0, audibleTabCount);
  if (audibleWebTabCount_ == 0) {
    webModeButton_->setText(QStringLiteral("网页"));
    webModeButton_->setToolTip(QStringLiteral("切换到网页模式"));
    return;
  }

  webModeButton_->setText(
      QStringLiteral("网页 · %1").arg(audibleWebTabCount_));
  webModeButton_->setToolTip(
      QStringLiteral("有 %1 个网页标签正在播放声音，点击切换到网页模式")
          .arg(audibleWebTabCount_));
}

void MainWindow::applyPresentationMode(const UiPresentationMode mode) {
  if (presentationMode_.has_value() && *presentationMode_ == mode) {
    return;
  }
  presentationMode_ = mode;
  const QString modeKey = presentationModeKey(mode);

  QList<QWidget*> themedWidgets = nativePlaybackPage_->findChildren<QWidget*>();
  themedWidgets.push_front(nativePlaybackPage_);
  themedWidgets.push_front(centralSurface_);
  themedWidgets.push_front(menuBar());
  themedWidgets.push_front(this);
  for (auto* const widget : themedWidgets) {
    widget->setProperty("themeMode", modeKey);
  }
  switch (mode) {
    case UiPresentationMode::LocalAudio:
      eyebrowLabel_->clear();
      titleLabel_->setText(QStringLiteral("本地媒体"));
      subtitleLabel_->setText(
          QStringLiteral("播放音频、查看歌词并管理当前队列"));
      modeBadgeLabel_->setText(QStringLiteral("音频"));
      break;
    case UiPresentationMode::LocalVideo:
      eyebrowLabel_->clear();
      titleLabel_->setText(QStringLiteral("本地媒体"));
      subtitleLabel_->setText(
          QStringLiteral("播放本地音视频并管理当前队列"));
      modeBadgeLabel_->setText(QStringLiteral("视频"));
      break;
    case UiPresentationMode::Live:
      eyebrowLabel_->clear();
      titleLabel_->setText(QStringLiteral("直播"));
      subtitleLabel_->setText(
          QStringLiteral("载入频道清单、切换直播源并查看连接状态"));
      modeBadgeLabel_->setText(QStringLiteral("直播"));
      break;
  }
  videoOutput_->setPresentationMode(mode);
  setNativeDarkTitleBar(
      this, themeSettings_.appearanceMode != QStringLiteral("light"));

  // 动态属性只影响视觉；重新抛光现有控件，不重建对象或信号连接。
  for (auto* const widget : themedWidgets) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }
}

void MainWindow::refreshControlIcons() {
  const UiThemePalette palette = resolvedThemePalette(themeSettings_);
  if (fileMenuButton_ != nullptr) {
    fileMenuButton_->setIcon(controlIcon(ControlIcon::File, controlIconColor_));
    viewMenuButton_->setIcon(controlIcon(ControlIcon::View, controlIconColor_));
    helpMenuButton_->setIcon(controlIcon(ControlIcon::Help, controlIconColor_));
    themeButton_->setIcon(controlIcon(ControlIcon::Theme, palette.accent));
  }
  if (previousButton_ == nullptr) {
    return;
  }

  previousButton_->setIcon(
      controlIcon(ControlIcon::Previous, controlIconColor_));
  nextButton_->setIcon(controlIcon(ControlIcon::Next, controlIconColor_));
  stopButton_->setIcon(controlIcon(ControlIcon::Stop, controlIconColor_));
  networkRefreshButton_->setIcon(
      controlIcon(ControlIcon::Refresh, controlIconColor_));
  const bool showsPause =
      playPauseButton_->accessibleName() == QStringLiteral("暂停");
  playPauseButton_->setIcon(
      controlIcon(showsPause ? ControlIcon::Pause : ControlIcon::Play,
                  QColor(QStringLiteral("#ffffff"))));
  const bool isMuted =
      volumeButton_->accessibleName() == QStringLiteral("已静音");
  volumeButton_->setIcon(
      controlIcon(isMuted ? ControlIcon::Muted : ControlIcon::Volume,
                  controlIconColor_));
  fullScreenButton_->setIcon(controlIcon(
      isFullScreen() ? ControlIcon::ExitFullScreen : ControlIcon::FullScreen,
      controlIconColor_));
  miniPlayerButton_->setIcon(controlIcon(
      isMiniPlayer_ ? ControlIcon::ExitMiniPlayer : ControlIcon::MiniPlayer,
      controlIconColor_));
  playlistToggleButton_->setIcon(
      controlIcon(isPlaylistExpanded_ ? ControlIcon::CollapseRight
                                      : ControlIcon::ExpandLeft,
                  controlIconColor_));
  const int modeIndex =
      playbackModeButton_->property("playbackModeIndex").toInt();
  selectPlaybackMode(playbackModeButton_, modeIndex, controlIconColor_);
  for (QAction* const action : playbackModeButton_->menu()->actions()) {
    if (action->isCheckable()) {
      action->setIcon(
          controlIcon(playbackModeIcon(action->data().toInt()),
                      controlIconColor_));
    }
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
                                       : QStringLiteral("载入清单"));
  livePlaylistStatusLabel_->setText(viewState.livePlaylistStatusText);
  playPauseButton_->setEnabled(viewState.canPlay || viewState.canPause);
  const bool showsPause = viewState.canPause;
  playPauseButton_->setIcon(
      controlIcon(showsPause ? ControlIcon::Pause : ControlIcon::Play,
                  QColor(QStringLiteral("#ffffff"))));
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
  selectPlaybackMode(playbackModeButton_, viewState.playbackModeIndex,
                     controlIconColor_);
  selectPlaybackRate(playbackRateButton_, viewState.isTemporaryFastPlayback
                                              ? 2.0
                                              : viewState.playbackRate);
  fullScreenAction_->setEnabled(viewState.canToggleFullscreen);
  fullScreenButton_->setEnabled(viewState.canToggleFullscreen);
  miniPlayerButton_->setEnabled(isMiniPlayer_ ||
                                viewState.canToggleFullscreen);
  mediaNameLabel_->setText(viewState.mediaName);
  statusLabel_->setText(viewState.statusText);
  positionLabel_->setText(viewState.positionText);
  volumeLabel_->setText(QStringLiteral("%1%").arg(viewState.volumeValue));
  volumeButton_->setIcon(
      controlIcon(viewState.isMuted ? ControlIcon::Muted : ControlIcon::Volume,
                  controlIconColor_));
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
    connect(livePlaylistModel_, &QAbstractItemModel::dataChanged, this,
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
                                   : QStringLiteral("本地队列"));
  livePlaylistTools_->setVisible(showsLivePlaylist);
  livePlaylistUrlEdit_->setVisible(showsLivePlaylist);
  livePlaylistHistoryButton_->setVisible(showsLivePlaylist);
  livePlaylistLoadButton_->setVisible(showsLivePlaylist);
  livePlaylistLocateButton_->setVisible(showsLivePlaylist);
  livePlaylistStatusLabel_->setVisible(showsLivePlaylist);
  livePlaylistScopeTabs_->setVisible(showsLivePlaylist);
  livePlaylistSearchEdit_->setVisible(showsLivePlaylist);
  openButton_->setVisible(!showsLivePlaylist);
  if (showsLivePlaylist) {
    applyLivePlaylistFilter();
  }
}

void MainWindow::changeLivePlaylistScope(const int scopeIndex) {
  if (scopeIndex < 0 ||
      scopeIndex >=
          static_cast<int>(livePlaylistScopeScrollPositions_.size())) {
    return;
  }

  livePlaylistScopeScrollPositions_[livePlaylistScopeIndex_] =
      playlistView_->verticalScrollBar()->value();
  livePlaylistScopeIndex_ = scopeIndex;
  applyLivePlaylistFilter();
  const int scrollPosition = livePlaylistScopeScrollPositions_[scopeIndex];
  playlistView_->verticalScrollBar()->setValue(scrollPosition);
  QTimer::singleShot(0, playlistView_, [this, scopeIndex, scrollPosition] {
    if (isLivePlaylistActive_ && livePlaylistScopeIndex_ == scopeIndex) {
      playlistView_->verticalScrollBar()->setValue(scrollPosition);
    }
  });
}

void MainWindow::applyLivePlaylistFilter() {
  if (!isLivePlaylistActive_ || playlistView_->model() == nullptr ||
      playlistView_->model() != livePlaylistModel_) {
    return;
  }
  const QString query = livePlaylistSearchEdit_->text().trimmed();
  const bool showsFavoritesOnly =
      livePlaylistScopeTabs_->currentIndex() == 1;
  for (int row = 0; row < livePlaylistModel_->rowCount(); ++row) {
    const QModelIndex index = livePlaylistModel_->index(row, 0);
    const QString displayName = index.data(Qt::UserRole).toString();
    const bool matchesSearch =
        query.isEmpty() ||
        displayName.contains(query, Qt::CaseInsensitive);
    const bool matchesScope =
        !showsFavoritesOnly ||
        index.data(PlaylistModel::kFavoriteRole).toBool();
    playlistView_->setRowHidden(row, !matchesSearch || !matchesScope);
  }
}

void MainWindow::closeEvent(QCloseEvent* const event) {
  if (!isDownloadExitConfirmed_ && browserPage_ != nullptr) {
    const int activeDownloadCount = browserPage_->activeDownloadCount();
    if (activeDownloadCount > 0) {
      event->ignore();
      showActiveDownloadExitConfirmation(activeDownloadCount);
      return;
    }
  }
  if (activeDownloadExitDialog_ != nullptr) {
    activeDownloadExitDialog_->hide();
  }
  if (browserPage_ != nullptr) {
    browserPage_->shutdown();
  }
  emit closing();
  QMainWindow::closeEvent(event);
}

void MainWindow::showActiveDownloadExitConfirmation(
    const int activeDownloadCount) {
  if (activeDownloadExitDialog_ == nullptr) {
    activeDownloadExitDialog_ = new QDialog(this);
    activeDownloadExitDialog_->setObjectName(
        QStringLiteral("browserActiveDownloadExitDialog"));
    activeDownloadExitDialog_->setWindowTitle(QStringLiteral("下载仍在进行"));
    activeDownloadExitDialog_->setModal(false);
    auto* const layout = new QVBoxLayout(activeDownloadExitDialog_);
    activeDownloadExitLabel_ = new QLabel(activeDownloadExitDialog_);
    activeDownloadExitLabel_->setObjectName(
        QStringLiteral("browserActiveDownloadExitLabel"));
    activeDownloadExitLabel_->setWordWrap(true);
    layout->addWidget(activeDownloadExitLabel_);
    auto* const buttons = new QHBoxLayout();
    auto* const returnButton = new QPushButton(
        QStringLiteral("返回应用"), activeDownloadExitDialog_);
    returnButton->setObjectName(
        QStringLiteral("browserActiveDownloadReturnButton"));
    auto* const exitButton = new QPushButton(
        QStringLiteral("取消下载并退出"), activeDownloadExitDialog_);
    exitButton->setObjectName(
        QStringLiteral("browserActiveDownloadExitButton"));
    buttons->addStretch();
    buttons->addWidget(returnButton);
    buttons->addWidget(exitButton);
    layout->addLayout(buttons);
    connect(returnButton, &QPushButton::clicked, activeDownloadExitDialog_,
            &QDialog::hide);
    connect(exitButton, &QPushButton::clicked, this, [this] {
      isDownloadExitConfirmed_ = true;
      activeDownloadExitDialog_->hide();
      QTimer::singleShot(0, this, &MainWindow::close);
    });
  }
  activeDownloadExitLabel_->setText(
      QStringLiteral("仍有 %1 个网页下载任务未完成。现在退出会取消这些下载，"
                     "已下载的临时内容由 WebView2 清理。")
          .arg(activeDownloadCount));
  activeDownloadExitDialog_->show();
  activeDownloadExitDialog_->raise();
  activeDownloadExitDialog_->activateWindow();
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
      width() * 27 / 100, kPlaylistMinimumWidth, kPlaylistMaximumWidth);
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
  dialog.setThemeSettings(themeSettings_);
  connect(&dialog, &LiveSourceMemoDialog::memosSaved, this,
          [this](const QVector<LiveSourceMemo>& memos) {
            liveSourceMemos_ = memos;
            emit liveSourceMemosChanged(liveSourceMemos_);
          });
  dialog.exec();
}

void MainWindow::showShortcutHelp() {
  ShortcutHelpDialog dialog(this);
  dialog.setThemeSettings(themeSettings_);
  dialog.exec();
}

void MainWindow::showThemeSettings() {
  const ThemeSettings originalSettings = themeSettings_;
  ThemeSettingsDialog dialog(originalSettings, this);
  connect(&dialog, &ThemeSettingsDialog::previewChanged, this,
          &MainWindow::applyThemeSettings);
  if (dialog.exec() != QDialog::Accepted) {
    applyThemeSettings(originalSettings);
    return;
  }

  applyThemeSettings(dialog.settings());
  if (themeSettings_ != originalSettings) {
    emit themeSettingsChanged(themeSettings_);
  }
}

void MainWindow::applyThemeSettings(const ThemeSettings& settings) {
  const ThemeSettings normalized = normalizedThemeSettings(settings);
  if (normalized == themeSettings_ && !styleSheet().isEmpty()) {
    return;
  }
  const bool hadCustomBackground =
      !themeSettings_.backgroundImagePath.isEmpty();
  const bool hasCustomBackground =
      !normalized.backgroundImagePath.isEmpty();
  const bool updatesStyle =
      styleSheet().isEmpty() || normalized.accentKey != themeSettings_.accentKey ||
      normalized.appearanceMode != themeSettings_.appearanceMode ||
      normalized.customAccentColor != themeSettings_.customAccentColor ||
      hasCustomBackground != hadCustomBackground;
  themeSettings_ = normalized;
  centralSurface_->setProperty("accentKey", themeSettings_.accentKey);
  centralSurface_->setProperty("appearanceMode",
                               themeSettings_.appearanceMode);
  centralSurface_->setProperty("backgroundBlur",
                               themeSettings_.backgroundBlur);
  centralSurface_->setProperty("backgroundOpacity",
                               themeSettings_.backgroundOpacity);
  centralSurface_->setProperty("customBackground", hasCustomBackground);
  for (QWidget* const widget :
       {static_cast<QWidget*>(playlistPanel_),
        mediaCard_ != nullptr ? mediaCard_->parentWidget() : nullptr,
        mediaDisplay_}) {
    if (widget != nullptr) {
      widget->setProperty("customBackground", hasCustomBackground);
    }
  }
  themeBackground_->setThemeSettings(themeSettings_);
  videoOutput_->setThemeSettings(themeSettings_);
  lyricsView_->setThemeSettings(themeSettings_);
  if (updatesStyle) {
    const QString overrideStyle = themeOverrideStyleSheet(themeSettings_);
    setStyleSheet(mainWindowStyleSheet() + overrideStyle);
    nativePlaybackPage_->setStyleSheet(overrideStyle);
    displayModePanel_->setStyleSheet(overrideStyle);
    for (QMenu* const menu :
         {fileMenu_, viewMenu_, helpMenu_, recentLocalMediaMenu_,
          playlistContextMenu_, livePlaylistContextMenu_}) {
      if (menu != nullptr) {
        menu->setStyleSheet(overrideStyle);
      }
    }
    for (QMenu* const menu : nativePlaybackPage_->findChildren<QMenu*>()) {
      menu->setStyleSheet(overrideStyle);
    }
  }
  controlIconColor_ = resolvedThemePalette(themeSettings_).text;
  refreshControlIcons();
  setNativeDarkTitleBar(
      this, themeSettings_.appearanceMode != QStringLiteral("light"));
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
  if (isMiniPlayer_) {
    exitMiniPlayer();
    showFullScreen();
    updateFullScreenText();
    return;
  }
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

void MainWindow::toggleMiniPlayer() {
  if (isMiniPlayer_) {
    exitMiniPlayer();
  } else {
    enterMiniPlayer();
  }
}

void MainWindow::enterMiniPlayer() {
  if (isMiniPlayer_ || displayMode_ == DisplayMode::Web) {
    return;
  }

  miniPlayerPreviousMinimumSize_ = minimumSize();
  miniPlayerPreviousWasOnTop_ =
      windowFlags().testFlag(Qt::WindowStaysOnTopHint);
  if (isFullScreen()) {
    miniPlayerPreviousGeometry_ = normalGeometry();
    miniPlayerPreviousWindowState_ = Qt::WindowNoState;
    showNormal();
  } else {
    miniPlayerPreviousGeometry_ =
        isMaximized() ? normalGeometry() : geometry();
    miniPlayerPreviousWindowState_ = windowState();
  }

  const QPoint previousCenter = miniPlayerPreviousGeometry_.center();
  isMiniPlayer_ = true;
  setWindowState(Qt::WindowNoState);
  setWindowFlag(Qt::WindowStaysOnTopHint, true);
  setMinimumSize(kMiniPlayerMinimumSize);
  setGeometry(QRect(previousCenter -
                        QPoint(kMiniPlayerSize.width() / 2,
                               kMiniPlayerSize.height() / 2),
                    kMiniPlayerSize));
  show();
  raise();
  activateWindow();
  updateFullScreenText();
}

void MainWindow::exitMiniPlayer() {
  if (!isMiniPlayer_) {
    return;
  }

  isMiniPlayer_ = false;
  setWindowState(Qt::WindowNoState);
  setWindowFlag(Qt::WindowStaysOnTopHint,
                miniPlayerPreviousWasOnTop_);
  setMinimumSize(miniPlayerPreviousMinimumSize_);
  if (miniPlayerPreviousGeometry_.isValid()) {
    setGeometry(miniPlayerPreviousGeometry_);
  }
  setWindowState(miniPlayerPreviousWindowState_);
  show();
  raise();
  activateWindow();
  updateFullScreenText();
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
  menuBar()->hide();
  for (auto* const widget : fullScreenChrome_) {
    widget->setVisible(!isNowFullScreen && !isMiniPlayer_);
  }
  for (auto* const widget : miniPlayerHiddenControls_) {
    widget->setVisible(!isMiniPlayer_);
  }
  playlistPanel_->setVisible(!isNowFullScreen && !isMiniPlayer_ &&
                             isPlaylistExpanded_);
  playlistToggleButton_->setVisible(!isNowFullScreen && !isMiniPlayer_);
  if (isNowFullScreen || isMiniPlayer_) {
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
      isNowFullScreen ? ControlIcon::ExitFullScreen : ControlIcon::FullScreen,
      controlIconColor_));
  fullScreenButton_->setAccessibleName(isNowFullScreen
                                           ? QStringLiteral("退出全屏")
                                           : QStringLiteral("进入全屏"));
  fullScreenButton_->setToolTip(isNowFullScreen
                                    ? QStringLiteral("退出全屏（F11 或 Esc）")
                                    : QStringLiteral("进入全屏（F11）"));
  miniPlayerButton_->setIcon(controlIcon(
      isMiniPlayer_ ? ControlIcon::ExitMiniPlayer : ControlIcon::MiniPlayer,
      controlIconColor_));
  miniPlayerButton_->setAccessibleName(
      isMiniPlayer_ ? QStringLiteral("返回正常窗口")
                    : QStringLiteral("小窗口播放"));
  miniPlayerButton_->setToolTip(
      isMiniPlayer_ ? QStringLiteral("返回正常尺寸播放")
                    : QStringLiteral("切换到置顶小窗口播放"));
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
  playlistToggleButton_->setIcon(
      controlIcon(isPlaylistExpanded_ ? ControlIcon::CollapseRight
                                      : ControlIcon::ExpandLeft,
                  controlIconColor_));
  playlistToggleButton_->setAccessibleName(actionText);
  playlistToggleButton_->setToolTip(actionText);
}

}  // namespace mediahub::gui
