#pragma once

#include <QFrame>
#include <QHash>

class QLabel;
class QResizeEvent;
class QStackedWidget;
class QToolButton;

namespace mediahub::gui {

// 在网页原生宿主右侧互斥承载浏览器辅助页面。
class BrowserSidePanel final : public QFrame {
    Q_OBJECT

 public:
    enum class Page {
        History,
        Favorites,
        Downloads,
        Audio,
        TabSearch,
        Groups,
    };
    Q_ENUM(Page)

    explicit BrowserSidePanel(QWidget* parent = nullptr);

    // 调用线程：GUI 主线程。页面所有权转移给侧边面板。
    void addPage(Page page, QWidget* widget, const QString& title);
    // 调用线程：GUI 主线程。只切换 Qt 布局，不改变网页和下载后台状态。
    void showPage(Page page);
    void closePanel();
    void setCompact(bool isCompact);

    [[nodiscard]] bool containsPage(Page page) const noexcept;
    [[nodiscard]] Page currentPage() const noexcept;
    [[nodiscard]] QWidget* pageWidget(Page page) const noexcept;

 signals:
    void panelClosed();
    void pageChanged(Page page);

 private:
    struct PageEntry {
        QWidget* widget{nullptr};
        QString title;
    };

    QHash<int, PageEntry> pages_;
    QStackedWidget* stack_{nullptr};
    QLabel* titleLabel_{nullptr};
    QToolButton* closeButton_{nullptr};
    Page currentPage_{Page::History};

    void activateParentLayout();
};

}  // namespace mediahub::gui
