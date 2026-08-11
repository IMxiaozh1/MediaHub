#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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

bool waitUntil(const std::function<bool()>& predicate, const qint64 timeoutMilliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return predicate();
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
    void reportsRuntimeStatusWithoutBlockingGuiThread();
};

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
}

}  // namespace
}  // namespace mediahub::browser_webview2

QTEST_MAIN(mediahub::browser_webview2::WebView2BrowserTest)

#include "webview2_browser_test.moc"
