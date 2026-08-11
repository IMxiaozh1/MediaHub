#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#include <QtTest>

#include <cstdint>
#include <functional>

#include "browser_event_listener.h"
#include "mediahub/browser_webview2/webview2_browser_backend.h"

namespace mediahub::browser_webview2 {
namespace {

constexpr std::uint64_t kGeneration = 7;
constexpr qint64 kInitializationTimeoutMilliseconds = 10000;

// 调用线程：GUI 主线程，在有界事件循环中等待明确谓词。
bool waitUntil(const std::function<bool()>& predicate, const qint64 timeoutMilliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return predicate();
}

// 调用线程：GUI 主线程，等待 WebView2 释放文件后清理测试专用 Profile。
bool removeTemporaryProfile(const QString& directory,
                            const qint64 timeoutMilliseconds) {
    const QString cleanDirectory =
        QDir::fromNativeSeparators(QDir::cleanPath(directory));
    const QString temporaryRoot =
        QDir::fromNativeSeparators(QDir::cleanPath(QDir::tempPath()));
    const QString expectedPrefix = temporaryRoot + QLatin1Char('/');
    if (!cleanDirectory.startsWith(expectedPrefix, Qt::CaseInsensitive) ||
        !QFileInfo(cleanDirectory).fileName().startsWith(
            QStringLiteral("mediahub_webview2_tests-"))) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    do {
        QDirIterator iterator(cleanDirectory,
                              QDir::AllEntries | QDir::NoDotAndDotDot |
                                  QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QFileDevice::Permissions permissions = QFile::permissions(path);
            static_cast<void>(
                QFile::setPermissions(path, permissions | QFileDevice::WriteOwner));
        }
        const QFileDevice::Permissions rootPermissions =
            QFile::permissions(cleanDirectory);
        static_cast<void>(QFile::setPermissions(
            cleanDirectory, rootPermissions | QFileDevice::WriteOwner));
        if (!QDir(cleanDirectory).exists() ||
            QDir(cleanDirectory).removeRecursively()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    } while (timer.elapsed() < timeoutMilliseconds);
    return !QDir(cleanDirectory).exists();
}

class RecordingBrowserListener final : public gui::BrowserEventListener {
 public:
    void onBrowserReady(const std::uint64_t generation) override {
        recordCallbackThread();
        generation_ = generation;
        isReady_ = true;
    }

    void onBrowserError(const std::uint64_t generation,
                        const gui::BrowserErrorKind kind,
                        const long errorCode) override {
        recordCallbackThread();
        generation_ = generation;
        errorKind_ = kind;
        errorCode_ = errorCode;
        hasError_ = true;
    }

    void onNavigationStarted(std::uint64_t) override { recordCallbackThread(); }

    void onNavigationCompleted(std::uint64_t, const QString&, const QString&, bool,
                               bool) override {
        recordCallbackThread();
    }

    void onFullScreenChanged(std::uint64_t, bool) override { recordCallbackThread(); }

    void onPermissionRequested(std::uint64_t, const QString&,
                               gui::BrowserPermissionKind) override {
        recordCallbackThread();
    }

    void onExternalProtocolRequested(std::uint64_t, const QString&) override {
        recordCallbackThread();
    }

    void onCertificateErrorRequested(std::uint64_t, const QString&,
                                     const QString&) override {
        recordCallbackThread();
    }

    void onDownloadRequested(std::uint64_t, const QString&, const QString&,
                             std::int64_t) override {
        recordCallbackThread();
    }

    void onDownloadUpdated(std::uint64_t, gui::BrowserDownloadState, std::int64_t,
                           std::int64_t) override {
        recordCallbackThread();
    }

    void onBrowsingDataCleared(std::uint64_t) override { recordCallbackThread(); }
    void onPopupRejected() override { recordCallbackThread(); }

    [[nodiscard]] bool reachedTerminalState() const noexcept {
        return isReady_ || hasError_;
    }

    [[nodiscard]] bool wasCalledFromWrongThread() const noexcept {
        return wasCalledFromWrongThread_;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool hasError() const noexcept { return hasError_; }
    [[nodiscard]] gui::BrowserErrorKind errorKind() const noexcept { return errorKind_; }
    [[nodiscard]] long errorCode() const noexcept { return errorCode_; }

 private:
    void recordCallbackThread() {
        if (QThread::currentThread() != QApplication::instance()->thread()) {
            wasCalledFromWrongThread_ = true;
        }
    }

    bool isReady_{false};
    bool hasError_{false};
    bool wasCalledFromWrongThread_{false};
    std::uint64_t generation_{0};
    gui::BrowserErrorKind errorKind_{gui::BrowserErrorKind::InitializationFailed};
    long errorCode_{0};
};

class WebView2BrowserTest final : public QObject {
    Q_OBJECT

 private slots:
    void requiresEveryDefaultDenyHandlerBeforeReady();
    void reportsRuntimeStatusWithoutBlockingGuiThread();
};

void WebView2BrowserTest::requiresEveryDefaultDenyHandlerBeforeReady() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());

    const int registerEventsStart = source.indexOf(QStringLiteral("HRESULT registerEvents()"));
    const int registerEventsEnd =
        source.indexOf(QStringLiteral("HRESULT registerNavigationStarting()"));
    QVERIFY(registerEventsStart >= 0);
    QVERIFY(registerEventsEnd > registerEventsStart);
    const QString registerEvents =
        source.mid(registerEventsStart, registerEventsEnd - registerEventsStart);

    const QStringList requiredRegistrations{
        QStringLiteral("registerPermissionRequested()"),
        QStringLiteral("registerDownloadStarting()"),
        QStringLiteral("registerServerCertificateErrorDetected()"),
        QStringLiteral("registerLaunchingExternalUriScheme()"),
        QStringLiteral("registerNewWindowRequested()"),
    };
    for (const QString& registration : requiredRegistrations) {
        QVERIFY2(registerEvents.contains(registration), qPrintable(registration));
    }

    const QStringList requiredSafetyMarkers{
        QStringLiteral("ICoreWebView2_4"),
        QStringLiteral("ICoreWebView2_14"),
        QStringLiteral("ICoreWebView2_18"),
        QStringLiteral("COREWEBVIEW2_PERMISSION_STATE_DENY"),
        QStringLiteral("COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL"),
        QStringLiteral("put_Cancel(TRUE)"),
        QStringLiteral("put_Handled(TRUE)"),
    };
    for (const QString& marker : requiredSafetyMarkers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
    }

    const QStringList requiredRaiiMarkers{
        QStringLiteral("permissionRequested_.bind"),
        QStringLiteral("downloadStarting_.bind"),
        QStringLiteral("serverCertificateErrorDetected_.bind"),
        QStringLiteral("launchingExternalUriScheme_.bind"),
        QStringLiteral("newWindowRequested_.bind"),
        QStringLiteral("permissionRequested_.reset"),
        QStringLiteral("downloadStarting_.reset"),
        QStringLiteral("serverCertificateErrorDetected_.reset"),
        QStringLiteral("launchingExternalUriScheme_.reset"),
        QStringLiteral("newWindowRequested_.reset"),
    };
    for (const QString& marker : requiredRaiiMarkers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
    }

    const int registerGate = source.indexOf(QStringLiteral("result = registerEvents();"));
    const int readyAssignment = source.indexOf(QStringLiteral("isReady_ = true;"));
    const int firstSafetyReset =
        source.indexOf(QStringLiteral("newWindowRequested_.reset();"));
    const int controllerClose =
        source.indexOf(QStringLiteral("controller_->Close()"), firstSafetyReset);
    QVERIFY(registerGate >= 0);
    QVERIFY(readyAssignment > registerGate);
    QVERIFY(firstSafetyReset >= 0);
    QVERIFY(controllerClose > firstSafetyReset);
}

void WebView2BrowserTest::reportsRuntimeStatusWithoutBlockingGuiThread() {
    QWidget testWindow;
    testWindow.resize(640, 360);
    testWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&testWindow, 5000));

    QTemporaryDir testProfile;
    QVERIFY(testProfile.isValid());

    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    backend.initialize(reinterpret_cast<void*>(testWindow.winId()), testProfile.path(),
                       kGeneration);

    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QCOMPARE(listener.generation(), kGeneration);
    QVERIFY(!listener.wasCalledFromWrongThread());
    if (listener.hasError()) {
        QVERIFY(listener.errorKind() == gui::BrowserErrorKind::RuntimeUnavailable ||
                listener.errorKind() == gui::BrowserErrorKind::InitializationFailed ||
                listener.errorKind() == gui::BrowserErrorKind::ProfileUnavailable);
        QVERIFY(listener.errorCode() < 0);
    }

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(removeTemporaryProfile(testProfile.path(), 5000));
}

}  // namespace
}  // namespace mediahub::browser_webview2

QTEST_MAIN(mediahub::browser_webview2::WebView2BrowserTest)

#include "webview2_browser_test.moc"
