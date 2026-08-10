#include "ui_theme.h"

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
      QListView#playlistView[responsiveSize="compact"],
      QLineEdit#livePlaylistUrlEdit[responsiveSize="compact"],
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
      QListView#playlistView[responsiveSize="normal"],
      QLineEdit#livePlaylistUrlEdit[responsiveSize="normal"],
      QLabel#livePlaylistStatusLabel[responsiveSize="normal"],
      QPushButton#livePlaylistLoadButton[responsiveSize="normal"],
      QPushButton#livePlaylistLocateButton[responsiveSize="normal"] {
          font-size: 13px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="normal"] {
          font-size: 11px;
      }
      QLabel#playlistTitleLabel[responsiveSize="large"] {
          font-size: 17px;
      }
      QTabBar#playlistKindTabs[responsiveSize="large"]::tab,
      QListView#playlistView[responsiveSize="large"],
      QLineEdit#livePlaylistUrlEdit[responsiveSize="large"],
      QLabel#livePlaylistStatusLabel[responsiveSize="large"],
      QPushButton#livePlaylistLoadButton[responsiveSize="large"],
      QPushButton#livePlaylistLocateButton[responsiveSize="large"] {
          font-size: 15px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="large"] {
          font-size: 12px;
      }
      QLabel#playlistTitleLabel[responsiveSize="extraLarge"] {
          font-size: 19px;
      }
      QTabBar#playlistKindTabs[responsiveSize="extraLarge"]::tab,
      QListView#playlistView[responsiveSize="extraLarge"],
      QLineEdit#livePlaylistUrlEdit[responsiveSize="extraLarge"],
      QLabel#livePlaylistStatusLabel[responsiveSize="extraLarge"],
      QPushButton#livePlaylistLoadButton[responsiveSize="extraLarge"],
      QPushButton#livePlaylistLocateButton[responsiveSize="extraLarge"] {
          font-size: 17px;
      }
      QLabel#livePlaylistSourceLabel[responsiveSize="extraLarge"] {
          font-size: 14px;
      }
  )");
  return styleSheet;
}

}  // namespace mediahub::gui
