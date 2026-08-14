#include <QTest>

#include "browser_bookmark_html.h"

namespace mediahub::gui {
namespace {

class BrowserBookmarkHtmlTest final : public QObject {
    Q_OBJECT

 private slots:
    void importsCommonHtmlAndFiltersUnsafeEntries();
    void rejectsOversizedInputAndCapsEntryCount();
    void exportsEscapedNormalizedHtmlAndRoundTripsNotes();
};

void BrowserBookmarkHtmlTest::importsCommonHtmlAndFiltersUnsafeEntries() {
    const QByteArray html = R"HTML(
        <!DOCTYPE NETSCAPE-Bookmark-file-1>
        <DL><p>
          <DT><A HREF="https://Example.com/path?token=secret#part">A &amp; B</A>
          <DD>第一条 <b>备注</b>
          <DT><A HREF='javascript:alert(1)'>危险地址</A>
          <DT><A HREF="https://user:pass@example.com/private">带凭据地址</A>
          <DT><A HREF="https://example.com/path">重复地址</A>
          <DT><A HREF=https://two.example/a>第二条</A>
        </DL><p>
    )HTML";

    const BrowserBookmarkImportResult result = importBrowserBookmarksHtml(html);

    QVERIFY(!result.isInputTooLarge);
    QCOMPARE(result.favorites.size(), 2);
    QCOMPARE(result.rejectedEntries, 3);
    QCOMPARE(result.favorites.at(0).url,
             QStringLiteral("https://example.com/path"));
    QCOMPARE(result.favorites.at(0).title, QStringLiteral("A & B"));
    QCOMPARE(result.favorites.at(0).note, QStringLiteral("第一条 备注"));
    QCOMPARE(result.favorites.at(1).url,
             QStringLiteral("https://two.example/a"));
}

void BrowserBookmarkHtmlTest::rejectsOversizedInputAndCapsEntryCount() {
    const BrowserBookmarkImportResult oversized = importBrowserBookmarksHtml(
        QByteArray(8 * 1024 * 1024 + 1, 'x'));
    QVERIFY(oversized.isInputTooLarge);
    QVERIFY(oversized.favorites.isEmpty());

    QByteArray html("<DL>");
    for (int index = 0; index < 5002; ++index) {
        html += "<DT><A HREF=\"https://example.com/" +
                QByteArray::number(index) + "\">item</A>";
    }
    html += "</DL>";
    const BrowserBookmarkImportResult capped = importBrowserBookmarksHtml(html);
    QCOMPARE(capped.favorites.size(), 5000);
    QCOMPARE(capped.rejectedEntries, 2);
}

void BrowserBookmarkHtmlTest::exportsEscapedNormalizedHtmlAndRoundTripsNotes() {
    const QByteArray html = exportBrowserBookmarksHtml({
        {QStringLiteral("https://Example.com/a?secret=value#part"),
         QStringLiteral("A & <B>"), QStringLiteral("备注 & 说明")},
        {QStringLiteral("file:///C:/private.txt"), QStringLiteral("本地"), {}},
    });

    QVERIFY(html.contains("HREF=\"https://example.com/a\""));
    QVERIFY(html.contains("A &amp; &lt;B&gt;"));
    QVERIFY(html.contains("备注 &amp; 说明"));
    QVERIFY(!html.contains("secret"));
    QVERIFY(!html.contains("private.txt"));

    const BrowserBookmarkImportResult imported = importBrowserBookmarksHtml(html);
    QCOMPARE(imported.favorites.size(), 1);
    QCOMPARE(imported.favorites.at(0).title, QStringLiteral("A & <B>"));
    QCOMPARE(imported.favorites.at(0).note, QStringLiteral("备注 & 说明"));
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserBookmarkHtmlTest)

#include "browser_bookmark_html_test.moc"
