#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

namespace mediahub::browser_webview2 {

// 为真实 WebView2 集成测试提供固定内容，只允许监听 IPv4 环回接口。
class LocalWebTestServer final : public QObject {
 public:
    explicit LocalWebTestServer(QObject* parent = nullptr);
    ~LocalWebTestServer() override;

    LocalWebTestServer(const LocalWebTestServer&) = delete;
    LocalWebTestServer& operator=(const LocalWebTestServer&) = delete;

    // 调用线程：GUI 主线程，使用系统分配的临时端口监听 127.0.0.1。
    [[nodiscard]] bool start();
    // 调用线程：GUI 主线程，关闭监听器和全部未完成连接。
    void stop() noexcept;

    [[nodiscard]] QUrl url(const QString& path) const;
    [[nodiscard]] QString origin() const;
    [[nodiscard]] QHostAddress serverAddress() const;
    [[nodiscard]] quint16 serverPort() const noexcept;
    [[nodiscard]] int requestCount(const QString& path) const;

 private:
    // 调用线程：GUI 主线程，由 QTcpServer 事件循环调用。
    void handlePendingConnections();
    // 调用线程：GUI 主线程，只解析一个有界 HTTP 请求头。
    void handleReadyRead(QTcpSocket* socket);
    [[nodiscard]] QByteArray responseForPath(const QString& path) const;

    QTcpServer server_;
    QHash<QTcpSocket*, QByteArray> pendingRequests_;
    QHash<QString, int> requestCounts_;
};

}  // namespace mediahub::browser_webview2
