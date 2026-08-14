#include "browser_download_widget.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace mediahub::gui {

BrowserDownloadWidget::BrowserDownloadWidget(QWidget* const parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("browserDownloadWidget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);

    auto* details = new QHBoxLayout();
    originLabel_ = new QLabel(this);
    originLabel_->setObjectName(QStringLiteral("browserDownloadOriginLabel"));
    fileNameLabel_ = new QLabel(this);
    fileNameLabel_->setObjectName(QStringLiteral("browserDownloadFileNameLabel"));
    sizeLabel_ = new QLabel(this);
    sizeLabel_->setObjectName(QStringLiteral("browserDownloadSizeLabel"));
    stateLabel_ = new QLabel(this);
    stateLabel_->setObjectName(QStringLiteral("browserDownloadStateLabel"));
    details->addWidget(originLabel_);
    details->addWidget(fileNameLabel_);
    details->addWidget(sizeLabel_);
    details->addStretch();
    details->addWidget(stateLabel_);
    layout->addLayout(details);

    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName(QStringLiteral("browserDownloadProgressBar"));
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    layout->addWidget(progressBar_);

    auto* actionRow = new QHBoxLayout();
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("browserDownloadErrorLabel"));
    errorLabel_->setWordWrap(true);
    chooseButton_ = new QPushButton(QStringLiteral("选择保存位置"), this);
    chooseButton_->setObjectName(QStringLiteral("browserDownloadChooseButton"));
    cancelButton_ = new QPushButton(QStringLiteral("取消下载"), this);
    cancelButton_->setObjectName(QStringLiteral("browserDownloadCancelButton"));
    actionRow->addWidget(errorLabel_, 1);
    actionRow->addWidget(chooseButton_);
    actionRow->addWidget(cancelButton_);
    layout->addLayout(actionRow);

    connect(chooseButton_, &QPushButton::clicked, this,
            &BrowserDownloadWidget::chooseDestination);
    connect(cancelButton_, &QPushButton::clicked, this,
            &BrowserDownloadWidget::requestCancel);
    hide();
}

void BrowserDownloadWidget::beginDownload(const std::uint64_t requestId,
                                          const QString& origin,
                                          const QString& suggestedFileName,
                                          const std::int64_t totalBytes) {
    requestId_ = requestId;
    suggestedFileName_ = suggestedFileName;
    isActive_ = true;
    isDestinationSubmitted_ = false;
    isCancelSent_ = false;
    isTerminal_ = false;
    originLabel_->setText(origin);
    fileNameLabel_->setText(suggestedFileName);
    sizeLabel_->setText(formatBytes(totalBytes));
    stateLabel_->setText(QStringLiteral("等待选择保存位置"));
    errorLabel_->clear();
    progressBar_->setValue(0);
    progressBar_->setRange(totalBytes > 0 ? 0 : 0, totalBytes > 0 ? 100 : 0);
    chooseButton_->setEnabled(true);
    cancelButton_->setEnabled(true);
    show();
}

void BrowserDownloadWidget::updateDownload(const std::uint64_t requestId,
                                           const BrowserDownloadState state,
                                           const std::int64_t receivedBytes,
                                           const std::int64_t totalBytes) {
    if (!isActive_ || requestId != requestId_) {
        return;
    }
    if (totalBytes > 0) {
        progressBar_->setRange(0, 100);
        const long double ratio =
            static_cast<long double>(receivedBytes) /
            static_cast<long double>(totalBytes);
        const auto percent = static_cast<int>(
            qBound<long double>(0.0L, ratio * 100.0L, 100.0L));
        progressBar_->setValue(percent);
        sizeLabel_->setText(QStringLiteral("%1 / %2")
                                .arg(formatBytes(receivedBytes),
                                     formatBytes(totalBytes)));
    } else {
        progressBar_->setRange(0, 0);
        sizeLabel_->setText(formatBytes(receivedBytes));
    }

    switch (state) {
    case BrowserDownloadState::InProgress:
        if (!isCancelSent_) {
            stateLabel_->setText(QStringLiteral("下载中"));
        }
        break;
    case BrowserDownloadState::CancelFailed:
        stateLabel_->setText(QStringLiteral("取消失败，可重试"));
        isCancelSent_ = false;
        cancelButton_->setEnabled(true);
        break;
    case BrowserDownloadState::RetryableFailure:
        stateLabel_->setText(QStringLiteral("下载中断"));
        isCancelSent_ = false;
        cancelButton_->setEnabled(true);
        break;
    case BrowserDownloadState::Completed:
        stateLabel_->setText(QStringLiteral("下载完成"));
        isTerminal_ = true;
        break;
    case BrowserDownloadState::Failed:
        stateLabel_->setText(QStringLiteral("下载失败"));
        isTerminal_ = true;
        break;
    case BrowserDownloadState::Cancelled:
        stateLabel_->setText(QStringLiteral("下载已取消"));
        isTerminal_ = true;
        break;
    }
    if (isTerminal_) {
        chooseButton_->setEnabled(false);
        cancelButton_->setEnabled(false);
    }
}

void BrowserDownloadWidget::submitDestination(const QString& destination) {
    if (!isActive_ || isDestinationSubmitted_ || isCancelSent_ || isTerminal_) {
        return;
    }
    const QString cleaned = QDir::cleanPath(destination.trimmed());
    if (destination.trimmed().isEmpty()) {
        showPathError(QStringLiteral("请选择保存文件"));
        return;
    }
    const QFileInfo destinationInfo(cleaned);
    if (!destinationInfo.isAbsolute()) {
        showPathError(QStringLiteral("保存位置必须是绝对路径"));
        return;
    }
    if (destinationInfo.exists()) {
        showPathError(destinationInfo.isDir()
                          ? QStringLiteral("目标不能是文件夹")
                          : QStringLiteral("目标文件已存在，请选择新名称"));
        return;
    }
    if (!isAcceptableFileName(destinationInfo.fileName())) {
        showPathError(QStringLiteral("文件名不可接受，请选择新名称"));
        return;
    }
    const QFileInfo parentInfo(destinationInfo.absolutePath());
    if (!parentInfo.exists() || !parentInfo.isDir()) {
        showPathError(QStringLiteral("保存文件夹不存在"));
        return;
    }

    errorLabel_->clear();
    stateLabel_->setText(QStringLiteral("等待下载开始"));
    isDestinationSubmitted_ = true;
    chooseButton_->setEnabled(false);
    emit destinationChosen(requestId_, QDir::toNativeSeparators(cleaned));
}

void BrowserDownloadWidget::completeDestinationSelection(
    const QString& destination) {
    if (destination.isEmpty()) {
        requestCancel();
        return;
    }
    submitDestination(destination);
}

QString BrowserDownloadWidget::originText() const { return originLabel_->text(); }
QString BrowserDownloadWidget::fileNameText() const { return fileNameLabel_->text(); }
QString BrowserDownloadWidget::sizeText() const { return sizeLabel_->text(); }
QString BrowserDownloadWidget::stateText() const { return stateLabel_->text(); }
QString BrowserDownloadWidget::errorText() const { return errorLabel_->text(); }
int BrowserDownloadWidget::progressValue() const noexcept {
    return progressBar_->value();
}
bool BrowserDownloadWidget::isTerminal() const noexcept { return isTerminal_; }
bool BrowserDownloadWidget::hasSubmittedDestination() const noexcept {
    return isDestinationSubmitted_;
}
std::uint64_t BrowserDownloadWidget::requestId() const noexcept { return requestId_; }

void BrowserDownloadWidget::chooseDestination() {
    if (!isActive_ || isDestinationSubmitted_ || isCancelSent_ || isTerminal_) {
        return;
    }
    const QString destination = QFileDialog::getSaveFileName(
        this, QStringLiteral("选择下载保存位置"), suggestedFileName_);
    completeDestinationSelection(destination);
}

void BrowserDownloadWidget::requestCancel() {
    if (!isActive_ || isCancelSent_ || isTerminal_) {
        return;
    }
    isCancelSent_ = true;
    stateLabel_->setText(QStringLiteral("正在取消下载..."));
    chooseButton_->setEnabled(false);
    cancelButton_->setEnabled(false);
    emit cancelRequested(requestId_);
}

void BrowserDownloadWidget::showPathError(const QString& message) {
    errorLabel_->setText(message);
}

bool BrowserDownloadWidget::isAcceptableFileName(const QString& fileName) {
    if (fileName.isEmpty() || fileName.endsWith(QLatin1Char('.')) ||
        fileName.endsWith(QLatin1Char(' '))) {
        return false;
    }
    static const QRegularExpression invalidCharacters(
        QStringLiteral(R"([<>:"/\\|?*])"));
    if (fileName.contains(invalidCharacters)) {
        return false;
    }
    const QString stem = fileName.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QRegularExpression reservedName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
    return !reservedName.match(stem).hasMatch();
}

QString BrowserDownloadWidget::formatBytes(const std::int64_t bytes) {
    if (bytes < 0) {
        return QStringLiteral("大小未知");
    }
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB")
            .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB")
            .arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

}  // namespace mediahub::gui
