#include "ui_theme.h"

#include <QColor>
#include <QDialog>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QProgressBar>
#include <QStringList>
#include <QTest>
#include <QWidget>

namespace mediahub::gui {

class UiThemeTest final : public QObject {
    Q_OBJECT

 private slots:
    void coversBrowserAuxiliarySurfaces();
    void distinguishesPrimaryWarningAndDestructiveActions();
    void appliesRepresentativeBrowserSurfaceColors();
    void keepsBrowserAuxiliarySurfacesResponsive();
};

void UiThemeTest::coversBrowserAuxiliarySurfaces() {
    const QString& styleSheet = mainWindowStyleSheet();
    const QStringList requiredSelectors{
        QStringLiteral("#browserChrome"),
        QStringLiteral("#displayModeRail"),
        QStringLiteral("#browserTabStrip"),
        QStringLiteral("#browserToolbar"),
        QStringLiteral("#browserNavigationBar"),
        QStringLiteral("#browserAddressContainer"),
        QStringLiteral("#browserSidePanel"),
        QStringLiteral("#browserSidePanelTitleBar"),
        QStringLiteral("#browserSidePanelStack"),
        QStringLiteral("#browserMoreMenu"),
        QStringLiteral("#browserDownloadButton"),
        QStringLiteral("#browserHistoryDialog"),
        QStringLiteral("#browserHistoryClearDialog"),
        QStringLiteral("#browserFavoritesDialog"),
        QStringLiteral("#browserFavoriteImportDialog"),
        QStringLiteral("#browserFavoriteEditorDialog"),
        QStringLiteral("#browserAudioTabsDialog"),
        QStringLiteral("#browserTabSearchDialog"),
        QStringLiteral("#browserTabGroupDialog"),
        QStringLiteral("#browserPinnedCloseDialog"),
        QStringLiteral("#browserPermissionDialog"),
        QStringLiteral("#browserPermissionManagementDialog"),
        QStringLiteral("#browserStartupSettingsDialog"),
        QStringLiteral("#browserActiveDownloadExitDialog"),
        QStringLiteral("#browserExternalProtocolDialog"),
        QStringLiteral("#browserCertificateDialog"),
        QStringLiteral("#browserClearDataDialog"),
        QStringLiteral("#browserHistoryList"),
        QStringLiteral("#browserFavoritesList"),
        QStringLiteral("#browserHistorySearchEdit"),
        QStringLiteral("#browserFavoritesSearchEdit"),
        QStringLiteral("#browserAudioTabsList"),
        QStringLiteral("#browserTabSearchList"),
        QStringLiteral("#browserTabSearchEdit"),
        QStringLiteral("#browserStartupSettingsExplanation"),
        QStringLiteral("#browserHomeUrlEdit"),
        QStringLiteral("#browserStartupModeCombo"),
        QStringLiteral("#browserStartupUrlsList"),
        QStringLiteral("#browserStartupUrlEdit"),
        QStringLiteral("#browserStartupSaveButton"),
        QStringLiteral("#browserStartupRemoveButton"),
        QStringLiteral("#browserPinnedCloseConfirmButton"),
        QStringLiteral("#browserActiveDownloadExitButton"),
        QStringLiteral("#browserFindBar"),
        QStringLiteral("#browserTabContextMenu"),
        QStringLiteral("#browserDownloadCenter"),
        QStringLiteral("#browserDownloadCenterTitleLabel"),
        QStringLiteral("#browserDownloadCenterSummaryLabel"),
        QStringLiteral("#browserDownloadClearCompletedButton"),
        QStringLiteral("#browserDownloadRetryButton"),
        QStringLiteral("#browserDownloadCenterScrollArea"),
        QStringLiteral("#browserDownloadCenterContent"),
        QStringLiteral("#browserDownloadCenterEmptyLabel"),
        QStringLiteral("#browserDownloadWidget"),
        QStringLiteral("#browserDownloadProgressBar"),
        QStringLiteral("#browserMaximumTabCountSpin"),
    };

    for (const QString& selector : requiredSelectors) {
        QVERIFY2(styleSheet.contains(selector), qPrintable(selector));
    }
}

void UiThemeTest::distinguishesPrimaryWarningAndDestructiveActions() {
    const QString& styleSheet = mainWindowStyleSheet();

    QVERIFY(styleSheet.contains(
        QStringLiteral("QDialog QPushButton#browserFavoriteSaveButton")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("QDialog QPushButton#browserCertificateSafetyButton")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("QDialog QPushButton#browserStartupSaveButton")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("QDialog QPushButton#browserClearDataConfirmButton")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("QDialog QPushButton#browserStartupRemoveButton")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("QDialog QPushButton#browserCertificateContinueButton")));
    QVERIFY(styleSheet.contains(QStringLiteral("background: #168a5d")));
    QVERIFY(styleSheet.contains(QStringLiteral("background: #fff0ee")));
    QVERIFY(styleSheet.contains(QStringLiteral("background: #fff4df")));
}

void UiThemeTest::appliesRepresentativeBrowserSurfaceColors() {
    QDialog historyDialog;
    historyDialog.setObjectName(QStringLiteral("browserHistoryDialog"));
    auto* const historyList = new QListWidget(&historyDialog);
    historyList->setObjectName(QStringLiteral("browserHistoryList"));
    historyDialog.setStyleSheet(mainWindowStyleSheet());
    historyDialog.ensurePolished();
    historyList->ensurePolished();

    QCOMPARE(historyDialog.palette().color(QPalette::Window), QColor("#f7f9fb"));
    QCOMPARE(historyDialog.palette().color(QPalette::WindowText),
             QColor("#1f2d3a"));
    QCOMPARE(historyList->palette().color(QPalette::Base), QColor("#ffffff"));
    QCOMPARE(historyList->palette().color(QPalette::Text), QColor("#293847"));

    QMenu tabMenu;
    tabMenu.setObjectName(QStringLiteral("browserTabContextMenu"));
    tabMenu.setStyleSheet(mainWindowStyleSheet());
    tabMenu.ensurePolished();
    QCOMPARE(tabMenu.palette().color(QPalette::Window), QColor("#ffffff"));

    QWidget downloadWidget;
    downloadWidget.setObjectName(QStringLiteral("browserDownloadWidget"));
    auto* const progressBar = new QProgressBar(&downloadWidget);
    progressBar->setObjectName(QStringLiteral("browserDownloadProgressBar"));
    downloadWidget.setStyleSheet(mainWindowStyleSheet());
    downloadWidget.ensurePolished();
    progressBar->ensurePolished();
    QCOMPARE(downloadWidget.palette().color(QPalette::Window),
             QColor("#ffffff"));
    QCOMPARE(progressBar->palette().color(QPalette::Base), QColor("#dce4eb"));

    QWidget downloadCenter;
    downloadCenter.setObjectName(QStringLiteral("browserDownloadCenter"));
    downloadCenter.setStyleSheet(mainWindowStyleSheet());
    downloadCenter.ensurePolished();
    QCOMPARE(downloadCenter.palette().color(QPalette::Window),
             QColor("#f7f9fb"));

    QFrame browserChrome;
    browserChrome.setObjectName(QStringLiteral("browserChrome"));
    browserChrome.setStyleSheet(mainWindowStyleSheet());
    browserChrome.ensurePolished();
    QCOMPARE(browserChrome.palette().color(QPalette::Window),
             QColor("#f4f6f8"));
    QCOMPARE(browserChrome.palette().color(QPalette::WindowText),
             QColor("#24313d"));

    QFrame browserSidePanel;
    browserSidePanel.setObjectName(QStringLiteral("browserSidePanel"));
    browserSidePanel.setStyleSheet(mainWindowStyleSheet());
    browserSidePanel.ensurePolished();
    QCOMPARE(browserSidePanel.palette().color(QPalette::Window),
             QColor("#f7f9fb"));

    QFrame browserSidePanelTitleBar;
    browserSidePanelTitleBar.setObjectName(
        QStringLiteral("browserSidePanelTitleBar"));
    browserSidePanelTitleBar.setStyleSheet(mainWindowStyleSheet());
    browserSidePanelTitleBar.ensurePolished();
    QCOMPARE(browserSidePanelTitleBar.palette().color(QPalette::Window),
             QColor("#dce4eb"));
}

void UiThemeTest::keepsBrowserAuxiliarySurfacesResponsive() {
    const QString& styleSheet = mainWindowStyleSheet();

    QVERIFY(styleSheet.contains(QStringLiteral(
        "QWidget#browserPage[responsiveSize=\"compact\"] QDialog")));
    QVERIFY(styleSheet.contains(QStringLiteral(
        "QFrame#browserFindBar[responsiveSize=\"compact\"]")));
    QVERIFY(styleSheet.contains(QStringLiteral(
        "QWidget#browserPage[responsiveSize=\"large\"] QDialog QListWidget")));
    QVERIFY(styleSheet.contains(QStringLiteral(
        "QWidget#browserPage[responsiveSize=\"extraLarge\"] QDialog QLineEdit")));
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::UiThemeTest)

#include "ui_theme_test.moc"
