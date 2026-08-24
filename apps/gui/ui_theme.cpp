#include "ui_theme.h"

#include <QColor>
#include <QHeaderView>
#include <QPalette>
#include <QTableWidget>

#include "player_view_state.h"

namespace mediahub::gui {

UiPresentationMode presentationModeFor(const PlayerViewState& viewState) {
  if (viewState.isLivePlaylistActive || viewState.canRefreshNetwork) {
    return UiPresentationMode::Live;
  }
  if (viewState.isAudioVisualizationActive || viewState.canShowLyrics ||
      viewState.isLyricsVisible) {
    return UiPresentationMode::LocalAudio;
  }
  return UiPresentationMode::LocalVideo;
}

QString presentationModeKey(const UiPresentationMode mode) {
  switch (mode) {
    case UiPresentationMode::LocalAudio:
      return QStringLiteral("audio");
    case UiPresentationMode::LocalVideo:
      return QStringLiteral("video");
    case UiPresentationMode::Live:
      return QStringLiteral("live");
  }
  return QStringLiteral("video");
}

const QString& mainWindowStyleSheet() {
  static const QString styleSheet = QStringLiteral(R"(
      QWidget {
          font-family: "Microsoft YaHei UI";
          font-size: 12px;
      }
      QMainWindow[themeMode="audio"],
      QWidget#centralSurface[themeMode="audio"] {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #f8fbfa, stop:0.52 #eef7f3,
                                      stop:1 #e6f1ed);
          color: #17211e;
      }
      QMainWindow[themeMode="video"],
      QWidget#centralSurface[themeMode="video"] {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #0b0e13, stop:0.55 #101620,
                                      stop:1 #151d27);
          color: #edf3f1;
      }
      QMainWindow[themeMode="live"],
      QWidget#centralSurface[themeMode="live"] {
          background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                      stop:0 #111214, stop:1 #181a1d);
          color: #f1f1ef;
      }
      QMenuBar {
          border: none;
          padding: 3px 8px;
      }
      QMenuBar[themeMode="audio"] {
          background: #f8fbfa;
          color: #26342f;
      }
      QMenuBar[themeMode="video"], QMenuBar[themeMode="live"] {
          background: #0a0d11;
          color: #d7dddb;
      }
      QMenuBar::item {
          border-radius: 5px;
          padding: 5px 9px;
      }
      QMenuBar::item:selected {
          background: #25b878;
          color: #ffffff;
      }
      QMenu, QMenu#optionPopup {
          background: #171c22;
          border: 1px solid #313943;
          color: #e8eeec;
          padding: 6px;
      }
      QMenu[themeMode="audio"], QMenu#optionPopup[themeMode="audio"] {
          background: #ffffff;
          border-color: #cfddd7;
          color: #26342f;
      }
      QMenu::item {
          border-radius: 5px;
          margin: 2px;
          min-width: 92px;
          padding: 7px 22px;
      }
      QMenu::item:selected {
          background: #25b878;
          color: #ffffff;
      }
      QMenu::item:checked {
          background: #1b8f61;
          color: #ffffff;
      }
      QToolTip {
          background: #20252c;
          border: 1px solid #3b444f;
          color: #f2f5f4;
          padding: 5px 8px;
      }
      QWidget#centralSurface {
          border: 1px solid #596977;
      }
      QWidget#centralSurface[themeMode="audio"] {
          border-color: #b9ccc4;
      }
      QFrame#displayModePanel {
          background: #0d131a;
          border: none;
          border-bottom: 1px solid #4d5c69;
      }
      QFrame#displayModeRail {
          background: #f4f7fa;
          border: 1px solid #788794;
          border-radius: 8px;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 6px;
          color: #405161;
          font-size: 14px;
          font-weight: 700;
          padding: 0 14px;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"]:hover:!checked {
          background: #e2e8ee;
          border-color: #aab6c0;
          color: #172735;
      }
      QToolButton#localModeButton:checked {
          background: #e7f6ef;
          border-color: #58bd8d;
          color: #145c3f;
      }
      QToolButton#liveModeButton:checked {
          background: #fff3df;
          border-color: #dda451;
          color: #704511;
      }
      QToolButton#webModeButton:checked {
          background: #e8f2fc;
          border-color: #68a3d2;
          color: #174f7c;
      }
      QFrame#headerPanel {
          border: none;
          border-radius: 12px;
      }
      QFrame#headerPanel[themeMode="audio"] {
          background: rgba(255, 255, 255, 178);
          border: 1px solid rgba(199, 221, 211, 180);
      }
      QFrame#headerPanel[themeMode="video"] {
          background: rgba(20, 27, 36, 218);
          border: 1px solid #25303d;
      }
      QFrame#headerPanel[themeMode="live"] {
          background: #1b1d20;
          border: 1px solid #303236;
          border-radius: 5px;
      }
      QLabel#eyebrowLabel {
          font-family: "Bahnschrift SemiCondensed";
          font-size: 11px;
          font-weight: 700;
      }
      QLabel#eyebrowLabel[themeMode="audio"],
      QLabel#eyebrowLabel[themeMode="video"] {
          color: #1cbf73;
      }
      QLabel#eyebrowLabel[themeMode="live"] {
          color: #f1ad46;
      }
      QLabel#titleLabel {
          font-size: 25px;
          font-weight: 700;
      }
      QLabel#titleLabel[themeMode="audio"] {
          color: #17211e;
      }
      QLabel#titleLabel[themeMode="video"],
      QLabel#titleLabel[themeMode="live"] {
          color: #f4f7f6;
      }
      QLabel#subtitleLabel {
          font-size: 12px;
      }
      QLabel#subtitleLabel[themeMode="audio"] {
          color: #65756f;
      }
      QLabel#subtitleLabel[themeMode="video"] {
          color: #8f9aa5;
      }
      QLabel#subtitleLabel[themeMode="live"] {
          color: #a8a7a3;
      }
      QLabel#modeBadgeLabel {
          border-radius: 12px;
          font-family: "Bahnschrift";
          font-size: 10px;
          font-weight: 700;
          padding: 6px 11px;
      }
      QLabel#modeBadgeLabel[themeMode="audio"] {
          background: #dff5ea;
          color: #147e52;
      }
      QLabel#modeBadgeLabel[themeMode="video"] {
          background: #172e2a;
          border: 1px solid #245344;
          color: #51daa2;
      }
      QLabel#modeBadgeLabel[themeMode="live"] {
          background: #33281a;
          border: 1px solid #6a4b25;
          color: #ffc66d;
          border-radius: 4px;
      }
      QWidget#mediaDisplay {
          border-radius: 10px;
      }
      QWidget#mediaDisplay[themeMode="audio"] {
          background: #eef7f3;
          border: 1px solid #c8ddd4;
      }
      QWidget#mediaDisplay[themeMode="video"] {
          background: #050608;
          border: 1px solid #27313d;
      }
      QWidget#mediaDisplay[themeMode="live"] {
          background: #050505;
          border: 1px solid #383838;
          border-radius: 3px;
      }
      QFrame#playlistPanel {
          border-radius: 10px;
      }
      QFrame#playlistPanel[themeMode="audio"] {
          background: rgba(255, 255, 255, 212);
          border: 1px solid #ceded7;
      }
      QFrame#playlistPanel[themeMode="video"] {
          background: #161c24;
          border: 1px solid #293440;
      }
      QFrame#playlistPanel[themeMode="live"] {
          background: #202225;
          border: 1px solid #36383c;
          border-radius: 4px;
      }
      QLabel#playlistTitleLabel {
          font-size: 14px;
          font-weight: 700;
      }
      QLabel#playlistTitleLabel[themeMode="audio"] {
          color: #20312b;
      }
      QLabel#playlistTitleLabel[themeMode="video"] {
          color: #edf1ef;
      }
      QLabel#playlistTitleLabel[themeMode="live"] {
          color: #f1e4cf;
      }
      QTabBar#playlistKindTabs::tab {
          border: none;
          font-size: 12px;
          min-width: 0;
          padding: 7px 5px;
      }
      QTabBar#playlistKindTabs[themeMode="audio"]::tab {
          background: #edf4f1;
          color: #66756f;
      }
      QTabBar#playlistKindTabs[themeMode="video"]::tab {
          background: #202832;
          color: #8e9aa4;
      }
      QTabBar#playlistKindTabs[themeMode="live"]::tab {
          background: #18191b;
          border-bottom: 1px solid #343538;
          color: #918f8a;
      }
      QTabBar#playlistKindTabs::tab:selected {
          background: #20b974;
          color: #ffffff;
          font-weight: 700;
      }
      QTabBar#playlistKindTabs[themeMode="live"]::tab:selected {
          background: #34291d;
          border-bottom: 2px solid #e3a64d;
          color: #ffd18c;
      }
      QFrame#livePlaylistTools[themeMode="live"] {
          background: #17181a;
          border: 1px solid #34363a;
          border-radius: 2px;
      }
      QLabel#livePlaylistSourceLabel[themeMode="live"] {
          color: #d69a43;
          font-weight: 700;
      }
      QLineEdit#livePlaylistUrlEdit,
      QLineEdit#livePlaylistSearchEdit {
          border-radius: 5px;
          padding: 7px 8px;
      }
      QLineEdit#livePlaylistUrlEdit[themeMode="audio"],
      QLineEdit#livePlaylistSearchEdit[themeMode="audio"] {
          background: #f8fbfa;
          border: 1px solid #c6d8d0;
          color: #1d2d27;
      }
      QLineEdit#livePlaylistUrlEdit[themeMode="video"],
      QLineEdit#livePlaylistUrlEdit[themeMode="live"],
      QLineEdit#livePlaylistSearchEdit[themeMode="video"],
      QLineEdit#livePlaylistSearchEdit[themeMode="live"] {
          background: #121417;
          border: 1px solid #404349;
          color: #eceeec;
          font-family: "Cascadia Mono";
          font-size: 11px;
          selection-background-color: #98692d;
      }
      QLineEdit#livePlaylistUrlEdit:focus,
      QLineEdit#livePlaylistSearchEdit:focus {
          border-color: #e5a342;
      }
      QLabel#livePlaylistStatusLabel {
          font-size: 11px;
      }
      QLabel#livePlaylistStatusLabel[themeMode="audio"] {
          color: #6d7d77;
      }
      QLabel#livePlaylistStatusLabel[themeMode="video"] {
          color: #929da6;
      }
      QLabel#livePlaylistStatusLabel[themeMode="live"] {
          background: #202124;
          border: none;
          border-left: 2px solid #9f7134;
          color: #c8b18e;
          padding: 4px 6px;
      }
      QPushButton#livePlaylistLoadButton[themeMode="live"],
      QPushButton#livePlaylistLocateButton[themeMode="live"] {
          min-width: 0;
          padding: 6px 5px;
      }
      QPushButton#livePlaylistLocateButton[themeMode="live"] {
          background: #282a2d;
          border-color: #44464b;
          color: #d4d2cd;
      }
      QListView#playlistView {
          border: none;
          font-size: 12px;
          outline: none;
          padding: 3px;
      }
      QListView#playlistView[themeMode="audio"] {
          background: #f7faf9;
          color: #263630;
      }
      QListView#playlistView[themeMode="video"] {
          background: #10151b;
          color: #cbd3d0;
      }
      QListView#playlistView[themeMode="live"] {
          background: #151618;
          border: 1px solid #323438;
          border-radius: 2px;
          color: #d4d4d1;
          padding: 1px;
      }
      QListView#playlistView::item {
          border-radius: 5px;
          padding: 7px 7px;
      }
      QListView#playlistView[themeMode="live"]::item {
          border-radius: 0;
          border-bottom: 1px solid #25272a;
      }
      QListView#playlistView::item:hover {
          background: rgba(45, 184, 121, 45);
      }
      QListView#playlistView::item:selected {
          background: #1f9b68;
          color: #ffffff;
      }
      QListView#playlistView[themeMode="live"]::item:selected {
          background: #594326;
          color: #ffe0aa;
      }
      QListView#playlistView[themeMode="live"]::item:hover:!selected {
          background: #29261f;
          color: #f0dfc3;
      }
      QListView#playlistView::item:disabled {
          background: #4a2525;
          color: #ffb0a5;
          font-weight: 700;
      }
      QScrollBar[themeMode="live"]:vertical {
          background: #141517;
          border: none;
          margin: 0;
          width: 8px;
      }
      QScrollBar[themeMode="live"]::handle:vertical {
          background: #4c4d50;
          border-radius: 3px;
          min-height: 24px;
      }
      QScrollBar[themeMode="live"]::handle:vertical:hover {
          background: #9c7138;
      }
      QScrollBar[themeMode="live"]::add-line:vertical,
      QScrollBar[themeMode="live"]::sub-line:vertical {
          height: 0;
      }
      QScrollBar[themeMode="live"]::add-page:vertical,
      QScrollBar[themeMode="live"]::sub-page:vertical {
          background: transparent;
      }
  )") + QStringLiteral(R"(
      QWidget#browserPage {
          background: #f2f5f8;
          border: 1px solid #8796a4;
          border-top: none;
          color: #24313d;
      }
      QFrame#browserChrome {
          background: #f4f6f8;
          border-bottom: 1px solid #8796a4;
          color: #24313d;
      }
      QFrame#browserTabStrip {
          background: #dce3ea;
          border-bottom: 1px solid #9ba8b4;
      }
      QFrame#browserToolbar,
      QFrame#browserNavigationBar {
          background: #ffffff;
      }
      QFrame#browserAddressContainer {
          background: #f6f8fa;
          border: 1px solid #b8c3ce;
          border-radius: 8px;
      }
      QFrame#browserAddressContainer:focus-within {
          border-color: #2a76ad;
      }
      QFrame#browserChrome QLineEdit#browserAddressEdit {
          background: transparent;
          border: none;
          color: #1d2b38;
          padding: 0 6px;
          selection-background-color: #2a76ad;
      }
      QFrame#browserChrome QToolButton,
      QFrame#browserChrome QPushButton {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 6px;
          color: #344454;
          min-width: 30px;
          max-width: 30px;
          min-height: 30px;
          max-height: 30px;
          padding: 0;
      }
      QFrame#browserChrome QToolButton:hover:enabled,
      QFrame#browserChrome QPushButton:hover:enabled {
          background: #e5ebf0;
          border-color: #bdc8d2;
      }
      QFrame#browserChrome QToolButton:pressed:enabled,
      QFrame#browserChrome QPushButton:pressed:enabled {
          background: #d2e6f5;
          border-color: #5d99c7;
      }
      QFrame#browserChrome QToolButton:focus,
      QFrame#browserChrome QPushButton:focus {
          border-color: #2a76ad;
      }
      QFrame#browserChrome QToolButton:disabled,
      QFrame#browserChrome QPushButton:disabled {
          background: transparent;
          border-color: transparent;
          color: #a5afb9;
      }
      QTabBar#browserTabBar {
          background: transparent;
          qproperty-drawBase: 0;
      }
      QTabBar#browserTabBar::tab {
          background: #d9e0e7;
          border: 1px solid transparent;
          border-radius: 7px 7px 0 0;
          color: #536272;
          height: 32px;
          margin-right: 2px;
          padding: 0 8px;
      }
      QTabBar#browserTabBar::tab:selected {
          background: #ffffff;
          border-color: #9eabb7;
          border-bottom-color: #ffffff;
          border-top-color: #2a76ad;
          color: #182633;
          font-weight: 700;
      }
      QTabBar#browserTabBar::tab:hover:!selected {
          background: #edf1f5;
          color: #263746;
      }
      QFrame#browserSidePanel {
          background: #f7f9fb;
          border-left: 2px solid #8796a4;
      }
      QFrame#browserSidePanelTitleBar {
          background: #dce4eb;
          border-bottom: 1px solid #9ba8b4;
          min-height: 42px;
      }
      QStackedWidget#browserSidePanelStack {
          background: #f7f9fb;
          border: none;
      }
      QLabel#browserSidePanelTitle {
          border-left: 3px solid #2a76ad;
          color: #102331;
          font-size: 15px;
          font-weight: 700;
          padding-left: 9px;
      }
      QFrame#browserSidePanel QDialog {
          background: transparent;
          color: #273747;
          min-width: 0;
      }
      QMenu#browserMoreMenu {
          background: #ffffff;
          border: 1px solid #aeb9c4;
          color: #263746;
          padding: 4px;
      }
      QMenu#browserMoreMenu::item {
          border-radius: 5px;
          padding: 7px 28px 7px 10px;
      }
      QMenu#browserMoreMenu::item:selected {
          background: #2a76ad;
          color: #ffffff;
      }
      QToolButton#browserDownloadButton[hasActivity="true"] {
          color: #1676ad;
          border-bottom-color: #1676ad;
      }
      QFrame#mediaCard, QFrame#transportPanel {
          border-radius: 10px;
      }
      QFrame#mediaCard[themeMode="audio"],
      QFrame#transportPanel[themeMode="audio"] {
          background: rgba(255, 255, 255, 220);
          border: 1px solid #cfdfd8;
      }
      QFrame#mediaCard[themeMode="video"],
      QFrame#transportPanel[themeMode="video"] {
          background: #151b23;
          border: 1px solid #29333f;
      }
      QFrame#mediaCard[themeMode="live"],
      QFrame#transportPanel[themeMode="live"] {
          background: #202225;
          border: 1px solid #36383c;
          border-radius: 4px;
      }
      QLabel#captionLabel, QLabel#transportCaptionLabel,
      QLabel#positionLabel, QLabel#volumeLabel {
          font-size: 11px;
          font-weight: 700;
      }
      QLabel#captionLabel[themeMode="audio"],
      QLabel#transportCaptionLabel[themeMode="audio"],
      QLabel#positionLabel[themeMode="audio"],
      QLabel#volumeLabel[themeMode="audio"] {
          color: #66766f;
      }
      QLabel#captionLabel[themeMode="video"],
      QLabel#transportCaptionLabel[themeMode="video"],
      QLabel#positionLabel[themeMode="video"],
      QLabel#volumeLabel[themeMode="video"] {
          color: #8f9ba4;
      }
      QLabel#captionLabel[themeMode="live"],
      QLabel#transportCaptionLabel[themeMode="live"],
      QLabel#positionLabel[themeMode="live"],
      QLabel#volumeLabel[themeMode="live"] {
          color: #b1b0ac;
      }
      QLabel#currentMediaLabel {
          font-size: 17px;
          font-weight: 600;
      }
      QLabel#currentMediaLabel[themeMode="audio"] {
          color: #182720;
      }
      QLabel#currentMediaLabel[themeMode="video"],
      QLabel#currentMediaLabel[themeMode="live"] {
          color: #f1f4f3;
      }
      QLabel#playbackStatusLabel {
          border-radius: 11px;
          font-size: 11px;
          font-weight: 700;
          min-width: 82px;
          padding: 5px 10px;
      }
      QLabel#playbackStatusLabel[themeMode="audio"],
      QLabel#playbackStatusLabel[themeMode="video"] {
          background: #163a2e;
          color: #5ce0a6;
      }
      QLabel#playbackStatusLabel[themeMode="audio"] {
          background: #dff5ea;
          color: #117b4d;
      }
      QLabel#playbackStatusLabel[themeMode="live"] {
          background: #3a2c1d;
          color: #ffc66d;
          border-radius: 3px;
      }
      QLabel#playbackErrorLabel {
          background: #502724;
          border: 1px solid #7c3b35;
          border-radius: 5px;
          color: #ffd2cb;
          padding: 7px 10px;
      }
      QLabel#playbackErrorLabel[themeMode="audio"] {
          background: #fff0ed;
          border-color: #efc5bc;
          color: #a34232;
      }
      QToolButton[optionSelector="true"] {
          border-radius: 15px;
          font-size: 11px;
          font-weight: 700;
          min-width: 54px;
          padding: 7px 10px;
      }
      QToolButton[optionSelector="true"][themeMode="audio"] {
          background: #edf4f1;
          border: 1px solid #c8d8d1;
          color: #2d5546;
      }
      QToolButton[optionSelector="true"][themeMode="video"] {
          background: #202832;
          border: 1px solid #33404d;
          color: #c8d5d0;
      }
      QToolButton[optionSelector="true"][themeMode="live"] {
          background: #2b2d30;
          border: 1px solid #46484c;
          border-radius: 3px;
          color: #d4d3cf;
      }
      QToolButton[optionSelector="true"]:hover:enabled {
          background: #1f9b68;
          border-color: #2ac486;
          color: #ffffff;
      }
      QToolButton[optionSelector="true"]::menu-indicator {
          image: none;
          width: 0px;
      }
      QToolButton[transportControl="true"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 18px;
          padding: 0;
      }
      QToolButton[transportControl="true"][themeMode="audio"] {
          background: #edf4f1;
          border-color: #cfddd7;
      }
      QToolButton[transportControl="true"][themeMode="video"] {
          background: #202832;
          border-color: #303b47;
      }
      QToolButton[transportControl="true"][themeMode="live"] {
          background: #2a2c2f;
          border-color: #414347;
          border-radius: 4px;
      }
      QToolButton[transportControl="true"]:hover:enabled {
          background: #1e9d68;
          border-color: #32c98c;
      }
      QToolButton[transportControl="true"]:pressed:enabled {
          background: #16784f;
      }
      QToolButton[transportControl="true"]:disabled {
          background: rgba(110, 120, 120, 35);
          border-color: rgba(120, 130, 130, 38);
      }
      QToolButton[primaryTransport="true"] {
          background: #20bd78;
          border-color: #43d497;
      }
      QToolButton[lyricsControl="true"] {
          color: #1b9b63;
          font-size: 15px;
          font-weight: 800;
      }
      QToolButton[lyricsControl="true"][themeMode="video"],
      QToolButton[lyricsControl="true"][themeMode="live"] {
          color: #57dca5;
      }
      QToolButton[lyricsControl="true"]:checked {
          background: #20bd78;
          border-color: #43d497;
          color: #ffffff;
      }
      QToolButton[playlistToggle="true"] {
          border-radius: 12px;
      }
      QToolButton[playlistToggle="true"][themeMode="live"] {
          border-radius: 3px;
      }
      QFrame#volumePopup {
          background: #171c22;
          border: 1px solid #35404a;
          border-radius: 9px;
          min-width: 88px;
      }
      QFrame#volumePopup[themeMode="audio"] {
          background: #ffffff;
          border-color: #cbdad4;
      }
      QSlider::groove:horizontal {
          background: #47515a;
          border-radius: 2px;
          height: 4px;
      }
      QSlider[themeMode="audio"]::groove:horizontal {
          background: #d7e2dd;
      }
      QSlider::sub-page:horizontal {
          background: #20bd78;
          border-radius: 2px;
      }
      QSlider[themeMode="live"]::sub-page:horizontal {
          background: #e6a447;
      }
      QSlider::handle:horizontal {
          background: #f7faf9;
          border: 2px solid #20bd78;
          border-radius: 6px;
          margin: -5px 0;
          width: 12px;
      }
      QSlider[themeMode="live"]::handle:horizontal {
          border-color: #e6a447;
      }
      QSlider::groove:horizontal:disabled {
          background: #3a4046;
      }
      QSlider::sub-page:horizontal:disabled {
          background: #52645d;
      }
      QSlider::handle:horizontal:disabled {
          background: #8b9290;
          border-color: #66706d;
      }
      QSlider::groove:vertical {
          background: #47515a;
          border-radius: 2px;
          width: 4px;
      }
      QSlider[themeMode="audio"]::groove:vertical {
          background: #d7e2dd;
      }
      QSlider::add-page:vertical {
          background: #20bd78;
          border-radius: 2px;
      }
      QSlider::sub-page:vertical {
          background: #47515a;
          border-radius: 2px;
      }
      QSlider::handle:vertical {
          background: #f7faf9;
          border: 2px solid #20bd78;
          border-radius: 6px;
          height: 12px;
          margin: 0 -5px;
      }
      QPushButton {
          border-radius: 6px;
          font-size: 12px;
          font-weight: 600;
          min-width: 68px;
          padding: 8px 12px;
      }
      QPushButton[themeMode="audio"] {
          background: #ffffff;
          border: 1px solid #c9d8d2;
          color: #263b33;
      }
      QPushButton[themeMode="video"] {
          background: #202832;
          border: 1px solid #35414d;
          color: #dce3e0;
      }
      QPushButton[themeMode="live"] {
          background: #2b2d30;
          border: 1px solid #494b4f;
          border-radius: 3px;
          color: #deddda;
          padding: 7px 10px;
      }
      QPushButton:hover:enabled {
          background: #1c9e67;
          border-color: #2cc283;
          color: #ffffff;
      }
      QPushButton[primary="true"] {
          background: #20b974;
          border-color: #20b974;
          color: #ffffff;
      }
      QPushButton[primary="true"][themeMode="live"] {
          background: #8d632d;
          border-color: #b17c38;
          color: #fff2d9;
      }
      QPushButton:disabled {
          background: rgba(100, 110, 110, 35);
          border-color: rgba(120, 130, 130, 40);
          color: #747d7a;
      }
      QLabel#playlistTitleLabel[responsiveSize="compact"] {
          font-size: 14px;
      }
      QTabBar#playlistKindTabs[responsiveSize="compact"]::tab,
      QLineEdit#livePlaylistUrlEdit[responsiveSize="compact"],
      QLineEdit#livePlaylistSearchEdit[responsiveSize="compact"],
      QLabel#livePlaylistStatusLabel[responsiveSize="compact"],
      QPushButton#livePlaylistLoadButton[responsiveSize="compact"],
      QPushButton#livePlaylistLocateButton[responsiveSize="compact"] {
          font-size: 12px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="compact"] {
          font-size: 10px;
      }
      QLabel#playlistTitleLabel[responsiveSize="normal"] {
          font-size: 15px;
      }
      QTabBar#playlistKindTabs[responsiveSize="normal"]::tab,
      QLineEdit#livePlaylistUrlEdit[responsiveSize="normal"],
      QLineEdit#livePlaylistSearchEdit[responsiveSize="normal"],
      QLabel#livePlaylistStatusLabel[responsiveSize="normal"],
      QPushButton#livePlaylistLoadButton[responsiveSize="normal"],
      QPushButton#livePlaylistLocateButton[responsiveSize="normal"] {
          font-size: 13px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="normal"] {
          font-size: 11px;
      }
      QLabel#playlistTitleLabel[responsiveSize="large"] {
          font-size: 16px;
      }
      QTabBar#playlistKindTabs[responsiveSize="large"]::tab,
      QLineEdit#livePlaylistUrlEdit[responsiveSize="large"],
      QLineEdit#livePlaylistSearchEdit[responsiveSize="large"],
      QLabel#livePlaylistStatusLabel[responsiveSize="large"],
      QPushButton#livePlaylistLoadButton[responsiveSize="large"],
      QPushButton#livePlaylistLocateButton[responsiveSize="large"] {
          font-size: 14px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="large"] {
          font-size: 12px;
      }
      QLabel#playlistTitleLabel[responsiveSize="extraLarge"] {
          font-size: 18px;
      }
      QTabBar#playlistKindTabs[responsiveSize="extraLarge"]::tab,
      QLineEdit#livePlaylistUrlEdit[responsiveSize="extraLarge"],
      QLineEdit#livePlaylistSearchEdit[responsiveSize="extraLarge"],
      QLabel#livePlaylistStatusLabel[responsiveSize="extraLarge"],
      QPushButton#livePlaylistLoadButton[responsiveSize="extraLarge"],
      QPushButton#livePlaylistLocateButton[responsiveSize="extraLarge"] {
          font-size: 16px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="extraLarge"] {
          font-size: 13px;
      }
      QLineEdit#browserAddressEdit[responsiveSize="compact"],
      QPushButton#browserGoButton[responsiveSize="compact"],
      QPushButton#browserClearDataButton[responsiveSize="compact"],
      QFrame#browserToolbar QToolButton[responsiveSize="compact"],
      QFrame#browserToolbar QPushButton[responsiveSize="compact"] {
          font-size: 12px;
      }
      QPushButton#browserGoButton[responsiveSize="compact"],
      QPushButton#browserClearDataButton[responsiveSize="compact"],
      QFrame#browserToolbar QToolButton[responsiveSize="compact"],
      QFrame#browserToolbar QPushButton[responsiveSize="compact"] {
          min-width: 0;
          font-size: 10px;
          padding: 2px 1px;
      }
      QLineEdit#browserAddressEdit[responsiveSize="normal"] {
          font-size: 13px;
      }
      QLineEdit#browserAddressEdit[responsiveSize="large"] {
          font-size: 15px;
      }
      QLineEdit#browserAddressEdit[responsiveSize="extraLarge"] {
          font-size: 17px;
      }
  )") + QStringLiteral(R"(
      QDialog#browserHistoryDialog,
      QDialog#browserHistoryClearDialog,
      QDialog#browserFavoritesDialog,
      QDialog#browserFavoriteImportDialog,
      QDialog#browserFavoriteEditorDialog,
      QDialog#browserAudioTabsDialog,
      QDialog#browserTabSearchDialog,
      QDialog#browserTabGroupDialog,
      QDialog#browserPinnedCloseDialog,
      QDialog#browserPermissionDialog,
      QDialog#browserPermissionManagementDialog,
      QDialog#browserStartupSettingsDialog,
      QDialog#browserActiveDownloadExitDialog,
      QDialog#browserExternalProtocolDialog,
      QDialog#browserCertificateDialog,
      QDialog#browserClearDataDialog {
          background: #f7f9fb;
          color: #1f2d3a;
          min-width: 360px;
      }
      QDialog#browserHistoryDialog QLabel,
      QDialog#browserHistoryClearDialog QLabel,
      QDialog#browserFavoritesDialog QLabel,
      QDialog#browserFavoriteImportDialog QLabel,
      QDialog#browserFavoriteEditorDialog QLabel,
      QDialog#browserAudioTabsDialog QLabel,
      QDialog#browserTabSearchDialog QLabel,
      QDialog#browserTabGroupDialog QLabel,
      QDialog#browserPinnedCloseDialog QLabel,
      QDialog#browserPermissionDialog QLabel,
      QDialog#browserPermissionManagementDialog QLabel,
      QDialog#browserStartupSettingsDialog QLabel,
      QDialog#browserActiveDownloadExitDialog QLabel,
      QDialog#browserExternalProtocolDialog QLabel,
      QDialog#browserCertificateDialog QLabel,
      QDialog#browserClearDataDialog QLabel {
          background: transparent;
          color: #526171;
          font-size: 12px;
      }
      QDialog#browserHistoryDialog QPushButton,
      QDialog#browserHistoryClearDialog QPushButton,
      QDialog#browserFavoritesDialog QPushButton,
      QDialog#browserFavoriteImportDialog QPushButton,
      QDialog#browserFavoriteEditorDialog QPushButton,
      QDialog#browserAudioTabsDialog QPushButton,
      QDialog#browserTabSearchDialog QPushButton,
      QDialog#browserTabGroupDialog QPushButton,
      QDialog#browserPinnedCloseDialog QPushButton,
      QDialog#browserPermissionDialog QPushButton,
      QDialog#browserPermissionManagementDialog QPushButton,
      QDialog#browserStartupSettingsDialog QPushButton,
      QDialog#browserActiveDownloadExitDialog QPushButton,
      QDialog#browserExternalProtocolDialog QPushButton,
      QDialog#browserCertificateDialog QPushButton,
      QDialog#browserClearDataDialog QPushButton {
          background: #ffffff;
          border: 1px solid #c0cbd5;
          color: #263746;
      }
      QDialog#browserHistoryDialog QPushButton:hover:enabled,
      QDialog#browserHistoryClearDialog QPushButton:hover:enabled,
      QDialog#browserFavoritesDialog QPushButton:hover:enabled,
      QDialog#browserFavoriteImportDialog QPushButton:hover:enabled,
      QDialog#browserFavoriteEditorDialog QPushButton:hover:enabled,
      QDialog#browserAudioTabsDialog QPushButton:hover:enabled,
      QDialog#browserTabSearchDialog QPushButton:hover:enabled,
      QDialog#browserTabGroupDialog QPushButton:hover:enabled,
      QDialog#browserPinnedCloseDialog QPushButton:hover:enabled,
      QDialog#browserPermissionDialog QPushButton:hover:enabled,
      QDialog#browserPermissionManagementDialog QPushButton:hover:enabled,
      QDialog#browserStartupSettingsDialog QPushButton:hover:enabled,
      QDialog#browserActiveDownloadExitDialog QPushButton:hover:enabled,
      QDialog#browserExternalProtocolDialog QPushButton:hover:enabled,
      QDialog#browserCertificateDialog QPushButton:hover:enabled,
      QDialog#browserClearDataDialog QPushButton:hover:enabled {
          background: #e7f1f9;
          border-color: #68a3d2;
          color: #174f7c;
      }
      QListWidget#browserHistoryList,
      QListWidget#browserFavoritesList,
      QListWidget#browserAudioTabsList,
      QListWidget#browserTabSearchList,
      QListWidget#browserTabGroupList,
      QListWidget#browserStartupUrlsList {
          alternate-background-color: #f4f7fa;
          background: #ffffff;
          border: 1px solid #c4ced8;
          border-radius: 8px;
          color: #293847;
          outline: none;
          padding: 5px;
      }
      QListWidget#browserHistoryList::item,
      QListWidget#browserFavoritesList::item,
      QListWidget#browserAudioTabsList::item,
      QListWidget#browserTabSearchList::item,
      QListWidget#browserTabGroupList::item,
      QListWidget#browserStartupUrlsList::item {
          border-bottom: 1px solid #e1e7ec;
          border-radius: 5px;
          min-height: 38px;
          padding: 7px 9px;
      }
      QListWidget#browserHistoryList::item:hover,
      QListWidget#browserFavoritesList::item:hover,
      QListWidget#browserAudioTabsList::item:hover,
      QListWidget#browserTabSearchList::item:hover,
      QListWidget#browserTabGroupList::item:hover,
      QListWidget#browserStartupUrlsList::item:hover {
          background: #e8f2fa;
          color: #173a56;
      }
      QListWidget#browserHistoryList::item:selected,
      QListWidget#browserFavoritesList::item:selected,
      QListWidget#browserAudioTabsList::item:selected,
      QListWidget#browserTabSearchList::item:selected,
      QListWidget#browserTabGroupList::item:selected,
      QListWidget#browserStartupUrlsList::item:selected {
          background: #2a76ad;
          color: #ffffff;
      }
      QLineEdit#browserFavoriteTitleEdit,
      QLineEdit#browserFavoriteUrlEdit,
      QLineEdit#browserFavoriteNoteEdit,
      QLineEdit#browserHistorySearchEdit,
      QLineEdit#browserFavoritesSearchEdit,
      QLineEdit#browserPermissionSearchEdit,
      QLineEdit#browserTabSearchEdit,
      QLineEdit#browserTabGroupNameEdit,
      QLineEdit#browserHomeUrlEdit,
      QLineEdit#browserStartupUrlEdit,
      QLineEdit#browserFindEdit {
          background: #ffffff;
          border: 1px solid #bdc8d2;
          border-radius: 7px;
          color: #21313f;
          padding: 7px 9px;
          selection-background-color: #2a76ad;
      }
      QLineEdit#browserFavoriteTitleEdit:focus,
      QLineEdit#browserFavoriteUrlEdit:focus,
      QLineEdit#browserFavoriteNoteEdit:focus,
      QLineEdit#browserHistorySearchEdit:focus,
      QLineEdit#browserFavoritesSearchEdit:focus,
      QLineEdit#browserPermissionSearchEdit:focus,
      QLineEdit#browserTabSearchEdit:focus,
      QLineEdit#browserTabGroupNameEdit:focus,
      QLineEdit#browserHomeUrlEdit:focus,
      QLineEdit#browserStartupUrlEdit:focus,
      QLineEdit#browserFindEdit:focus {
          background: #ffffff;
          border-color: #2a76ad;
      }
      QLabel#browserPermissionOriginLabel,
      QLabel#browserPermissionKindLabel,
      QLabel#browserExternalProtocolOriginLabel,
      QLabel#browserExternalProtocolTargetLabel,
      QLabel#browserCertificateOriginLabel {
          background: #f0f4f7;
          border: 1px solid #c2ccd6;
          border-left: 3px solid #2a76ad;
          border-radius: 6px;
          color: #263746;
          font-family: "Cascadia Mono";
          padding: 8px 10px;
      }
      QLabel#browserPermissionLimitationLabel,
      QLabel#browserPermissionStatusLabel,
      QLabel#browserFavoriteTransferStatusLabel,
      QLabel#browserStartupSettingsExplanation,
      QLabel#browserClearDataExplanation {
          background: #fff6dc;
          border: 1px solid #e2c26b;
          border-radius: 7px;
          color: #684908;
          padding: 9px 11px;
      }
      QLabel#browserCertificateErrorLabel {
          background: #fff0ed;
          border: 1px solid #e2aaa2;
          border-left: 3px solid #c84d40;
          border-radius: 7px;
          color: #8e2f26;
          padding: 9px 11px;
      }
      QDialog QPushButton#browserFavoriteSaveButton,
      QDialog QPushButton#browserFavoriteImportConfirmButton,
      QDialog QPushButton#browserPermissionAllowOnceButton,
      QDialog QPushButton#browserPermissionRememberButton,
      QDialog QPushButton#browserPermissionSaveButton,
      QDialog QPushButton#browserTabGroupCreateButton,
      QDialog QPushButton#browserTabGroupRenameButton,
      QDialog QPushButton#browserTabGroupRecolorButton,
      QDialog QPushButton#browserTabGroupToggleCollapsedButton,
      QDialog QPushButton#browserStartupSaveButton,
      QDialog QPushButton#browserExternalProtocolConfirmButton,
      QDialog QPushButton#browserCertificateSafetyButton,
      QDialog QPushButton#browserFavoriteImportButton,
      QDialog QPushButton#browserFavoriteExportButton,
      QPushButton#browserDownloadRetryButton {
          background: #168a5d;
          border-color: #29bd80;
          color: #ffffff;
      }
      QDialog QPushButton#browserFavoriteSaveButton:hover:enabled,
      QDialog QPushButton#browserFavoriteImportConfirmButton:hover:enabled,
      QDialog QPushButton#browserPermissionAllowOnceButton:hover:enabled,
      QDialog QPushButton#browserPermissionRememberButton:hover:enabled,
      QDialog QPushButton#browserPermissionSaveButton:hover:enabled,
      QDialog QPushButton#browserTabGroupCreateButton:hover:enabled,
      QDialog QPushButton#browserTabGroupRenameButton:hover:enabled,
      QDialog QPushButton#browserTabGroupRecolorButton:hover:enabled,
      QDialog QPushButton#browserTabGroupToggleCollapsedButton:hover:enabled,
      QDialog QPushButton#browserStartupSaveButton:hover:enabled,
      QDialog QPushButton#browserExternalProtocolConfirmButton:hover:enabled,
      QDialog QPushButton#browserCertificateSafetyButton:hover:enabled,
      QDialog QPushButton#browserFavoriteImportButton:hover:enabled,
      QDialog QPushButton#browserFavoriteExportButton:hover:enabled,
      QPushButton#browserDownloadRetryButton:hover:enabled {
          background: #20aa72;
          border-color: #42d69a;
      }
      QDialog QPushButton#browserFavoriteRemoveButton,
      QDialog QPushButton#browserHistoryRemoveButton,
      QDialog QPushButton#browserHistoryClearButton,
      QDialog QPushButton#browserHistoryClearConfirmButton,
      QDialog QPushButton#browserPermissionDenyButton,
      QDialog QPushButton#browserPermissionRemoveButton,
      QDialog QPushButton#browserTabGroupRemoveButton,
      QDialog QPushButton#browserStartupRemoveButton,
      QDialog QPushButton#browserCertificateContinueButton,
      QDialog QPushButton#browserClearDataConfirmButton,
      QDialog QPushButton#browserPinnedCloseConfirmButton,
      QDialog QPushButton#browserActiveDownloadExitButton,
      QPushButton#browserDownloadCancelButton,
      QDialog QPushButton#browserAudioTabCloseButton {
          background: #fff0ee;
          border-color: #d89992;
          color: #922f27;
      }
      QDialog QPushButton#browserFavoriteRemoveButton:hover:enabled,
      QDialog QPushButton#browserHistoryRemoveButton:hover:enabled,
      QDialog QPushButton#browserHistoryClearButton:hover:enabled,
      QDialog QPushButton#browserHistoryClearConfirmButton:hover:enabled,
      QDialog QPushButton#browserPermissionDenyButton:hover:enabled,
      QDialog QPushButton#browserPermissionRemoveButton:hover:enabled,
      QDialog QPushButton#browserTabGroupRemoveButton:hover:enabled,
      QDialog QPushButton#browserStartupRemoveButton:hover:enabled,
      QDialog QPushButton#browserClearDataConfirmButton:hover:enabled,
      QDialog QPushButton#browserPinnedCloseConfirmButton:hover:enabled,
      QDialog QPushButton#browserActiveDownloadExitButton:hover:enabled,
      QPushButton#browserDownloadCancelButton:hover:enabled,
      QDialog QPushButton#browserAudioTabCloseButton:hover:enabled {
          background: #a84036;
          border-color: #d96054;
          color: #ffffff;
      }
  )") + QStringLiteral(R"(
      QTableWidget#browserPermissionTable {
          alternate-background-color: #f4f7fa;
          background: #ffffff;
          border: 1px solid #c4ced8;
          border-radius: 8px;
          color: #293847;
          gridline-color: #e1e7ec;
          outline: none;
      }
      QTableWidget#browserPermissionTable::item {
          min-height: 32px;
          padding: 5px 8px;
      }
      QTableWidget#browserPermissionTable::item:selected {
          background: #2a76ad;
          color: #ffffff;
      }
      QTableWidget#browserPermissionTable QHeaderView::section {
          background: #e8edf2;
          border: 0;
          border-bottom: 1px solid #bcc7d1;
          color: #263746;
          padding: 7px 9px;
      }
      QComboBox#browserPermissionStateCombo {
          background: #ffffff;
          border: 1px solid #bdc8d2;
          border-radius: 7px;
          color: #21313f;
          min-width: 104px;
          padding: 7px 9px;
      }
      QComboBox#browserTabGroupColorCombo,
      QComboBox#browserStartupModeCombo {
          background: #ffffff;
          border: 1px solid #bdc8d2;
          border-radius: 7px;
          color: #21313f;
          min-width: 116px;
          padding: 7px 9px;
      }
      QSpinBox#browserMaximumTabCountSpin {
          background: #ffffff;
          border: 1px solid #bdc8d2;
          border-radius: 7px;
          color: #21313f;
          min-width: 104px;
          padding: 7px 9px;
      }
      QLabel#browserTabGroupStatusLabel[status="success"] {
          background: #e6f6ee;
          border: 1px solid #83c3a3;
          border-radius: 7px;
          color: #1e6b47;
          padding: 8px 10px;
      }
      QLabel#browserTabGroupStatusLabel[status="error"] {
          background: #fff0ed;
          border: 1px solid #e2aaa2;
          border-radius: 7px;
          color: #8e2f26;
          padding: 8px 10px;
      }
      QDialog QPushButton#browserCertificateContinueButton {
          background: #fff4df;
          border-color: #d8ad62;
          color: #724b0d;
      }
      QDialog QPushButton#browserCertificateContinueButton:hover:enabled {
          background: #9c682d;
          border-color: #d39b50;
          color: #ffffff;
      }
      QFrame#browserFindBar {
          background: #ffffff;
          border: 1px solid #b8c3ce;
          border-radius: 8px;
      }
      QLabel#browserFindResultLabel {
          color: #647484;
          font-family: "Cascadia Mono";
          min-width: 44px;
      }
      QFrame#browserFindBar QPushButton,
      QFrame#browserFindBar QToolButton {
          min-width: 54px;
          padding: 6px 9px;
      }
      QMenu#browserTabContextMenu {
          background: #ffffff;
          border: 1px solid #aeb9c4;
          border-radius: 7px;
          color: #263746;
          padding: 6px;
      }
      QMenu#browserTabContextMenu::separator {
          background: #d8dfe5;
          height: 1px;
          margin: 5px 9px;
      }
      QMenu#browserTabContextMenu::item {
          margin: 1px;
          min-width: 150px;
          padding: 7px 25px;
      }
      QMenu#browserTabContextMenu::item:selected {
          background: #2a76ad;
          color: #ffffff;
      }
      QWidget#browserDownloadCenter {
          background: #f7f9fb;
          border: 1px solid #c2ccd6;
          border-radius: 10px;
          color: #293847;
      }
      QLabel#browserDownloadCenterTitleLabel {
          color: #1f2d3a;
          font-size: 15px;
          font-weight: 700;
      }
      QLabel#browserDownloadCenterSummaryLabel {
          color: #647484;
          font-family: "Cascadia Mono";
      }
      QPushButton#browserDownloadClearCompletedButton {
          background: #ffffff;
          border: 1px solid #c0cbd5;
          border-radius: 7px;
          color: #394b5b;
          padding: 6px 10px;
      }
      QPushButton#browserDownloadClearCompletedButton:hover:enabled {
          background: #e7f1f9;
          border-color: #68a3d2;
          color: #174f7c;
      }
      QScrollArea#browserDownloadCenterScrollArea,
      QWidget#browserDownloadCenterContent {
          background: transparent;
          border: none;
      }
      QLabel#browserDownloadCenterEmptyLabel {
          background: #ffffff;
          border: 1px dashed #b8c3ce;
          border-radius: 8px;
          color: #71808e;
          padding: 24px;
      }
      QWidget#browserDownloadWidget {
          background: #ffffff;
          border: 1px solid #c2ccd6;
          border-left: 3px solid #2a76ad;
          border-radius: 8px;
          color: #293847;
      }
      QLabel#browserDownloadOriginLabel {
          color: #71808e;
          font-family: "Cascadia Mono";
      }
      QLabel#browserDownloadFileNameLabel {
          color: #1f2d3a;
          font-weight: 700;
      }
      QLabel#browserDownloadSizeLabel {
          color: #647484;
      }
      QLabel#browserDownloadSpeedLabel,
      QLabel#browserDownloadRemainingLabel {
          color: #4f7387;
          font-size: 10px;
      }
      QLabel#browserDownloadStateLabel {
          background: #e5f5ed;
          border-radius: 9px;
          color: #1e6b47;
          font-weight: 700;
          padding: 3px 8px;
      }
      QLabel#browserDownloadErrorLabel {
          color: #a43d32;
      }
      QProgressBar#browserDownloadProgressBar {
          background: #dce4eb;
          border: none;
          border-radius: 4px;
          color: #21313f;
          font-size: 10px;
          min-height: 8px;
          text-align: center;
      }
      QProgressBar#browserDownloadProgressBar::chunk {
          background: #20b974;
          border-radius: 4px;
      }
      QDialog#browserHistoryDialog QScrollBar:vertical,
      QDialog#browserFavoritesDialog QScrollBar:vertical,
      QDialog#browserAudioTabsDialog QScrollBar:vertical,
      QDialog#browserTabSearchDialog QScrollBar:vertical,
      QDialog#browserTabGroupDialog QScrollBar:vertical,
      QDialog#browserStartupSettingsDialog QScrollBar:vertical,
      QScrollArea#browserDownloadCenterScrollArea QScrollBar:vertical {
          background: #eef2f5;
          border: none;
          margin: 2px;
          width: 9px;
      }
      QDialog#browserHistoryDialog QScrollBar::handle:vertical,
      QDialog#browserFavoritesDialog QScrollBar::handle:vertical,
      QDialog#browserAudioTabsDialog QScrollBar::handle:vertical,
      QDialog#browserTabSearchDialog QScrollBar::handle:vertical,
      QDialog#browserTabGroupDialog QScrollBar::handle:vertical,
      QDialog#browserStartupSettingsDialog QScrollBar::handle:vertical,
      QScrollArea#browserDownloadCenterScrollArea QScrollBar::handle:vertical {
          background: #a5b2be;
          border-radius: 4px;
          min-height: 24px;
      }
      QDialog#browserHistoryDialog QScrollBar::add-line:vertical,
      QDialog#browserHistoryDialog QScrollBar::sub-line:vertical,
      QDialog#browserFavoritesDialog QScrollBar::add-line:vertical,
      QDialog#browserFavoritesDialog QScrollBar::sub-line:vertical,
      QDialog#browserAudioTabsDialog QScrollBar::add-line:vertical,
      QDialog#browserAudioTabsDialog QScrollBar::sub-line:vertical,
      QDialog#browserTabSearchDialog QScrollBar::add-line:vertical,
      QDialog#browserTabSearchDialog QScrollBar::sub-line:vertical,
      QDialog#browserTabGroupDialog QScrollBar::add-line:vertical,
      QDialog#browserTabGroupDialog QScrollBar::sub-line:vertical,
      QDialog#browserStartupSettingsDialog QScrollBar::add-line:vertical,
      QDialog#browserStartupSettingsDialog QScrollBar::sub-line:vertical,
      QScrollArea#browserDownloadCenterScrollArea QScrollBar::add-line:vertical,
      QScrollArea#browserDownloadCenterScrollArea QScrollBar::sub-line:vertical {
          height: 0;
      }
      QWidget#browserPage[responsiveSize="compact"] QDialog {
          min-width: 300px;
      }
      QWidget#browserPage[responsiveSize="compact"] QDialog QListWidget,
      QFrame#browserFindBar[responsiveSize="compact"] QLineEdit,
      QFrame#browserFindBar[responsiveSize="compact"] QPushButton,
      QFrame#browserFindBar[responsiveSize="compact"] QToolButton {
          font-size: 11px;
      }
      QFrame#browserFindBar[responsiveSize="compact"] QPushButton,
      QFrame#browserFindBar[responsiveSize="compact"] QToolButton {
          min-width: 44px;
          padding: 5px 6px;
      }
      QWidget#browserPage[responsiveSize="large"] QDialog QListWidget,
      QWidget#browserPage[responsiveSize="large"] QDialog QLineEdit,
      QFrame#browserFindBar[responsiveSize="large"] QLineEdit,
      QFrame#browserFindBar[responsiveSize="large"] QPushButton {
          font-size: 14px;
      }
      QWidget#browserPage[responsiveSize="extraLarge"] QDialog QListWidget,
      QWidget#browserPage[responsiveSize="extraLarge"] QDialog QLineEdit,
      QFrame#browserFindBar[responsiveSize="extraLarge"] QLineEdit,
      QFrame#browserFindBar[responsiveSize="extraLarge"] QPushButton {
          font-size: 16px;
      }
      QFrame#browserSidePanel QDialog {
          background: transparent;
          color: #273747;
          min-width: 0;
          min-height: 0;
      }
  )") + QStringLiteral(R"(
      QMainWindow[themeMode="audio"],
      QMainWindow[themeMode="video"],
      QMainWindow[themeMode="live"],
      QWidget#centralSurface[themeMode="audio"],
      QWidget#centralSurface[themeMode="video"],
      QWidget#centralSurface[themeMode="live"] {
          background: #0e1013;
          color: #e7e9ec;
      }
      QWidget#centralSurface {
          border: none;
      }
      QMenuBar[themeMode="audio"],
      QMenuBar[themeMode="video"],
      QMenuBar[themeMode="live"] {
          background: #0b0d10;
          border-bottom: 1px solid #22262c;
          color: #c9cdd2;
      }
      QMenuBar::item {
          border-radius: 3px;
      }
      QMenuBar::item:selected {
          background: #282c32;
          color: #ffffff;
      }
      QFrame#displayModePanel {
          background: #0b0d10;
          border: none;
          border-bottom: 1px solid #25292f;
      }
      QToolButton[topChromeButton="true"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 4px;
          color: #aeb4bb;
          padding: 0;
      }
      QToolButton[topChromeButton="true"]:hover,
      QToolButton[topChromeButton="true"]:checked {
          background: #20242a;
          border-color: #505862;
          color: #f1f3f5;
      }
      QToolButton[topChromeButton="true"]::menu-indicator {
          image: none;
          width: 0;
      }
      QLabel#brandLabel {
          color: #f1f3f5;
          font-family: "Segoe UI Semibold";
          font-size: 15px;
          font-weight: 600;
      }
      QFrame#displayModeRail {
          background: transparent;
          border: none;
          border-radius: 0;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 4px;
          color: #8f969f;
          font-size: 13px;
          font-weight: 600;
          padding: 0 12px;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"]:hover:!checked {
          background: #1b1e23;
          border-color: #2b3037;
          color: #d9dde1;
      }
      QToolButton#localModeButton:checked {
          background: #202a34;
          border-color: #34536d;
          color: #74bfff;
      }
      QToolButton#liveModeButton:checked {
          background: #302123;
          border-color: #63383d;
          color: #ff858c;
      }
      QToolButton#webModeButton:checked {
          background: #202a34;
          border-color: #365777;
          color: #7db9ec;
      }
      QFrame#headerPanel[themeMode="audio"],
      QFrame#headerPanel[themeMode="video"],
      QFrame#headerPanel[themeMode="live"] {
          background: transparent;
          border: none;
          border-radius: 0;
      }
      QLabel#titleLabel[themeMode="audio"],
      QLabel#titleLabel[themeMode="video"],
      QLabel#titleLabel[themeMode="live"] {
          color: #f0f2f4;
          font-family: "Segoe UI Semibold";
          font-size: 20px;
          font-weight: 600;
      }
      QLabel#subtitleLabel[themeMode="audio"],
      QLabel#subtitleLabel[themeMode="video"],
      QLabel#subtitleLabel[themeMode="live"] {
          color: #7f8790;
          font-size: 11px;
      }
      QWidget#mediaDisplay[themeMode="audio"],
      QWidget#mediaDisplay[themeMode="video"],
      QWidget#mediaDisplay[themeMode="live"] {
          background: #07080a;
          border: 1px solid #292d33;
          border-radius: 6px;
      }
      QFrame#playlistPanel[themeMode="audio"],
      QFrame#playlistPanel[themeMode="video"],
      QFrame#playlistPanel[themeMode="live"] {
          background: #15171b;
          border: 1px solid #292d33;
          border-radius: 6px;
      }
      QLabel#playlistTitleLabel[themeMode="audio"],
      QLabel#playlistTitleLabel[themeMode="video"],
      QLabel#playlistTitleLabel[themeMode="live"] {
          color: #e6e9ec;
          font-weight: 600;
      }
      QPushButton[compactAction="true"] {
          background: transparent;
          border: 1px solid #3b4652;
          border-radius: 4px;
          color: #9fc9ee;
          min-width: 68px;
          padding: 5px 9px;
      }
      QPushButton[compactAction="true"]:hover:enabled {
          background: #203040;
          border-color: #4f789c;
          color: #d9efff;
      }
      QFrame#livePlaylistTools[themeMode="audio"],
      QFrame#livePlaylistTools[themeMode="video"],
      QFrame#livePlaylistTools[themeMode="live"] {
          background: transparent;
          border: none;
          border-radius: 0;
      }
      QLabel#livePlaylistSourceLabel[themeMode="audio"],
      QLabel#livePlaylistSourceLabel[themeMode="video"],
      QLabel#livePlaylistSourceLabel[themeMode="live"] {
          color: #8d949d;
          font-weight: 600;
      }
      QLineEdit#livePlaylistUrlEdit[themeMode="audio"],
      QLineEdit#livePlaylistUrlEdit[themeMode="video"],
      QLineEdit#livePlaylistUrlEdit[themeMode="live"],
      QLineEdit#livePlaylistSearchEdit[themeMode="audio"],
      QLineEdit#livePlaylistSearchEdit[themeMode="video"],
      QLineEdit#livePlaylistSearchEdit[themeMode="live"] {
          background: #0e1013;
          border: 1px solid #343941;
          border-radius: 4px;
          color: #dfe2e5;
          font-family: "Microsoft YaHei UI";
          padding: 6px 8px;
          selection-background-color: #365d7d;
      }
      QLineEdit#livePlaylistUrlEdit:focus,
      QLineEdit#livePlaylistSearchEdit:focus {
          border-color: #4d8fc6;
      }
      QLabel#livePlaylistStatusLabel[themeMode="audio"],
      QLabel#livePlaylistStatusLabel[themeMode="video"],
      QLabel#livePlaylistStatusLabel[themeMode="live"] {
          background: transparent;
          border: none;
          color: #8d949c;
          padding: 2px 0;
      }
      QPushButton#livePlaylistLoadButton[themeMode="live"] {
          background: #8f343a;
          border: 1px solid #b84b53;
          border-radius: 4px;
          color: #ffffff;
      }
      QPushButton#livePlaylistLoadButton[themeMode="live"]:hover:enabled {
          background: #a53d45;
          border-color: #d05b64;
      }
      QPushButton#livePlaylistLocateButton[themeMode="live"] {
          background: #202328;
          border: 1px solid #393e45;
          border-radius: 4px;
          color: #c9cdd1;
      }
      QListView#playlistView[themeMode="audio"],
      QListView#playlistView[themeMode="video"],
      QListView#playlistView[themeMode="live"] {
          background: #101216;
          border: 1px solid #25292f;
          border-radius: 4px;
          color: #cbd0d5;
          outline: none;
          padding: 0;
      }
      QListView#playlistView::item,
      QListView#playlistView[themeMode="live"]::item {
          background: transparent;
          border: none;
          border-bottom: 1px solid #20242a;
          border-radius: 0;
          min-height: 18px;
          padding: 4px 7px;
      }
      QListView#playlistView::item:hover:!selected,
      QListView#playlistView[themeMode="live"]::item:hover:!selected {
          background: #1a1e23;
          color: #f0f2f4;
      }
      QListView#playlistView::item:selected {
          background: #233443;
          border-left: 3px solid #55aef2;
          color: #f5f8fa;
      }
      QListView#playlistView[themeMode="live"]::item:selected {
          background: #352124;
          border-left: 3px solid #e85d66;
          color: #fff0f1;
      }
      QListView#playlistView::item:disabled {
          background: #2b1c1f;
          color: #e69ca1;
          font-weight: 600;
      }
      QListView#playlistView QScrollBar:vertical {
          background: #101216;
          border: none;
          margin: 0;
          width: 8px;
      }
      QListView#playlistView QScrollBar::handle:vertical {
          background: #3b4047;
          border-radius: 3px;
          min-height: 24px;
      }
      QListView#playlistView QScrollBar::handle:vertical:hover {
          background: #59616a;
      }
      QListView#playlistView QScrollBar::add-line:vertical,
      QListView#playlistView QScrollBar::sub-line:vertical {
          height: 0;
      }
      QListView#playlistView QScrollBar:horizontal {
          background: #101216;
          border: none;
          height: 8px;
          margin: 0;
      }
      QListView#playlistView QScrollBar::handle:horizontal {
          background: #3b4047;
          border-radius: 3px;
          min-width: 24px;
      }
      QListView#playlistView QScrollBar::handle:horizontal:hover {
          background: #59616a;
      }
      QListView#playlistView QScrollBar::add-line:horizontal,
      QListView#playlistView QScrollBar::sub-line:horizontal {
          width: 0;
      }
      QListView#playlistView QScrollBar::add-page:horizontal,
      QListView#playlistView QScrollBar::sub-page:horizontal {
          background: #101216;
      }
      QFrame#playerDock {
          background: #121419;
          border: 1px solid #292d33;
          border-radius: 6px;
      }
      QFrame#playlistPanel[customBackground="true"] {
          background: rgba(21, 23, 27, 226);
      }
      QFrame#playerDock[customBackground="true"] {
          background: rgba(18, 20, 25, 226);
      }
      QWidget#mediaDisplay[customBackground="true"] {
          background: rgba(7, 8, 10, 238);
      }
      QFrame#mediaCard[themeMode="audio"],
      QFrame#mediaCard[themeMode="video"],
      QFrame#mediaCard[themeMode="live"] {
          background: transparent;
          border: none;
          border-bottom: 1px solid #24282e;
          border-radius: 0;
      }
      QFrame#transportPanel[themeMode="audio"],
      QFrame#transportPanel[themeMode="video"],
      QFrame#transportPanel[themeMode="live"] {
          background: transparent;
          border: none;
          border-radius: 0;
      }
      QLabel#captionLabel[themeMode="audio"],
      QLabel#captionLabel[themeMode="video"],
      QLabel#captionLabel[themeMode="live"],
      QLabel#positionLabel[themeMode="audio"],
      QLabel#positionLabel[themeMode="video"],
      QLabel#positionLabel[themeMode="live"] {
          color: #7f8790;
          font-size: 11px;
          font-weight: 600;
      }
      QLabel#currentMediaLabel[themeMode="audio"],
      QLabel#currentMediaLabel[themeMode="video"],
      QLabel#currentMediaLabel[themeMode="live"] {
          color: #eceff1;
          font-size: 14px;
          font-weight: 600;
      }
      QLabel#playbackStatusLabel[themeMode="audio"],
      QLabel#playbackStatusLabel[themeMode="video"],
      QLabel#playbackStatusLabel[themeMode="live"] {
          background: transparent;
          border: none;
          border-radius: 0;
          color: #72baf4;
          font-size: 11px;
          font-weight: 600;
          min-width: 0;
          padding: 0;
      }
      QLabel#playbackStatusLabel[themeMode="live"] {
          color: #ff7a82;
      }
      QLabel#playbackErrorLabel,
      QLabel#playbackErrorLabel[themeMode="audio"] {
          background: #321d20;
          border: 1px solid #63343a;
          border-radius: 4px;
          color: #f0b2b7;
          padding: 6px 8px;
      }
      QToolButton[transportControl="true"][themeMode="audio"],
      QToolButton[transportControl="true"][themeMode="video"],
      QToolButton[transportControl="true"][themeMode="live"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 4px;
      }
      QToolButton[transportControl="true"]:hover:enabled {
          background: #282c32;
          border-color: #3b4149;
      }
      QToolButton[transportControl="true"]:pressed:enabled {
          background: #343941;
      }
      QToolButton[primaryTransport="true"],
      QToolButton[primaryTransport="true"][themeMode="audio"],
      QToolButton[primaryTransport="true"][themeMode="video"],
      QToolButton[primaryTransport="true"][themeMode="live"] {
          background: #397aa8;
          border-color: #4b91c2;
          border-radius: 20px;
      }
      QToolButton[primaryTransport="true"]:hover:enabled {
          background: #448bbb;
          border-color: #62a9d8;
      }
      QToolButton[optionSelector="true"][themeMode="audio"],
      QToolButton[optionSelector="true"][themeMode="video"],
      QToolButton[optionSelector="true"][themeMode="live"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 4px;
          color: #aeb4bb;
          font-size: 11px;
          font-weight: 600;
          min-width: 48px;
          padding: 6px 8px;
      }
      QToolButton[optionSelector="true"]:hover:enabled {
          background: #282c32;
          border-color: #3b4149;
          color: #f0f2f4;
      }
      QToolButton[lyricsControl="true"],
      QToolButton[lyricsControl="true"][themeMode="video"],
      QToolButton[lyricsControl="true"][themeMode="live"] {
          color: #aeb4bb;
      }
      QToolButton[lyricsControl="true"]:checked {
          background: #25425a;
          border-color: #3d7199;
          color: #9ed5ff;
      }
      QToolButton[playlistToggle="true"] {
          background: #15171b;
          border: 1px solid #292d33;
          border-radius: 4px;
      }
      QSlider::groove:horizontal,
      QSlider[themeMode="audio"]::groove:horizontal {
          background: #343940;
          border-radius: 1px;
          height: 3px;
      }
      QSlider::sub-page:horizontal,
      QSlider[themeMode="live"]::sub-page:horizontal {
          background: #5baeea;
          border-radius: 1px;
      }
      QSlider[themeMode="live"]::sub-page:horizontal {
          background: #e65d66;
      }
      QSlider::handle:horizontal,
      QSlider[themeMode="live"]::handle:horizontal {
          background: #f2f4f5;
          border: none;
          border-radius: 5px;
          margin: -4px 0;
          width: 10px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="compact"],
      QLabel#livePlaylistSourceLabel[responsiveSize="normal"],
      QLabel#livePlaylistSourceLabel[responsiveSize="large"],
      QLabel#livePlaylistSourceLabel[responsiveSize="extraLarge"] {
          font-size: 11px;
      }
  )");
  return styleSheet;
}

UiThemePalette resolvedThemePalette(const ThemeSettings& settings) {
  const ThemeSettings normalized = normalizedThemeSettings(settings);
  const bool isLight = normalized.appearanceMode == QStringLiteral("light");
  UiThemePalette palette;
  palette.isDark = !isLight;

  if (isLight) {
    if (normalized.accentKey == QStringLiteral("blue")) {
      palette = {QColor("#edf5fb"), QColor("#fbfdff"), QColor("#ffffff"),
                 QColor("#f1f7fc"), QColor("#e5eff7"), QColor("#233746"),
                 QColor("#617889"), QColor("#c7d9e6"), QColor("#e4f0f8"),
                 QColor("#2f77b5"), QColor("#27689f"), QColor("#5d9fd1"),
                 false};
    } else if (normalized.accentKey == QStringLiteral("green")) {
      palette = {QColor("#eef6f1"), QColor("#fbfdfb"), QColor("#ffffff"),
                 QColor("#f0f7f3"), QColor("#e3eee7"), QColor("#263b31"),
                 QColor("#667d70"), QColor("#c9dacf"), QColor("#e3f0e8"),
                 QColor("#2f8c68"), QColor("#27785a"), QColor("#70a98c"),
                 false};
    } else if (normalized.accentKey == QStringLiteral("orange")) {
      palette = {QColor("#f7f1e7"), QColor("#fffaf2"), QColor("#fffdf8"),
                 QColor("#f4ebdd"), QColor("#eee3d2"), QColor("#3c3328"),
                 QColor("#7e7060"), QColor("#dfcfba"), QColor("#f1e4d2"),
                 QColor("#b47736"), QColor("#9e642d"), QColor("#c96758"),
                 false};
    } else if (normalized.accentKey == QStringLiteral("rose")) {
      palette = {QColor("#f8eeee"), QColor("#fff9f8"), QColor("#fffdfc"),
                 QColor("#f6e9ea"), QColor("#efe0e2"), QColor("#412f34"),
                 QColor("#826c72"), QColor("#e1cdd1"), QColor("#f3e2e4"),
                 QColor("#b85663"), QColor("#9f4854"), QColor("#d18b63"),
                 false};
    } else {
      palette = {QColor("#eef4f7"), QColor("#f9fbfc"), QColor("#ffffff"),
                 QColor("#f2f7f9"), QColor("#e5eef2"), QColor("#26343d"),
                 QColor("#667985"), QColor("#cbd8df"), QColor("#e4eef3"),
                 QColor("#347fae"), QColor("#2b6f99"), QColor("#4fae99"),
                 false};
    }
  } else if (normalized.accentKey == QStringLiteral("blue")) {
    palette = {QColor("#0d1720"), QColor("#09131b"), QColor("#12212c"),
               QColor("#162936"), QColor("#081118"), QColor("#e8f1f7"),
               QColor("#8fa4b3"), QColor("#274052"), QColor("#1b3444"),
               QColor("#56a8dd"), QColor("#6bb8e8"), QColor("#69c2b0"),
               true};
  } else if (normalized.accentKey == QStringLiteral("green")) {
    palette = {QColor("#101815"), QColor("#0b120f"), QColor("#16221d"),
               QColor("#1b2b24"), QColor("#09120e"), QColor("#e9f1ed"),
               QColor("#91a49a"), QColor("#2b4136"), QColor("#20342b"),
               QColor("#58b989"), QColor("#6bc99b"), QColor("#8ebc72"),
               true};
  } else if (normalized.accentKey == QStringLiteral("orange")) {
    palette = {QColor("#1b1510"), QColor("#130f0b"), QColor("#241b14"),
               QColor("#2e2117"), QColor("#110d09"), QColor("#f2ece5"),
               QColor("#aa9b8c"), QColor("#493629"), QColor("#38291f"),
               QColor("#e0a15a"), QColor("#edb46f"), QColor("#c97965"),
               true};
  } else if (normalized.accentKey == QStringLiteral("rose")) {
    palette = {QColor("#1a1215"), QColor("#120c0f"), QColor("#24171c"),
               QColor("#2e1d23"), QColor("#10090c"), QColor("#f2eaed"),
               QColor("#aa969d"), QColor("#493039"), QColor("#38242b"),
               QColor("#df7d8a"), QColor("#ec919e"), QColor("#d4a56a"),
               true};
  } else {
    palette = {QColor("#0e1013"), QColor("#0b0d10"), QColor("#15171b"),
               QColor("#1b1e23"), QColor("#07080a"), QColor("#e7e9ec"),
               QColor("#8f969f"), QColor("#292d33"), QColor("#20242a"),
               QColor("#55aef2"), QColor("#68baf5"), QColor("#e06a72"),
               true};
  }

  if (normalized.accentKey == QStringLiteral("custom")) {
    palette.accent = QColor(normalized.customAccentColor);
    palette.accentHover = palette.isDark ? palette.accent.lighter(112)
                                         : palette.accent.darker(112);
  }
  return palette;
}

void applyTablePalette(QTableWidget* const table,
                       const UiThemePalette& palette) {
  if (table == nullptr) {
    return;
  }

  QPalette themedPalette = table->palette();
  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    themedPalette.setColor(group, QPalette::Window, palette.canvas);
    themedPalette.setColor(group, QPalette::Base, palette.canvas);
    themedPalette.setColor(group, QPalette::AlternateBase, palette.panel);
    themedPalette.setColor(group, QPalette::Text,
                           group == QPalette::Disabled ? palette.mutedText
                                                       : palette.text);
    themedPalette.setColor(group, QPalette::WindowText, palette.text);
    themedPalette.setColor(group, QPalette::Button, palette.panelAlt);
    themedPalette.setColor(group, QPalette::ButtonText, palette.mutedText);
    themedPalette.setColor(group, QPalette::Highlight, palette.accent);
    themedPalette.setColor(group, QPalette::HighlightedText,
                           QColor(Qt::white));
  }

  table->setPalette(themedPalette);
  table->viewport()->setPalette(themedPalette);
  table->viewport()->setAutoFillBackground(true);
  table->horizontalHeader()->setPalette(themedPalette);
  table->horizontalHeader()->setAutoFillBackground(true);
  table->verticalHeader()->setPalette(themedPalette);
  table->verticalHeader()->setAutoFillBackground(true);
}

namespace {

QString rgbaColor(const QColor& color, const int alpha) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(alpha);
}

}  // namespace

QString themeOverrideStyleSheet(const ThemeSettings& settings) {
  const ThemeSettings normalized = normalizedThemeSettings(settings);
  if (normalized.appearanceMode == QStringLiteral("dark") &&
      normalized.accentKey == QStringLiteral("default")) {
    return QString{};
  }

  const UiThemePalette palette = resolvedThemePalette(normalized);
  QString style = QStringLiteral(R"(
      QMainWindow[themeMode="audio"],
      QMainWindow[themeMode="video"],
      QMainWindow[themeMode="live"],
      QWidget#centralSurface[themeMode="audio"],
      QWidget#centralSurface[themeMode="video"],
      QWidget#centralSurface[themeMode="live"] {
          background: @WINDOW@;
          color: @TEXT@;
      }
      QFrame#displayModePanel {
          background: @CHROME@;
          border-bottom: 1px solid @BORDER@;
      }
      QLabel#brandLabel,
      QLabel#titleLabel[themeMode="audio"],
      QLabel#titleLabel[themeMode="video"],
      QLabel#titleLabel[themeMode="live"],
      QLabel#playlistTitleLabel,
      QLabel#currentMediaLabel {
          color: @TEXT@;
      }
      QLabel#subtitleLabel,
      QLabel#captionLabel,
      QLabel#positionLabel,
      QLabel#playbackStatusLabel,
      QLabel#livePlaylistSourceLabel,
      QLabel#livePlaylistStatusLabel {
          color: @MUTED@;
      }
      QToolButton[topChromeButton="true"] {
          background: transparent;
          border: 1px solid transparent;
      }
      QToolButton[topChromeButton="true"]:hover,
      QToolButton[topChromeButton="true"]:checked {
          background: @HOVER@;
          border-color: @BORDER@;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"] {
          color: @MUTED@;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"]:hover:!checked {
          background: @HOVER@;
          border-color: @BORDER@;
          color: @TEXT@;
      }
      QFrame#displayModeRail QToolButton[modeSegment="true"]:checked {
          background: @ACCENT_SOFT@;
          border-color: @ACCENT@;
          color: @ACCENT@;
      }
      QToolButton#liveModeButton:checked {
          background: @SECONDARY_SOFT@;
          border-color: @SECONDARY@;
          color: @SECONDARY@;
      }
      QMenu#topFileMenu,
      QMenu#topViewMenu,
      QMenu#topHelpMenu,
      QMenu#recentLocalMediaMenu,
      QMenu#optionPopup,
      QMenu#playlistContextMenu,
      QMenu#livePlaylistContextMenu {
          background: @PANEL@;
          border: 1px solid @BORDER@;
          color: @TEXT@;
      }
      QMenu#topFileMenu::item:selected,
      QMenu#topViewMenu::item:selected,
      QMenu#topHelpMenu::item:selected,
      QMenu#recentLocalMediaMenu::item:selected,
      QMenu#optionPopup::item:selected,
      QMenu#playlistContextMenu::item:selected,
      QMenu#livePlaylistContextMenu::item:selected {
          background: @ACCENT_SOFT@;
          color: @TEXT@;
      }
      QMenu#optionPopup::item:checked {
          background: @ACCENT@;
          color: #ffffff;
      }
      QWidget#mediaDisplay[themeMode="audio"],
      QWidget#mediaDisplay[themeMode="video"],
      QWidget#mediaDisplay[themeMode="live"],
      QFrame#playlistPanel[themeMode="audio"],
      QFrame#playlistPanel[themeMode="video"],
      QFrame#playlistPanel[themeMode="live"],
      QFrame#playerDock[themeMode="audio"],
      QFrame#playerDock[themeMode="video"],
      QFrame#playerDock[themeMode="live"] {
          background: @PANEL@;
          border-color: @BORDER@;
      }
      QWidget#mediaDisplay[themeMode="audio"],
      QWidget#mediaDisplay[themeMode="video"],
      QWidget#mediaDisplay[themeMode="live"] {
          background: @CANVAS@;
      }
      QFrame#playerDock[themeMode="audio"],
      QFrame#playerDock[themeMode="video"],
      QFrame#playerDock[themeMode="live"] {
          background: @PANEL_ALT@;
      }
      QFrame#playlistPanel[themeMode="audio"][customBackground="true"],
      QFrame#playlistPanel[themeMode="video"][customBackground="true"],
      QFrame#playlistPanel[themeMode="live"][customBackground="true"],
      QFrame#playerDock[themeMode="audio"][customBackground="true"],
      QFrame#playerDock[themeMode="video"][customBackground="true"],
      QFrame#playerDock[themeMode="live"][customBackground="true"] {
          background: @PANEL_TRANSLUCENT@;
      }
      QWidget#mediaDisplay[themeMode="audio"][customBackground="true"],
      QWidget#mediaDisplay[themeMode="video"][customBackground="true"],
      QWidget#mediaDisplay[themeMode="live"][customBackground="true"] {
          background: @CANVAS_TRANSLUCENT@;
      }
      QLineEdit#livePlaylistUrlEdit[themeMode="audio"],
      QLineEdit#livePlaylistUrlEdit[themeMode="video"],
      QLineEdit#livePlaylistUrlEdit[themeMode="live"],
      QLineEdit#livePlaylistSearchEdit[themeMode="audio"],
      QLineEdit#livePlaylistSearchEdit[themeMode="video"],
      QLineEdit#livePlaylistSearchEdit[themeMode="live"],
      QListView#playlistView[themeMode="audio"],
      QListView#playlistView[themeMode="video"],
      QListView#playlistView[themeMode="live"] {
          background: @CANVAS@;
          border-color: @BORDER@;
          color: @TEXT@;
          selection-background-color: @ACCENT@;
      }
      QLineEdit#livePlaylistUrlEdit:focus,
      QLineEdit#livePlaylistSearchEdit:focus {
          border-color: @ACCENT@;
      }
      QListView#playlistView::item {
          border-bottom-color: @BORDER@;
      }
      QListView#playlistView::item:hover:!selected {
          background: @HOVER@;
          color: @TEXT@;
      }
      QListView#playlistView::item:selected {
          background: @ACCENT_SOFT@;
          border-left: 3px solid @ACCENT@;
          color: @TEXT@;
      }
      QListView#playlistView[themeMode="live"]::item:selected {
          background: @SECONDARY_SOFT@;
          border-left: 3px solid @SECONDARY@;
          color: @TEXT@;
      }
      QListView#playlistView QScrollBar:vertical,
      QListView#playlistView QScrollBar:horizontal,
      QListView#playlistView QScrollBar::add-page:horizontal,
      QListView#playlistView QScrollBar::sub-page:horizontal {
          background: @CANVAS@;
      }
      QListView#playlistView QScrollBar::handle:vertical,
      QListView#playlistView QScrollBar::handle:horizontal {
          background: @MUTED@;
      }
      QFrame#volumePopup[themeMode="audio"],
      QFrame#volumePopup[themeMode="video"],
      QFrame#volumePopup[themeMode="live"] {
          background: @PANEL@;
          border-color: @BORDER@;
      }
      QToolButton[transportControl="true"],
      QToolButton[optionSelector="true"] {
          color: @TEXT@;
      }
      QToolButton[transportControl="true"]:hover:enabled,
      QToolButton[optionSelector="true"]:hover:enabled {
          background: @HOVER@;
          border-color: @BORDER@;
      }
      QToolButton[primaryTransport="true"],
      QToolButton[lyricsControl="true"]:checked {
          background: @ACCENT@;
          border-color: @ACCENT_HOVER@;
          color: #ffffff;
      }
      QToolButton[primaryTransport="true"]:hover:enabled {
          background: @ACCENT_HOVER@;
      }
      QSlider::groove:horizontal {
          background: @BORDER@;
      }
      QSlider::sub-page:horizontal,
      QSlider[themeMode="live"]::sub-page:horizontal {
          background: @ACCENT@;
      }
      QSlider::handle:horizontal {
          background: @TEXT@;
          border-color: @ACCENT@;
      }
      QPushButton[compactAction="true"] {
          color: @ACCENT@;
          border-color: @BORDER@;
      }
      QPushButton[compactAction="true"]:hover:enabled {
          background: @ACCENT_SOFT@;
          border-color: @ACCENT@;
          color: @TEXT@;
      }
  )");
  style.replace(QStringLiteral("@WINDOW@"), palette.window.name());
  style.replace(QStringLiteral("@CHROME@"), palette.chrome.name());
  style.replace(QStringLiteral("@PANEL@"), palette.panel.name());
  style.replace(QStringLiteral("@PANEL_ALT@"), palette.panelAlt.name());
  style.replace(QStringLiteral("@CANVAS@"), palette.canvas.name());
  style.replace(QStringLiteral("@TEXT@"), palette.text.name());
  style.replace(QStringLiteral("@MUTED@"), palette.mutedText.name());
  style.replace(QStringLiteral("@BORDER@"), palette.border.name());
  style.replace(QStringLiteral("@HOVER@"), palette.hover.name());
  style.replace(QStringLiteral("@ACCENT@"), palette.accent.name());
  style.replace(QStringLiteral("@ACCENT_HOVER@"),
                palette.accentHover.name());
  style.replace(QStringLiteral("@SECONDARY@"), palette.secondary.name());
  style.replace(QStringLiteral("@ACCENT_SOFT@"),
                rgbaColor(palette.accent, palette.isDark ? 46 : 34));
  style.replace(QStringLiteral("@SECONDARY_SOFT@"),
                rgbaColor(palette.secondary, palette.isDark ? 48 : 36));
  style.replace(QStringLiteral("@PANEL_TRANSLUCENT@"),
                rgbaColor(palette.panel, palette.isDark ? 214 : 226));
  style.replace(QStringLiteral("@CANVAS_TRANSLUCENT@"),
                rgbaColor(palette.canvas, palette.isDark ? 150 : 166));
  return style;
}

}  // namespace mediahub::gui
