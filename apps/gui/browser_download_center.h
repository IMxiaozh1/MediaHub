#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>

#include "browser_types.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace mediahub::gui {

class BrowserDownloadItem;

// 集中展示相互隔离的网页下载任务，不因控件隐藏而改变任务状态。
class BrowserDownloadCenter final : public QWidget {
    Q_OBJECT

 public:
    enum class ItemState {
        WaitingForDestination,
        InProgress,
        Cancelling,
        CancelFailed,
        RetryableFailure,
        Completed,
        Failed,
        Cancelled,
    };
    Q_ENUM(ItemState)

    struct ItemSnapshot {
        ItemState state{ItemState::WaitingForDestination};
        QString originText;
        QString fileNameText;
        QString sizeText;
        QString speedText;
        QString remainingText;
        QString stateText;
        QString errorText;
        int progressValue{0};
        bool isProgressIndeterminate{true};
        bool hasSubmittedDestination{false};
    };

    static constexpr int kMaximumTrackedItems = 100;

    // 调用线程：GUI 主线程。
    explicit BrowserDownloadCenter(QWidget* parent = nullptr);

    // 调用线程：GUI 主线程。达到容量上限时返回 false；重复请求不会重置已有任务。
    [[nodiscard]] bool beginDownload(std::uint64_t requestId,
                                     const QString& origin,
                                     const QString& suggestedFileName,
                                     std::int64_t totalBytes);
    // 调用线程：GUI 主线程。未知请求和终态任务的迟到事件会被忽略。
    void updateDownload(std::uint64_t requestId, BrowserDownloadState state,
                        std::int64_t receivedBytes, std::int64_t totalBytes);
    // 调用线程：GUI 主线程。只接受尚不存在且父目录有效的绝对路径。
    [[nodiscard]] bool submitDestination(std::uint64_t requestId,
                                         const QString& destination);
    // 调用线程：GUI 主线程。系统选择器返回空值时等价于取消对应下载。
    void completeDestinationSelection(std::uint64_t requestId,
                                      const QString& destination);
    // 调用线程：GUI 主线程。取消失败后允许再次调用并重新发送请求。
    [[nodiscard]] bool requestCancel(std::uint64_t requestId);
    // 调用线程：GUI 主线程。仅可恢复中断任务能发送继续下载请求。
    [[nodiscard]] bool requestRetry(std::uint64_t requestId);
    // 调用线程：GUI 主线程。移除完成、失败和已取消任务，返回移除数量。
    int clearCompleted();
    // 调用线程：GUI 主线程。清除网页数据时移除全部任务展示，不发送后端命令。
    int clearForBrowsingData();

    [[nodiscard]] int trackedItemCount() const noexcept;
    [[nodiscard]] int activeItemCount() const noexcept;
    [[nodiscard]] QVector<std::uint64_t> activeRequestIds() const;
    [[nodiscard]] bool contains(std::uint64_t requestId) const noexcept;
    [[nodiscard]] std::optional<ItemSnapshot> itemSnapshot(
        std::uint64_t requestId) const;

 signals:
    void destinationChosen(std::uint64_t requestId, const QString& destination);
    void cancelRequested(std::uint64_t requestId);
    void retryRequested(std::uint64_t requestId);

 private:
    [[nodiscard]] BrowserDownloadItem* itemFor(
        std::uint64_t requestId) const noexcept;
    void chooseDestination(std::uint64_t requestId);
    void refreshSummary();

    QHash<quint64, BrowserDownloadItem*> items_;
    QVBoxLayout* itemLayout_{nullptr};
    QLabel* summaryLabel_{nullptr};
    QLabel* emptyLabel_{nullptr};
    QPushButton* clearButton_{nullptr};
};

}  // namespace mediahub::gui
