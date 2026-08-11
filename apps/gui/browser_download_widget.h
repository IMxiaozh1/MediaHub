#pragma once

#include <QWidget>

#include <cstdint>

#include "browser_types.h"

class QLabel;
class QProgressBar;
class QPushButton;

namespace mediahub::gui {

// 展示单个下载任务，并在提交路径前执行不覆盖校验。
class BrowserDownloadWidget final : public QWidget {
    Q_OBJECT

 public:
    // 调用线程：GUI 主线程。
    explicit BrowserDownloadWidget(QWidget* parent = nullptr);

    // 调用线程：GUI 主线程。新请求会重置单任务展示状态。
    void beginDownload(std::uint64_t requestId, const QString& origin,
                       const QString& suggestedFileName, std::int64_t totalBytes);
    // 调用线程：GUI 主线程。迟到 requestId 不改变当前任务。
    void updateDownload(std::uint64_t requestId, BrowserDownloadState state,
                        std::int64_t receivedBytes, std::int64_t totalBytes);
    // 调用线程：GUI 主线程。只接受尚不存在且父目录有效的绝对路径。
    void submitDestination(const QString& destination);
    // 调用线程：GUI 主线程。保存窗口返回空值时等价于取消当前下载。
    void completeDestinationSelection(const QString& destination);
    [[nodiscard]] QString originText() const;
    [[nodiscard]] QString fileNameText() const;
    [[nodiscard]] QString sizeText() const;
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] int progressValue() const noexcept;
    [[nodiscard]] bool isTerminal() const noexcept;
    [[nodiscard]] bool hasSubmittedDestination() const noexcept;
    [[nodiscard]] std::uint64_t requestId() const noexcept;

 signals:
    void destinationChosen(std::uint64_t requestId, const QString& destination);
    void cancelRequested(std::uint64_t requestId);

 private:
    void chooseDestination();
    void requestCancel();
    void showPathError(const QString& message);
    [[nodiscard]] static bool isAcceptableFileName(const QString& fileName);
    [[nodiscard]] static QString formatBytes(std::int64_t bytes);

    std::uint64_t requestId_{0};
    QString suggestedFileName_;
    QLabel* originLabel_{nullptr};
    QLabel* fileNameLabel_{nullptr};
    QLabel* sizeLabel_{nullptr};
    QLabel* stateLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QProgressBar* progressBar_{nullptr};
    QPushButton* chooseButton_{nullptr};
    QPushButton* cancelButton_{nullptr};
    bool isActive_{false};
    bool isDestinationSubmitted_{false};
    bool isCancelSent_{false};
    bool isTerminal_{false};
};

}  // namespace mediahub::gui
