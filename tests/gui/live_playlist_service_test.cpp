#include "live_playlist_service.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QSignalSpy>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <optional>
#include <utility>

namespace mediahub::gui {
namespace {

struct HttpReply {
  QByteArray response;
  int delayMilliseconds{0};
};

QByteArray makeHttpResponse(const int statusCode, const QByteArray &body,
                            const QByteArray &extraHeaders = {}) {
  const QByteArray reason = statusCode == 200 ? "OK" : "Error";
  QByteArray response =
      "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
  response += "Content-Type: application/x-mpegURL; charset=utf-8\r\n";
  response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  response += extraHeaders;
  response += "Connection: close\r\n\r\n";
  response += body;
  return response;
}

QByteArray makeRedirectResponse(const QByteArray &location) {
  return makeHttpResponse(302, {}, "Location: " + location + "\r\n");
}

class ScriptedHttpServer final : public QTcpServer {
public:
  explicit ScriptedHttpServer(QObject *const parent = nullptr)
      : QTcpServer(parent) {
    const bool isListening = listen(QHostAddress::LocalHost, 0);
    Q_ASSERT(isListening);
    connect(this, &QTcpServer::newConnection, this,
            &ScriptedHttpServer::acceptConnections);
  }

  void addReply(const QString &path, HttpReply reply) {
    replies_.insert(path, std::move(reply));
  }

  [[nodiscard]] QUrl url(const QString &path) const {
    return QUrl(
        QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
  }

  [[nodiscard]] bool hasCompletedReply(const QString &path) const {
    return completedReplies_.contains(path);
  }

  [[nodiscard]] bool hasReceivedRequest(const QString &path) const {
    return receivedRequests_.contains(path);
  }

private:
  void acceptConnections() {
    while (hasPendingConnections()) {
      QTcpSocket *const socket = nextPendingConnection();
      auto *const request = new QByteArray();
      connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request] {
        request->append(socket->readAll());
        const int headerEnd = request->indexOf("\r\n\r\n");
        if (headerEnd < 0) {
          return;
        }
        const int methodEnd = request->indexOf(' ');
        const int pathEnd = request->indexOf(' ', methodEnd + 1);
        const QString path = QString::fromLatin1(
            request->mid(methodEnd + 1, pathEnd - methodEnd - 1));
        receivedRequests_.append(path);
        const HttpReply reply = replies_.value(
            path, HttpReply{makeHttpResponse(404, "missing"), 0});
        disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
        QTimer::singleShot(reply.delayMilliseconds, socket,
                           [this, socket, path, response = reply.response] {
                             completedReplies_.append(path);
                             if (socket->state() ==
                                 QAbstractSocket::ConnectedState) {
                               socket->write(response);
                               socket->disconnectFromHost();
                             }
                           });
      });
      connect(socket, &QObject::destroyed, this, [request] { delete request; });
    }
  }

  QHash<QString, HttpReply> replies_;
  QStringList receivedRequests_;
  QStringList completedReplies_;
};

LivePlaylistLimits testLimits() {
  LivePlaylistLimits limits;
  limits.timeoutMilliseconds = 300;
  limits.maximumRedirects = 5;
  limits.maximumResponseBytes = 4096;
  limits.maximumEntries = 20;
  return limits;
}

} // namespace

class LivePlaylistServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void rejectsInvalidPlaylistUrls();
  void loadsValidListAndResolvesAgainstFinalRedirectUrl();
  void loadsLocalPlaylistFilesAndReportsFailures();
  void rejectsInvalidTextAndOrdinaryHlsManifests();
  void enforcesResponseAndEntryLimits();
  void reportsTimeoutAndNetworkFailure();
  void limitsRedirectsAndRejectsUnsafeTargets();
  void cancelsPreviousRequestAndIgnoresItsLateResult();
};

void LivePlaylistServiceTest::initTestCase() {
  qRegisterMetaType<LivePlaylistLoadError>();
  qRegisterMetaType<LivePlaylistLoadResult>();
}

void LivePlaylistServiceTest::rejectsInvalidPlaylistUrls() {
  LivePlaylistService service(nullptr, testLimits());
  QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);

  service.load(QStringLiteral("rtsp://example.test/list.m3u"));
  QCOMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::InvalidUrl);
  service.load(QStringLiteral("https://example.test/list m3u"));
  QCOMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::InvalidUrl);
}

void LivePlaylistServiceTest::
    loadsValidListAndResolvesAgainstFinalRedirectUrl() {
  ScriptedHttpServer server;
  server.addReply(QStringLiteral("/start"),
                  {makeRedirectResponse("/nested/list.m3u"), 0});
  server.addReply(
      QStringLiteral("/nested/list.m3u"),
      {makeHttpResponse(200, "#EXTM3U\n"
                             "#EXTINF:-1 group-title=\"忽略分类\",第一路\n"
                             "../stream/one.m3u8\n"
                             "#EXTINF:-1,重复项\n../stream/one.m3u8\n"
                             "#EXTINF:-1,非法项\nfile:///C:/private.mp4\n"
                             "#EXTINF:-1,第二路\nrtsp://192.0.2.2/live\n"),
       0});
  LivePlaylistService service(nullptr, testLimits());
  std::optional<LivePlaylistLoadResult> loaded;
  connect(
      &service, &LivePlaylistService::loadSucceeded, this,
      [&loaded](LivePlaylistLoadResult result) { loaded = std::move(result); });

  service.load(server.url(QStringLiteral("/start")).toString());

  QTRY_VERIFY(loaded.has_value());
  QCOMPARE(loaded->library.channels.size(), 2U);
  QCOMPARE(QString::fromStdString(loaded->library.channels[0].name),
           QStringLiteral("第一路"));
  QCOMPARE(QString::fromStdString(loaded->library.channels[0].streamUrl),
           server.url(QStringLiteral("/stream/one.m3u8")).toString());
  QCOMPARE(QString::fromStdString(loaded->library.channels[1].name),
           QStringLiteral("第二路"));
  QCOMPARE(loaded->duplicateChannelCount, 1U);
  QCOMPARE(loaded->skippedChannelCount, 1U);
}

void LivePlaylistServiceTest::loadsLocalPlaylistFilesAndReportsFailures() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString playlistPath =
      directory.filePath(QStringLiteral("本地频道.m3u8"));
  QFile playlist(playlistPath);
  QVERIFY(playlist.open(QIODevice::WriteOnly));
  const QByteArray playlistContent =
      "#EXTM3U\n"
      "#PLAYLIST: IPTV Channel List\n"
      "#AUTHOR: Test\n"
      "#EXTINF:-1 tvg-name=\"CCTV-1\" response-time=\"120ms\",CCTV-1\n"
      "http://192.0.2.1/live/index.m3u8\n"
      "#EXTINF:-1 tvg-name=\"凤凰中文\",凤凰中文\n"
      "https://example.test/phoenix.m3u8\n";
  QCOMPARE(playlist.write(playlistContent), playlistContent.size());
  playlist.close();

  LivePlaylistService service(nullptr, testLimits());
  QSignalSpy loadedSpy(&service, &LivePlaylistService::loadSucceeded);
  QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);
  service.loadLocalFile(playlistPath);

  QTRY_COMPARE(loadedSpy.count(), 1);
  const LivePlaylistLoadResult result =
      qvariant_cast<LivePlaylistLoadResult>(loadedSpy.takeFirst().front());
  QCOMPARE(result.library.channels.size(), 2U);
  QCOMPARE(QString::fromStdString(result.library.channels[0].name),
           QStringLiteral("CCTV-1"));
  QCOMPARE(QString::fromStdString(result.library.channels[1].name),
           QStringLiteral("凤凰中文"));
  QCOMPARE(failureSpy.count(), 0);

  const QString hlsPath = directory.filePath(QStringLiteral("single.m3u8"));
  QFile hlsFile(hlsPath);
  QVERIFY(hlsFile.open(QIODevice::WriteOnly));
  const QByteArray hlsContent =
      "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\nsegment.ts\n";
  QCOMPARE(hlsFile.write(hlsContent), hlsContent.size());
  hlsFile.close();
  service.loadLocalFile(hlsPath);
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(
               failureSpy.takeFirst().front()),
           LivePlaylistLoadError::HlsMediaManifest);

  service.loadLocalFile(directory.filePath(QStringLiteral("missing.m3u8")));
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(
               failureSpy.takeFirst().front()),
           LivePlaylistLoadError::LocalFileUnreadable);
}

void LivePlaylistServiceTest::rejectsInvalidTextAndOrdinaryHlsManifests() {
  ScriptedHttpServer server;
  server.addReply(
      QStringLiteral("/invalid-utf8"),
      {makeHttpResponse(200, QByteArray("#EXTM3U\n\xC3\x28", 10)), 0});
  server.addReply(QStringLiteral("/not-m3u"),
                  {makeHttpResponse(200, "plain text"), 0});
  server.addReply(QStringLiteral("/empty"),
                  {makeHttpResponse(200, "#EXTM3U\n# 只有注释\n"), 0});
  server.addReply(QStringLiteral("/hls"),
                  {makeHttpResponse(200, "#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
                                         "#EXTINF:6,segment\nsegment001.ts\n"),
                   0});
  LivePlaylistService service(nullptr, testLimits());
  QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);

  service.load(server.url(QStringLiteral("/invalid-utf8")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::InvalidUtf8);
  service.load(server.url(QStringLiteral("/not-m3u")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::InvalidFormat);
  service.load(server.url(QStringLiteral("/empty")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::NoPlayableEntries);
  service.load(server.url(QStringLiteral("/hls")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::HlsMediaManifest);
}

void LivePlaylistServiceTest::enforcesResponseAndEntryLimits() {
  ScriptedHttpServer server;
  server.addReply(QStringLiteral("/large"),
                  {makeHttpResponse(200, QByteArray(200, 'x')), 0});
  server.addReply(
      QStringLiteral("/entries"),
      {makeHttpResponse(200, "#EXTM3U\n"
                             "#EXTINF:-1,一\nhttps://example.test/1\n"
                             "#EXTINF:-1,二\nhttps://example.test/2\n"),
       0});
  LivePlaylistLimits limits = testLimits();
  limits.maximumResponseBytes = 64;
  {
    LivePlaylistService service(nullptr, limits);
    QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);
    service.load(server.url(QStringLiteral("/large")).toString());
    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(
        qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
        LivePlaylistLoadError::ResponseTooLarge);
  }
  limits = testLimits();
  limits.maximumEntries = 1;
  LivePlaylistService service(nullptr, limits);
  QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);
  service.load(server.url(QStringLiteral("/entries")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::TooManyEntries);
}

void LivePlaylistServiceTest::reportsTimeoutAndNetworkFailure() {
  ScriptedHttpServer server;
  server.addReply(QStringLiteral("/slow"),
                  {makeHttpResponse(200, "#EXTM3U\n"), 150});
  server.addReply(QStringLiteral("/error"),
                  {makeHttpResponse(500, "failed"), 0});
  LivePlaylistLimits limits = testLimits();
  limits.timeoutMilliseconds = 30;
  {
    LivePlaylistService service(nullptr, limits);
    QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);
    service.load(server.url(QStringLiteral("/slow")).toString());
    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(
        qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
        LivePlaylistLoadError::Timeout);
  }
  LivePlaylistService service(nullptr, testLimits());
  QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);
  service.load(server.url(QStringLiteral("/error")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::NetworkFailure);
}

void LivePlaylistServiceTest::limitsRedirectsAndRejectsUnsafeTargets() {
  ScriptedHttpServer server;
  server.addReply(QStringLiteral("/redirect-one"),
                  {makeRedirectResponse("/redirect-two"), 0});
  server.addReply(QStringLiteral("/redirect-two"),
                  {makeRedirectResponse("/final"), 0});
  server.addReply(QStringLiteral("/unsafe"),
                  {makeRedirectResponse("file:///C:/private/list.m3u"), 0});
  LivePlaylistLimits limits = testLimits();
  limits.maximumRedirects = 1;
  LivePlaylistService service(nullptr, limits);
  QSignalSpy failureSpy(&service, &LivePlaylistService::loadFailed);

  service.load(server.url(QStringLiteral("/redirect-one")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::TooManyRedirects);
  service.load(server.url(QStringLiteral("/unsafe")).toString());
  QTRY_COMPARE(failureSpy.count(), 1);
  QCOMPARE(qvariant_cast<LivePlaylistLoadError>(failureSpy.takeFirst().front()),
           LivePlaylistLoadError::UnsafeRedirect);
}

void LivePlaylistServiceTest::cancelsPreviousRequestAndIgnoresItsLateResult() {
  ScriptedHttpServer server;
  server.addReply(
      QStringLiteral("/old"),
      {makeHttpResponse(
           200, "#EXTM3U\n#EXTINF:-1,旧清单\nhttps://example.test/old\n"),
       100});
  server.addReply(
      QStringLiteral("/new"),
      {makeHttpResponse(
           200, "#EXTM3U\n#EXTINF:-1,新清单\nhttps://example.test/new\n"),
       0});
  LivePlaylistService service(nullptr, testLimits());
  QList<QString> loadedNames;
  connect(&service, &LivePlaylistService::loadSucceeded, this,
          [&loadedNames](const LivePlaylistLoadResult &result) {
            loadedNames.append(
                QString::fromStdString(result.library.channels.front().name));
          });

  service.load(server.url(QStringLiteral("/old")).toString());
  QTRY_VERIFY(server.hasReceivedRequest(QStringLiteral("/old")));
  service.load(server.url(QStringLiteral("/new")).toString());

  QTRY_COMPARE(loadedNames.size(), 1);
  QCOMPARE(loadedNames.front(), QStringLiteral("新清单"));
  QTRY_VERIFY_WITH_TIMEOUT(server.hasCompletedReply(QStringLiteral("/old")),
                           300);
  QCOMPARE(loadedNames.size(), 1);
}

} // namespace mediahub::gui

QTEST_GUILESS_MAIN(mediahub::gui::LivePlaylistServiceTest)

#include "live_playlist_service_test.moc"
