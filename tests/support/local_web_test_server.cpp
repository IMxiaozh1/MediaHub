#include "support/local_web_test_server.h"

#include <QAbstractSocket>
#include <QList>
#include <QTcpSocket>

namespace mediahub::browser_webview2 {
namespace {

constexpr int kMaximumRequestHeaderBytes = 16 * 1024;

QByteArray makeHttpResponse(const int statusCode, const QByteArray& reason,
                            const QByteArray& contentType, const QByteArray& body,
                            const QByteArray& extraHeaders = {}) {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason +
                          "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\n";
    response += extraHeaders;
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

QByteArray htmlResponse(const QByteArray& body) {
    return makeHttpResponse(200, QByteArrayLiteral("OK"),
                            QByteArrayLiteral("text/html; charset=utf-8"), body);
}

void appendLittleEndian16(QByteArray& output, const quint16 value) {
    output.append(static_cast<char>(value & 0xff));
    output.append(static_cast<char>((value >> 8) & 0xff));
}

void appendLittleEndian32(QByteArray& output, const quint32 value) {
    output.append(static_cast<char>(value & 0xff));
    output.append(static_cast<char>((value >> 8) & 0xff));
    output.append(static_cast<char>((value >> 16) & 0xff));
    output.append(static_cast<char>((value >> 24) & 0xff));
}

QByteArray generatedSilentWave() {
    constexpr quint32 kSampleRate = 8000;
    constexpr quint16 kChannelCount = 1;
    constexpr quint16 kBitsPerSample = 16;
    constexpr quint32 kSampleCount = 2000;
    constexpr quint32 kBytesPerSample = kBitsPerSample / 8;
    constexpr quint32 kDataSize = kSampleCount * kChannelCount * kBytesPerSample;

    QByteArray wave;
    wave.reserve(static_cast<int>(44 + kDataSize));
    wave.append("RIFF", 4);
    appendLittleEndian32(wave, 36 + kDataSize);
    wave.append("WAVEfmt ", 8);
    appendLittleEndian32(wave, 16);
    appendLittleEndian16(wave, 1);
    appendLittleEndian16(wave, kChannelCount);
    appendLittleEndian32(wave, kSampleRate);
    appendLittleEndian32(wave, kSampleRate * kChannelCount * kBytesPerSample);
    appendLittleEndian16(wave, kChannelCount * kBytesPerSample);
    appendLittleEndian16(wave, kBitsPerSample);
    wave.append("data", 4);
    appendLittleEndian32(wave, kDataSize);
    wave.append(QByteArray(static_cast<int>(kDataSize), '\0'));
    return wave;
}

QByteArray storageSetPage() {
    return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>storage-setting</title>
<script>
document.cookie = 'mediahub_cookie=1; Max-Age=3600; Path=/; SameSite=Lax';
localStorage.setItem('mediahub_local', '1');
const request = indexedDB.open('mediahub-storage-test', 1);
request.onupgradeneeded = () => {
    const database = request.result;
    if (!database.objectStoreNames.contains('values')) {
        database.createObjectStore('values');
    }
};
request.onerror = () => { document.title = 'storage-error'; };
request.onsuccess = () => {
    const database = request.result;
    const transaction = database.transaction('values', 'readwrite');
    transaction.objectStore('values').put('1', 'indexed');
    transaction.oncomplete = () => {
        database.close();
        document.title = 'stored';
    };
    transaction.onerror = () => { document.title = 'storage-error'; };
};
</script>)HTML"));
}

QByteArray storageReadPage() {
    return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>storage-reading</title>
<script>
const hasCookie = document.cookie.includes('mediahub_cookie=1');
const hasLocal = localStorage.getItem('mediahub_local') === '1';
const request = indexedDB.open('mediahub-storage-test', 1);
request.onupgradeneeded = () => {
    const database = request.result;
    if (!database.objectStoreNames.contains('values')) {
        database.createObjectStore('values');
    }
};
request.onerror = () => { document.title = 'storage-error'; };
request.onsuccess = () => {
    const database = request.result;
    const lookup = database.transaction('values').objectStore('values').get('indexed');
    lookup.onerror = () => { document.title = 'storage-error'; };
    lookup.onsuccess = () => {
        const hasIndexed = lookup.result === '1';
        database.close();
        document.title = !hasCookie && !hasLocal && !hasIndexed
            ? 'empty'
            : `cookie=${hasCookie ? 1 : 0};local=${hasLocal ? 1 : 0};indexed=${hasIndexed ? 1 : 0}`;
    };
};
</script>)HTML"));
}

QByteArray popupPage() {
    return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>popup-opening</title>
<script>
const checkSharedProfile = setInterval(() => {
    const hasCookie = document.cookie.includes('mediahub_popup=1');
    const hasLocal = localStorage.getItem('mediahub_popup') === '1';
    if (hasCookie && hasLocal) {
        clearInterval(checkSharedProfile);
        document.title = 'popup-shared';
    }
}, 25);
window.open('/child', 'mediahub-child', 'width=640,height=480');
</script>)HTML"));
}

QByteArray childPage() {
    return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>child-loading</title>
<script>
document.cookie = 'mediahub_popup=1; Path=/; SameSite=Lax';
localStorage.setItem('mediahub_popup', '1');
fetch('/signal/child-ready').finally(() => { document.title = 'child-ready'; });
</script>)HTML"));
}

}  // namespace

LocalWebTestServer::LocalWebTestServer(QObject* const parent)
    : QObject(parent), server_(this) {
    connect(&server_, &QTcpServer::newConnection, this,
            &LocalWebTestServer::handlePendingConnections);
}

LocalWebTestServer::~LocalWebTestServer() {
    stop();
}

bool LocalWebTestServer::start() {
    if (server_.isListening()) {
        return server_.serverAddress() == QHostAddress::LocalHost;
    }
    return server_.listen(QHostAddress::LocalHost, 0) &&
           server_.serverAddress() == QHostAddress::LocalHost;
}

void LocalWebTestServer::stop() noexcept {
    server_.close();
    const QList<QTcpSocket*> sockets = pendingRequests_.keys();
    pendingRequests_.clear();
    for (QTcpSocket* const socket : sockets) {
        if (socket != nullptr) {
            socket->abort();
            socket->deleteLater();
        }
    }
}

QUrl LocalWebTestServer::url(const QString& path) const {
    QUrl relative(path);
    QUrl result;
    result.setScheme(QStringLiteral("http"));
    result.setHost(QStringLiteral("127.0.0.1"));
    result.setPort(server_.serverPort());
    result.setPath(relative.path().startsWith(QLatin1Char('/'))
                       ? relative.path()
                       : QStringLiteral("/") + relative.path());
    result.setQuery(relative.query());
    return result;
}

QString LocalWebTestServer::origin() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
}

QHostAddress LocalWebTestServer::serverAddress() const {
    return server_.serverAddress();
}

quint16 LocalWebTestServer::serverPort() const noexcept {
    return server_.serverPort();
}

int LocalWebTestServer::requestCount(const QString& path) const {
    return requestCounts_.value(path);
}

void LocalWebTestServer::handlePendingConnections() {
    while (server_.hasPendingConnections()) {
        QTcpSocket* const socket = server_.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        pendingRequests_.insert(socket, QByteArray{});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QObject::destroyed, this,
                [this, socket] { pendingRequests_.remove(socket); });
    }
}

void LocalWebTestServer::handleReadyRead(QTcpSocket* const socket) {
    auto found = pendingRequests_.find(socket);
    if (found == pendingRequests_.end()) {
        return;
    }
    found.value().append(socket->readAll());
    if (found.value().size() > kMaximumRequestHeaderBytes) {
        socket->write(makeHttpResponse(431, QByteArrayLiteral("Request Header Too Large"),
                                       QByteArrayLiteral("text/plain; charset=utf-8"),
                                       QByteArrayLiteral("request too large")));
        pendingRequests_.erase(found);
        socket->disconnectFromHost();
        return;
    }

    const int headerEnd = found.value().indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    const QByteArray requestLine = found.value().left(found.value().indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    QByteArray response;
    if (parts.size() != 3 || parts.at(0) != QByteArrayLiteral("GET")) {
        response = makeHttpResponse(405, QByteArrayLiteral("Method Not Allowed"),
                                    QByteArrayLiteral("text/plain; charset=utf-8"),
                                    QByteArrayLiteral("method not allowed"));
    } else {
        const QUrl requestTarget = QUrl::fromEncoded(parts.at(1), QUrl::StrictMode);
        const QString path = requestTarget.path().isEmpty()
                                 ? QStringLiteral("/")
                                 : requestTarget.path();
        requestCounts_[path] = requestCounts_.value(path) + 1;
        response = responseForPath(path);
    }
    pendingRequests_.erase(found);
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray LocalWebTestServer::responseForPath(const QString& path) const {
    if (path == QStringLiteral("/storage/set")) {
        return storageSetPage();
    }
    if (path == QStringLiteral("/storage/read")) {
        return storageReadPage();
    }
    if (path == QStringLiteral("/redirect")) {
        return makeHttpResponse(302, QByteArrayLiteral("Found"),
                                QByteArrayLiteral("text/plain; charset=utf-8"), {},
                                QByteArrayLiteral("Location: /navigation/final\r\n"));
    }
    if (path == QStringLiteral("/navigation/final")) {
        return htmlResponse(QByteArrayLiteral(
            "<!doctype html><meta charset=\"utf-8\"><title>redirected</title>"));
    }
    if (path == QStringLiteral("/popup")) {
        return popupPage();
    }
    if (path == QStringLiteral("/child")) {
        return childPage();
    }
    if (path == QStringLiteral("/signal/child-ready")) {
        return makeHttpResponse(204, QByteArrayLiteral("No Content"),
                                QByteArrayLiteral("text/plain"), {});
    }
    if (path == QStringLiteral("/permission")) {
        return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>permission-requesting</title>
<script>
Promise.resolve()
    .then(() => navigator.mediaDevices.getUserMedia({audio: true}))
    .then((stream) => {
        stream.getTracks().forEach((track) => track.stop());
        document.title = 'permission=allowed';
    })
    .catch(() => { document.title = 'permission=denied'; });
</script>)HTML"));
    }
    if (path == QStringLiteral("/download")) {
        return makeHttpResponse(
            200, QByteArrayLiteral("OK"), QByteArrayLiteral("text/plain; charset=utf-8"),
            QByteArrayLiteral("MediaHub WebView2 test\n"),
            QByteArrayLiteral("Content-Disposition: attachment; filename=\"sample.txt\"\r\n"));
    }
    if (path == QStringLiteral("/upload")) {
        return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>upload-ready</title>
<input id="upload" type="file">
<script>
document.getElementById('upload').addEventListener('change', () => {
    document.title = 'upload-selected';
});
</script>)HTML"));
    }
    if (path == QStringLiteral("/media")) {
        return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>media-loading</title>
<audio id="media" controls preload="auto"></audio>
<script>
const media = document.getElementById('media');
media.addEventListener('loadedmetadata', () => { document.title = 'media-ready'; });
media.addEventListener('error', () => { document.title = 'media-error'; });
media.src = '/generated-silent.wav';
</script>)HTML"));
    }
    if (path == QStringLiteral("/generated-silent.wav")) {
        return makeHttpResponse(200, QByteArrayLiteral("OK"),
                                QByteArrayLiteral("audio/wav"), generatedSilentWave(),
                                QByteArrayLiteral("Accept-Ranges: bytes\r\n"));
    }
    if (path == QStringLiteral("/fullscreen")) {
        return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>fullscreen-ready</title>
<button id="enter">enter fullscreen</button>
<script>
document.getElementById('enter').addEventListener('click', () => {
    document.documentElement.requestFullscreen();
});
</script>)HTML"));
    }
    if (path == QStringLiteral("/external")) {
        return htmlResponse(QByteArrayLiteral(R"HTML(<!doctype html>
<meta charset="utf-8">
<title>external-ready</title>
<a href="mailto:test@example.invalid">external protocol</a>)HTML"));
    }
    return makeHttpResponse(404, QByteArrayLiteral("Not Found"),
                            QByteArrayLiteral("text/plain; charset=utf-8"),
                            QByteArrayLiteral("not found"));
}

}  // namespace mediahub::browser_webview2
