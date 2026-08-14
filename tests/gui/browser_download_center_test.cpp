#include <QDir>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>

#include "../../apps/gui/browser_download_center.h"

namespace mediahub::gui {
namespace {

class BrowserDownloadCenterTest final : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase();
    void keepsConcurrentRequestsIsolated();
    void ignoresDuplicateAndLateEvents();
    void validatesDestinationWithoutDisplayingPrivatePath();
    void clearsOnlyTerminalItems();
    void enforcesOneHundredItemLimit();
    void retriesCancellationAfterFailure();
    void retriesInterruptedDownloadWhenBackendAllowsResume();
    void hidingCenterDoesNotCancelActiveTasks();
};

void BrowserDownloadCenterTest::initTestCase() {
    qRegisterMetaType<std::uint64_t>("std::uint64_t");
}

void BrowserDownloadCenterTest::keepsConcurrentRequestsIsolated() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BrowserDownloadCenter center;
    QSignalSpy destinationSpy(&center,
                              &BrowserDownloadCenter::destinationChosen);

    QVERIFY(center.beginDownload(
        11, QStringLiteral("https://alice:secret@example.com/private?q=token"),
        QStringLiteral("folder/first.bin"), 8'000'000'000'000'000'000LL));
    QVERIFY(center.beginDownload(22, QStringLiteral("https://second.example/path"),
                                 QStringLiteral("second.bin"), -1));
    QVERIFY(center.submitDestination(
        11, directory.filePath(QStringLiteral("selected-first.bin"))));
    QVERIFY(center.submitDestination(
        22, directory.filePath(QStringLiteral("selected-second.bin"))));
    QCOMPARE(destinationSpy.count(), 2);

    center.updateDownload(11, BrowserDownloadState::InProgress,
                          6'000'000'000'000'000'000LL,
                          8'000'000'000'000'000'000LL);
    const auto first = center.itemSnapshot(11);
    const auto second = center.itemSnapshot(22);
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QCOMPARE(first->progressValue, 75);
    QCOMPARE(first->state, BrowserDownloadCenter::ItemState::InProgress);
    QVERIFY(first->remainingText.contains(QStringLiteral("剩余")));
    QVERIFY(first->speedText.contains(QStringLiteral("速度")));
    QCOMPARE(second->progressValue, 0);
    QCOMPARE(second->state, BrowserDownloadCenter::ItemState::InProgress);
    QCOMPARE(first->originText, QStringLiteral("https://example.com"));
    QCOMPARE(first->fileNameText, QStringLiteral("first.bin"));
}

void BrowserDownloadCenterTest::ignoresDuplicateAndLateEvents() {
    BrowserDownloadCenter center;
    QVERIFY(center.beginDownload(7, QStringLiteral("https://example.com"),
                                 QStringLiteral("original.bin"), 100));
    center.updateDownload(7, BrowserDownloadState::InProgress, 40, 100);
    QVERIFY(center.beginDownload(7, QStringLiteral("https://changed.example"),
                                 QStringLiteral("changed.bin"), 1));

    auto snapshot = center.itemSnapshot(7);
    QVERIFY(snapshot.has_value());
    QCOMPARE(center.trackedItemCount(), 1);
    QCOMPARE(snapshot->fileNameText, QStringLiteral("original.bin"));
    QCOMPARE(snapshot->progressValue, 40);

    center.updateDownload(7, BrowserDownloadState::Completed, 100, 100);
    center.updateDownload(7, BrowserDownloadState::Failed, 0, 100);
    snapshot = center.itemSnapshot(7);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->state, BrowserDownloadCenter::ItemState::Completed);
    QCOMPARE(snapshot->progressValue, 100);

    center.updateDownload(999, BrowserDownloadState::InProgress, 10, 20);
    QCOMPARE(center.trackedItemCount(), 1);
    QCOMPARE(center.clearCompleted(), 1);
    center.updateDownload(7, BrowserDownloadState::InProgress, 1, 2);
    QVERIFY(!center.contains(7));
}

void BrowserDownloadCenterTest::validatesDestinationWithoutDisplayingPrivatePath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BrowserDownloadCenter center;
    QSignalSpy destinationSpy(&center,
                              &BrowserDownloadCenter::destinationChosen);
    QVERIFY(center.beginDownload(31, QStringLiteral("not a site"),
                                 QStringLiteral("C:/private/source.bin"), 10));

    QVERIFY(!center.submitDestination(31, QStringLiteral("relative.bin")));
    QCOMPARE(center.itemSnapshot(31)->errorText,
             QStringLiteral("保存位置必须是绝对路径"));
    QVERIFY(!center.submitDestination(
        31, directory.filePath(QStringLiteral("missing/file.bin"))));
    QCOMPARE(center.itemSnapshot(31)->errorText,
             QStringLiteral("保存文件夹不存在"));
    QVERIFY(!center.submitDestination(
        31, directory.filePath(QStringLiteral("CON.txt"))));
    QCOMPARE(center.itemSnapshot(31)->errorText,
             QStringLiteral("文件名不可接受，请选择新名称"));
    QVERIFY(!center.submitDestination(31, directory.path()));
    QCOMPARE(center.itemSnapshot(31)->errorText,
             QStringLiteral("目标不能是文件夹"));

    const QString existingPath = directory.filePath(QStringLiteral("exists.bin"));
    QFile existingFile(existingPath);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    existingFile.close();
    QVERIFY(!center.submitDestination(31, existingPath));
    QCOMPARE(center.itemSnapshot(31)->errorText,
             QStringLiteral("目标文件已存在，请选择新名称"));

    const QString destination =
        directory.filePath(QStringLiteral("private-target.bin"));
    QVERIFY(center.submitDestination(31, destination));
    QCOMPARE(destinationSpy.count(), 1);
    QCOMPARE(destinationSpy.at(0).at(0).toULongLong(), qulonglong{31});
    QCOMPARE(destinationSpy.at(0).at(1).toString(),
             QDir::toNativeSeparators(destination));
    const auto snapshot = center.itemSnapshot(31);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->originText, QStringLiteral("来源未知"));
    QCOMPARE(snapshot->fileNameText, QStringLiteral("source.bin"));
    QVERIFY(!snapshot->originText.contains(directory.path()));
    QVERIFY(!snapshot->fileNameText.contains(directory.path()));
    QVERIFY(!snapshot->sizeText.contains(directory.path()));
    QVERIFY(!snapshot->stateText.contains(directory.path()));
    QVERIFY(!snapshot->errorText.contains(directory.path()));
}

void BrowserDownloadCenterTest::clearsOnlyTerminalItems() {
    BrowserDownloadCenter center;
    for (std::uint64_t requestId = 1; requestId <= 4; ++requestId) {
        QVERIFY(center.beginDownload(requestId, QStringLiteral("https://example.com"),
                                     QStringLiteral("file.bin"), 100));
    }
    center.updateDownload(1, BrowserDownloadState::Completed, 100, 100);
    center.updateDownload(2, BrowserDownloadState::Failed, 20, 100);
    center.updateDownload(3, BrowserDownloadState::Cancelled, 0, 100);
    center.updateDownload(4, BrowserDownloadState::InProgress, 50, 100);

    QCOMPARE(center.clearCompleted(), 3);
    QCOMPARE(center.trackedItemCount(), 1);
    QCOMPARE(center.activeItemCount(), 1);
    QVERIFY(center.contains(4));
}

void BrowserDownloadCenterTest::enforcesOneHundredItemLimit() {
    BrowserDownloadCenter center;
    for (int index = 0; index < BrowserDownloadCenter::kMaximumTrackedItems;
         ++index) {
        QVERIFY(center.beginDownload(
            static_cast<std::uint64_t>(index + 1),
            QStringLiteral("https://example.com"),
            QStringLiteral("file-%1.bin").arg(index), 100));
    }
    QCOMPARE(center.trackedItemCount(),
             BrowserDownloadCenter::kMaximumTrackedItems);
    QVERIFY(center.beginDownload(1, QStringLiteral("https://duplicate.example"),
                                 QStringLiteral("duplicate.bin"), 1));
    QVERIFY(!center.beginDownload(101, QStringLiteral("https://example.com"),
                                  QStringLiteral("overflow.bin"), 100));
    QVERIFY(!center.contains(101));
}

void BrowserDownloadCenterTest::retriesCancellationAfterFailure() {
    BrowserDownloadCenter center;
    QSignalSpy cancelSpy(&center, &BrowserDownloadCenter::cancelRequested);
    QVERIFY(center.beginDownload(51, QStringLiteral("https://example.com"),
                                 QStringLiteral("file.bin"), 100));

    QVERIFY(center.requestCancel(51));
    QCOMPARE(cancelSpy.count(), 1);
    QVERIFY(!center.requestCancel(51));
    center.updateDownload(51, BrowserDownloadState::InProgress, 20, 100);
    QCOMPARE(center.itemSnapshot(51)->state,
             BrowserDownloadCenter::ItemState::Cancelling);

    center.updateDownload(51, BrowserDownloadState::CancelFailed, 25, 100);
    auto snapshot = center.itemSnapshot(51);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->state, BrowserDownloadCenter::ItemState::CancelFailed);
    QVERIFY(snapshot->errorText.contains(QStringLiteral("可再次尝试")));
    QVERIFY(center.requestCancel(51));
    QCOMPARE(cancelSpy.count(), 2);

    center.updateDownload(51, BrowserDownloadState::Cancelled, 25, 100);
    QCOMPARE(center.itemSnapshot(51)->state,
             BrowserDownloadCenter::ItemState::Cancelled);
    QVERIFY(!center.requestCancel(51));
    QCOMPARE(cancelSpy.count(), 2);
}

void BrowserDownloadCenterTest::retriesInterruptedDownloadWhenBackendAllowsResume() {
    BrowserDownloadCenter center;
    QSignalSpy retrySpy(&center, &BrowserDownloadCenter::retryRequested);
    QVERIFY(center.beginDownload(57, QStringLiteral("https://example.com"),
                                 QStringLiteral("large-file.bin"), 100));

    center.updateDownload(57, BrowserDownloadState::RetryableFailure, 40, 100);
    auto snapshot = center.itemSnapshot(57);
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->state,
             BrowserDownloadCenter::ItemState::RetryableFailure);
    QVERIFY(snapshot->stateText.contains(QStringLiteral("可继续")));
    QCOMPARE(center.activeItemCount(), 1);

    QVERIFY(center.requestRetry(57));
    QCOMPARE(retrySpy.count(), 1);
    QVERIFY(!center.requestRetry(57));
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(center.itemSnapshot(57)->state,
             BrowserDownloadCenter::ItemState::InProgress);

    center.updateDownload(57, BrowserDownloadState::InProgress, 55, 100);
    QCOMPARE(center.itemSnapshot(57)->progressValue, 55);
    center.updateDownload(57, BrowserDownloadState::RetryableFailure, 60, 100);
    QVERIFY(center.requestRetry(57));
    QCOMPARE(retrySpy.count(), 2);

    center.updateDownload(57, BrowserDownloadState::Failed, 60, 100);
    QCOMPARE(center.itemSnapshot(57)->state,
             BrowserDownloadCenter::ItemState::Failed);
    QCOMPARE(center.activeItemCount(), 0);
    QVERIFY(!center.requestRetry(57));
    QCOMPARE(retrySpy.count(), 2);
}

void BrowserDownloadCenterTest::hidingCenterDoesNotCancelActiveTasks() {
    BrowserDownloadCenter center;
    QSignalSpy cancelSpy(&center, &BrowserDownloadCenter::cancelRequested);
    QVERIFY(center.beginDownload(61, QStringLiteral("https://example.com"),
                                 QStringLiteral("file.bin"), 100));
    center.hide();
    center.updateDownload(61, BrowserDownloadState::InProgress, 60, 100);

    QCOMPARE(cancelSpy.count(), 0);
    QCOMPARE(center.activeItemCount(), 1);
    QCOMPARE(center.itemSnapshot(61)->progressValue, 60);
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserDownloadCenterTest)

#include "browser_download_center_test.moc"
