#include "browser_download_center.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace mediahub::gui {
namespace {

QString safeOriginText(const QString& origin) {
    const QUrl parsed(origin.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QStringLiteral("http") &&
                              scheme != QStringLiteral("https")) ||
        parsed.host().isEmpty()) {
        return QStringLiteral("来源未知");
    }

    QUrl visible;
    visible.setScheme(scheme);
    visible.setHost(parsed.host());
    visible.setPort(parsed.port());
    return visible.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemovePath |
                                   QUrl::RemoveQuery | QUrl::RemoveFragment |
                                   QUrl::StripTrailingSlash);
}

bool isAcceptableFileName(const QString& fileName) {
    if (fileName.isEmpty() || fileName.endsWith(QLatin1Char('.')) ||
        fileName.endsWith(QLatin1Char(' '))) {
        return false;
    }
    static const QRegularExpression invalidCharacters(
        QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    if (fileName.contains(invalidCharacters)) {
        return false;
    }
    const QString stem = fileName.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QRegularExpression reservedName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
    return !reservedName.match(stem).hasMatch();
}

QString safeSuggestedFileName(const QString& suggestedFileName) {
    QString fileName = QFileInfo(
                           QDir::fromNativeSeparators(suggestedFileName.trimmed()))
                           .fileName();
    if (!isAcceptableFileName(fileName)) {
        return QStringLiteral("未命名文件");
    }
    constexpr int kMaximumVisibleFileNameLength = 180;
    if (fileName.size() > kMaximumVisibleFileNameLength) {
        fileName = fileName.left(kMaximumVisibleFileNameLength);
    }
    return fileName;
}

QString formatBytes(const std::int64_t bytes) {
    if (bytes < 0) {
        return QStringLiteral("大小未知");
    }
    constexpr std::int64_t kKilobyte = 1024;
    constexpr std::int64_t kMegabyte = kKilobyte * 1024;
    constexpr std::int64_t kGigabyte = kMegabyte * 1024;
    if (bytes >= kGigabyte) {
        return QStringLiteral("%1 GB")
            .arg(static_cast<double>(bytes) / static_cast<double>(kGigabyte),
                 0, 'f', 1);
    }
    if (bytes >= kMegabyte) {
        return QStringLiteral("%1 MB")
            .arg(static_cast<double>(bytes) / static_cast<double>(kMegabyte),
                 0, 'f', 1);
    }
    if (bytes >= kKilobyte) {
        return QStringLiteral("%1 KB")
            .arg(static_cast<double>(bytes) / static_cast<double>(kKilobyte),
                 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString formatRate(const double bytesPerSecond) {
    return QStringLiteral("%1/s").arg(formatBytes(
        static_cast<std::int64_t>(std::max(bytesPerSecond, 0.0))));
}

bool isTerminalState(const BrowserDownloadCenter::ItemState state) {
    return state == BrowserDownloadCenter::ItemState::Completed ||
           state == BrowserDownloadCenter::ItemState::Failed ||
           state == BrowserDownloadCenter::ItemState::Cancelled;
}

}  // namespace

class BrowserDownloadItem final : public QWidget {
 public:
    BrowserDownloadItem(const std::uint64_t requestId, const QString& origin,
                        const QString& suggestedFileName,
                        const std::int64_t totalBytes, QWidget* const parent)
        : QWidget(parent),
          requestId_(requestId),
          suggestedFileName_(safeSuggestedFileName(suggestedFileName)) {
        setObjectName(QStringLiteral("browserDownloadWidget"));
        setProperty("downloadRequestId",
                    QVariant::fromValue<qulonglong>(requestId_));

        auto* const layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6);

        auto* const details = new QHBoxLayout();
        originLabel_ = new QLabel(safeOriginText(origin), this);
        originLabel_->setObjectName(QStringLiteral("browserDownloadOriginLabel"));
        fileNameLabel_ = new QLabel(suggestedFileName_, this);
        fileNameLabel_->setObjectName(
            QStringLiteral("browserDownloadFileNameLabel"));
        sizeLabel_ = new QLabel(formatBytes(totalBytes), this);
        sizeLabel_->setObjectName(QStringLiteral("browserDownloadSizeLabel"));
        speedLabel_ = new QLabel(QStringLiteral("速度：计算中"), this);
        speedLabel_->setObjectName(QStringLiteral("browserDownloadSpeedLabel"));
        remainingLabel_ = new QLabel(QStringLiteral("剩余：大小未知"), this);
        remainingLabel_->setObjectName(
            QStringLiteral("browserDownloadRemainingLabel"));
        stateLabel_ = new QLabel(QStringLiteral("等待选择保存位置"), this);
        stateLabel_->setObjectName(QStringLiteral("browserDownloadStateLabel"));
        details->addWidget(originLabel_);
        details->addWidget(fileNameLabel_);
        details->addWidget(sizeLabel_);
        details->addWidget(speedLabel_);
        details->addWidget(remainingLabel_);
        details->addStretch();
        details->addWidget(stateLabel_);
        layout->addLayout(details);

        progressBar_ = new QProgressBar(this);
        progressBar_->setObjectName(
            QStringLiteral("browserDownloadProgressBar"));
        progressBar_->setRange(0, totalBytes > 0 ? 100 : 0);
        progressBar_->setValue(0);
        layout->addWidget(progressBar_);

        auto* const actionRow = new QHBoxLayout();
        errorLabel_ = new QLabel(this);
        errorLabel_->setObjectName(QStringLiteral("browserDownloadErrorLabel"));
        errorLabel_->setWordWrap(true);
        chooseButton_ = new QPushButton(QStringLiteral("选择保存位置"), this);
        chooseButton_->setObjectName(
            QStringLiteral("browserDownloadChooseButton"));
        cancelButton_ = new QPushButton(QStringLiteral("取消下载"), this);
        cancelButton_->setObjectName(
            QStringLiteral("browserDownloadCancelButton"));
        retryButton_ = new QPushButton(QStringLiteral("继续下载"), this);
        retryButton_->setObjectName(
            QStringLiteral("browserDownloadRetryButton"));
        retryButton_->hide();
        actionRow->addWidget(errorLabel_, 1);
        actionRow->addWidget(chooseButton_);
        actionRow->addWidget(retryButton_);
        actionRow->addWidget(cancelButton_);
        layout->addLayout(actionRow);
    }

    [[nodiscard]] std::uint64_t requestId() const noexcept { return requestId_; }
    [[nodiscard]] const QString& suggestedFileName() const noexcept {
        return suggestedFileName_;
    }
    [[nodiscard]] BrowserDownloadCenter::ItemState state() const noexcept {
        return state_;
    }
    [[nodiscard]] QPushButton* chooseButton() const noexcept {
        return chooseButton_;
    }
    [[nodiscard]] QPushButton* cancelButton() const noexcept {
        return cancelButton_;
    }
    [[nodiscard]] QPushButton* retryButton() const noexcept {
        return retryButton_;
    }

    [[nodiscard]] bool submitDestination(const QString& destination) {
        if (isTerminalState(state_) || state_ == BrowserDownloadCenter::ItemState::Cancelling ||
            hasSubmittedDestination_) {
            return false;
        }

        const QString trimmed = destination.trimmed();
        if (trimmed.isEmpty()) {
            showPathError(QStringLiteral("请选择保存文件"));
            return false;
        }
        const QString cleaned = QDir::cleanPath(trimmed);
        const QFileInfo destinationInfo(cleaned);
        if (!destinationInfo.isAbsolute()) {
            showPathError(QStringLiteral("保存位置必须是绝对路径"));
            return false;
        }
        if (destinationInfo.exists()) {
            showPathError(destinationInfo.isDir()
                              ? QStringLiteral("目标不能是文件夹")
                              : QStringLiteral("目标文件已存在，请选择新名称"));
            return false;
        }
        if (!isAcceptableFileName(destinationInfo.fileName())) {
            showPathError(QStringLiteral("文件名不可接受，请选择新名称"));
            return false;
        }
        const QFileInfo parentInfo(destinationInfo.absolutePath());
        if (!parentInfo.exists() || !parentInfo.isDir()) {
            showPathError(QStringLiteral("保存文件夹不存在"));
            return false;
        }
        if (!parentInfo.isWritable()) {
            showPathError(QStringLiteral("保存文件夹不可写"));
            return false;
        }

        errorLabel_->clear();
        retryButton_->hide();
        state_ = BrowserDownloadCenter::ItemState::InProgress;
        stateLabel_->setText(QStringLiteral("等待下载开始"));
        hasSubmittedDestination_ = true;
        chooseButton_->setEnabled(false);
        return true;
    }

    [[nodiscard]] bool markCancelling() {
        if (isTerminalState(state_) ||
            state_ == BrowserDownloadCenter::ItemState::Cancelling) {
            return false;
        }
        state_ = BrowserDownloadCenter::ItemState::Cancelling;
        stateLabel_->setText(QStringLiteral("正在取消下载..."));
        errorLabel_->clear();
        chooseButton_->setEnabled(false);
        retryButton_->hide();
        cancelButton_->setEnabled(false);
        return true;
    }

    [[nodiscard]] bool markRetrying() {
        if (state_ != BrowserDownloadCenter::ItemState::RetryableFailure) {
            return false;
        }
        state_ = BrowserDownloadCenter::ItemState::InProgress;
        stateLabel_->setText(QStringLiteral("正在继续下载..."));
        errorLabel_->clear();
        chooseButton_->setEnabled(false);
        retryButton_->setEnabled(false);
        retryButton_->hide();
        cancelButton_->setEnabled(true);
        cancelButton_->setText(QStringLiteral("取消下载"));
        return true;
    }

    void update(const BrowserDownloadState state, const std::int64_t receivedBytes,
                const std::int64_t totalBytes) {
        if (isTerminalState(state_)) {
            return;
        }
        updateProgress(receivedBytes, totalBytes);

        switch (state) {
        case BrowserDownloadState::InProgress:
            if (state_ != BrowserDownloadCenter::ItemState::Cancelling) {
                state_ = BrowserDownloadCenter::ItemState::InProgress;
                stateLabel_->setText(QStringLiteral("下载中"));
                errorLabel_->clear();
                chooseButton_->setEnabled(false);
                retryButton_->hide();
                cancelButton_->setEnabled(true);
                cancelButton_->setText(QStringLiteral("取消下载"));
            }
            break;
        case BrowserDownloadState::CancelFailed:
            state_ = BrowserDownloadCenter::ItemState::CancelFailed;
            stateLabel_->setText(QStringLiteral("取消失败，可重试"));
            errorLabel_->setText(QStringLiteral("下载未能取消，可再次尝试"));
            chooseButton_->setEnabled(!hasSubmittedDestination_);
            retryButton_->hide();
            cancelButton_->setEnabled(true);
            cancelButton_->setText(QStringLiteral("重试取消"));
            break;
        case BrowserDownloadState::RetryableFailure:
            state_ = BrowserDownloadCenter::ItemState::RetryableFailure;
            stateLabel_->setText(QStringLiteral("下载中断，可继续"));
            errorLabel_->setText(
                QStringLiteral("连接已中断，可从已下载位置继续"));
            chooseButton_->setEnabled(false);
            retryButton_->setEnabled(true);
            retryButton_->show();
            cancelButton_->setEnabled(true);
            cancelButton_->setText(QStringLiteral("取消下载"));
            break;
        case BrowserDownloadState::Completed:
            setTerminalState(BrowserDownloadCenter::ItemState::Completed,
                             QStringLiteral("下载完成"), QString{});
            break;
        case BrowserDownloadState::Failed:
            setTerminalState(BrowserDownloadCenter::ItemState::Failed,
                             QStringLiteral("下载失败"),
                             QStringLiteral("下载未完成"));
            break;
        case BrowserDownloadState::Cancelled:
            setTerminalState(BrowserDownloadCenter::ItemState::Cancelled,
                             QStringLiteral("下载已取消"), QString{});
            break;
        }
    }

    [[nodiscard]] BrowserDownloadCenter::ItemSnapshot snapshot() const {
        return BrowserDownloadCenter::ItemSnapshot{
            state_,
            originLabel_->text(),
            fileNameLabel_->text(),
            sizeLabel_->text(),
            speedLabel_->text(),
            remainingLabel_->text(),
            stateLabel_->text(),
            errorLabel_->text(),
            progressBar_->value(),
            progressBar_->minimum() == 0 && progressBar_->maximum() == 0,
            hasSubmittedDestination_,
        };
    }

 private:
    void showPathError(const QString& message) { errorLabel_->setText(message); }

    void updateProgress(const std::int64_t receivedBytes,
                        const std::int64_t totalBytes) {
        const std::int64_t safeReceived = std::max<std::int64_t>(receivedBytes, 0);
        if (!progressTimer_.isValid()) {
            progressTimer_.start();
            lastReceivedBytes_ = safeReceived;
        } else {
            const qint64 elapsedMilliseconds = progressTimer_.elapsed();
            if (elapsedMilliseconds > 0 && safeReceived >= lastReceivedBytes_) {
                const double bytesPerSecond =
                    static_cast<double>(safeReceived - lastReceivedBytes_) *
                    1000.0 / static_cast<double>(elapsedMilliseconds);
                speedLabel_->setText(
                    QStringLiteral("速度：%1").arg(formatRate(bytesPerSecond)));
            }
            lastReceivedBytes_ = safeReceived;
            progressTimer_.restart();
        }
        if (totalBytes > 0) {
            progressBar_->setRange(0, 100);
            const long double ratio =
                static_cast<long double>(safeReceived) /
                static_cast<long double>(totalBytes);
            const auto percent = static_cast<int>(
                std::clamp(ratio * 100.0L, 0.0L, 100.0L));
            progressBar_->setValue(percent);
            sizeLabel_->setText(
                QStringLiteral("%1 / %2")
                    .arg(formatBytes(safeReceived), formatBytes(totalBytes)));
            remainingLabel_->setText(
                QStringLiteral("剩余：%1")
                    .arg(formatBytes(std::max<std::int64_t>(
                        totalBytes - safeReceived, 0))));
            return;
        }
        progressBar_->setRange(0, 0);
        sizeLabel_->setText(formatBytes(safeReceived));
        remainingLabel_->setText(QStringLiteral("剩余：大小未知"));
        speedLabel_->setText(QStringLiteral("速度：未知"));
    }

    void setTerminalState(const BrowserDownloadCenter::ItemState state,
                          const QString& stateText, const QString& errorText) {
        state_ = state;
        stateLabel_->setText(stateText);
        errorLabel_->setText(errorText);
        chooseButton_->setEnabled(false);
        retryButton_->setEnabled(false);
        retryButton_->hide();
        cancelButton_->setEnabled(false);
    }

    std::uint64_t requestId_{0};
    QString suggestedFileName_;
    BrowserDownloadCenter::ItemState state_{
        BrowserDownloadCenter::ItemState::WaitingForDestination};
    QLabel* originLabel_{nullptr};
    QLabel* fileNameLabel_{nullptr};
    QLabel* sizeLabel_{nullptr};
    QLabel* speedLabel_{nullptr};
    QLabel* remainingLabel_{nullptr};
    QLabel* stateLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QProgressBar* progressBar_{nullptr};
    QPushButton* chooseButton_{nullptr};
    QPushButton* retryButton_{nullptr};
    QPushButton* cancelButton_{nullptr};
    bool hasSubmittedDestination_{false};
    QElapsedTimer progressTimer_;
    std::int64_t lastReceivedBytes_{0};
};

BrowserDownloadCenter::BrowserDownloadCenter(QWidget* const parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("browserDownloadCenter"));
    auto* const rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    auto* const headerLayout = new QHBoxLayout();
    auto* const titleLabel = new QLabel(QStringLiteral("下载中心"), this);
    titleLabel->setObjectName(QStringLiteral("browserDownloadCenterTitleLabel"));
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(
        QStringLiteral("browserDownloadCenterSummaryLabel"));
    clearButton_ = new QPushButton(QStringLiteral("清除已完成"), this);
    clearButton_->setObjectName(
        QStringLiteral("browserDownloadClearCompletedButton"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(summaryLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(clearButton_);
    rootLayout->addLayout(headerLayout);

    auto* const scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(
        QStringLiteral("browserDownloadCenterScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    auto* const content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("browserDownloadCenterContent"));
    itemLayout_ = new QVBoxLayout(content);
    itemLayout_->setContentsMargins(0, 0, 0, 0);
    itemLayout_->setSpacing(8);
    emptyLabel_ = new QLabel(QStringLiteral("暂无下载任务"), content);
    emptyLabel_->setObjectName(
        QStringLiteral("browserDownloadCenterEmptyLabel"));
    emptyLabel_->setAlignment(Qt::AlignCenter);
    itemLayout_->addWidget(emptyLabel_);
    itemLayout_->addStretch();
    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea, 1);

    connect(clearButton_, &QPushButton::clicked, this,
            &BrowserDownloadCenter::clearCompleted);
    refreshSummary();
}

bool BrowserDownloadCenter::beginDownload(
    const std::uint64_t requestId, const QString& origin,
    const QString& suggestedFileName, const std::int64_t totalBytes) {
    if (items_.contains(requestId)) {
        return true;
    }
    if (items_.size() >= kMaximumTrackedItems) {
        return false;
    }

    auto* const item = new BrowserDownloadItem(
        requestId, origin, suggestedFileName, totalBytes, itemLayout_->parentWidget());
    items_.insert(requestId, item);
    itemLayout_->insertWidget(0, item);
    connect(item->chooseButton(), &QPushButton::clicked, this,
            [this, requestId] { chooseDestination(requestId); });
    connect(item->cancelButton(), &QPushButton::clicked, this,
            [this, requestId] { (void)requestCancel(requestId); });
    connect(item->retryButton(), &QPushButton::clicked, this,
            [this, requestId] { (void)requestRetry(requestId); });
    refreshSummary();
    return true;
}

void BrowserDownloadCenter::updateDownload(
    const std::uint64_t requestId, const BrowserDownloadState state,
    const std::int64_t receivedBytes, const std::int64_t totalBytes) {
    BrowserDownloadItem* const item = itemFor(requestId);
    if (item == nullptr) {
        return;
    }
    item->update(state, receivedBytes, totalBytes);
    refreshSummary();
}

bool BrowserDownloadCenter::submitDestination(const std::uint64_t requestId,
                                              const QString& destination) {
    BrowserDownloadItem* const item = itemFor(requestId);
    if (item == nullptr || !item->submitDestination(destination)) {
        return false;
    }
    emit destinationChosen(requestId, QDir::toNativeSeparators(
                                          QDir::cleanPath(destination.trimmed())));
    refreshSummary();
    return true;
}

void BrowserDownloadCenter::completeDestinationSelection(
    const std::uint64_t requestId, const QString& destination) {
    if (destination.isEmpty()) {
        (void)requestCancel(requestId);
        return;
    }
    (void)submitDestination(requestId, destination);
}

bool BrowserDownloadCenter::requestCancel(const std::uint64_t requestId) {
    BrowserDownloadItem* const item = itemFor(requestId);
    if (item == nullptr || !item->markCancelling()) {
        return false;
    }
    emit cancelRequested(requestId);
    refreshSummary();
    return true;
}

bool BrowserDownloadCenter::requestRetry(const std::uint64_t requestId) {
    BrowserDownloadItem* const item = itemFor(requestId);
    if (item == nullptr || !item->markRetrying()) {
        return false;
    }
    emit retryRequested(requestId);
    refreshSummary();
    return true;
}

int BrowserDownloadCenter::clearCompleted() {
    int removedCount = 0;
    for (auto iterator = items_.begin(); iterator != items_.end();) {
        BrowserDownloadItem* const item = iterator.value();
        if (!isTerminalState(item->state())) {
            ++iterator;
            continue;
        }
        iterator = items_.erase(iterator);
        itemLayout_->removeWidget(item);
        delete item;
        ++removedCount;
    }
    refreshSummary();
    return removedCount;
}

int BrowserDownloadCenter::clearForBrowsingData() {
    const int removedCount = items_.size();
    for (BrowserDownloadItem* const item : items_) {
        itemLayout_->removeWidget(item);
        delete item;
    }
    items_.clear();
    refreshSummary();
    return removedCount;
}

int BrowserDownloadCenter::trackedItemCount() const noexcept {
    return items_.size();
}

int BrowserDownloadCenter::activeItemCount() const noexcept {
    int count = 0;
    for (const BrowserDownloadItem* const item : items_) {
        if (!isTerminalState(item->state())) {
            ++count;
        }
    }
    return count;
}

QVector<std::uint64_t> BrowserDownloadCenter::activeRequestIds() const {
    QVector<std::uint64_t> requestIds;
    requestIds.reserve(items_.size());
    for (const BrowserDownloadItem* const item : items_) {
        if (!isTerminalState(item->state())) {
            requestIds.append(item->requestId());
        }
    }
    return requestIds;
}

bool BrowserDownloadCenter::contains(const std::uint64_t requestId) const noexcept {
    return items_.contains(requestId);
}

std::optional<BrowserDownloadCenter::ItemSnapshot>
BrowserDownloadCenter::itemSnapshot(const std::uint64_t requestId) const {
    const BrowserDownloadItem* const item = itemFor(requestId);
    if (item == nullptr) {
        return std::nullopt;
    }
    return item->snapshot();
}

BrowserDownloadItem* BrowserDownloadCenter::itemFor(
    const std::uint64_t requestId) const noexcept {
    const auto iterator = items_.constFind(requestId);
    return iterator == items_.cend() ? nullptr : iterator.value();
}

void BrowserDownloadCenter::chooseDestination(const std::uint64_t requestId) {
    BrowserDownloadItem* const item = itemFor(requestId);
    if (item == nullptr || isTerminalState(item->state()) ||
        item->state() == ItemState::Cancelling) {
        return;
    }
    const QString destination = QFileDialog::getSaveFileName(
        this, QStringLiteral("选择下载保存位置"), item->suggestedFileName());
    completeDestinationSelection(requestId, destination);
}

void BrowserDownloadCenter::refreshSummary() {
    int terminalCount = 0;
    for (const BrowserDownloadItem* const item : items_) {
        if (isTerminalState(item->state())) {
            ++terminalCount;
        }
    }
    summaryLabel_->setText(QStringLiteral("%1 项，%2 项进行中")
                               .arg(items_.size())
                               .arg(items_.size() - terminalCount));
    clearButton_->setEnabled(terminalCount > 0);
    emptyLabel_->setVisible(items_.isEmpty());
}

}  // namespace mediahub::gui
