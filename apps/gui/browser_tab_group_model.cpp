#include "browser_tab_group_model.h"

#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace mediahub::gui {
namespace {

constexpr int kMaximumGroupCount = 20;
constexpr int kMaximumGroupNameLength = 64;

}  // namespace

const QVector<BrowserSessionGroup>& BrowserTabGroupModel::groups() const noexcept {
    return groups_;
}

void BrowserTabGroupModel::replace(const QVector<BrowserSessionGroup>& groups) {
    groups_.clear();
    groups_.reserve(std::min(groups.size(), kMaximumGroupCount));
    for (const BrowserSessionGroup& group : groups) {
        const QString id = group.id.trimmed();
        const QString name = normalizeName(group.name);
        const QString color = normalizeColor(group.color);
        if (id.isEmpty() || name.isEmpty() || color.isEmpty() || find(id) != nullptr) {
            continue;
        }
        groups_.append(BrowserSessionGroup{id, name, color, group.isCollapsed});
        if (groups_.size() == kMaximumGroupCount) {
            break;
        }
    }
}

std::optional<QString> BrowserTabGroupModel::create(const QString& name,
                                                    const QString& color) {
    const QString normalizedName = normalizeName(name);
    const QString normalizedColor = normalizeColor(color);
    if (groups_.size() >= kMaximumGroupCount || normalizedName.isEmpty() ||
        normalizedColor.isEmpty()) {
        return std::nullopt;
    }
    QString id;
    do {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } while (find(id) != nullptr);
    groups_.append(BrowserSessionGroup{id, normalizedName, normalizedColor, false});
    return id;
}

bool BrowserTabGroupModel::rename(const QString& id, const QString& name) {
    const int index = findIndex(id);
    const QString normalizedName = normalizeName(name);
    if (index < 0 || normalizedName.isEmpty()) {
        return false;
    }
    groups_[index].name = normalizedName;
    return true;
}

bool BrowserTabGroupModel::setColor(const QString& id, const QString& color) {
    const int index = findIndex(id);
    const QString normalizedColor = normalizeColor(color);
    if (index < 0 || normalizedColor.isEmpty()) {
        return false;
    }
    groups_[index].color = normalizedColor;
    return true;
}

bool BrowserTabGroupModel::setCollapsed(const QString& id,
                                        const bool isCollapsed) {
    const int index = findIndex(id);
    if (index < 0) {
        return false;
    }
    groups_[index].isCollapsed = isCollapsed;
    return true;
}

bool BrowserTabGroupModel::remove(const QString& id) {
    const int index = findIndex(id);
    if (index < 0) {
        return false;
    }
    groups_.removeAt(index);
    return true;
}

const BrowserSessionGroup* BrowserTabGroupModel::find(const QString& id) const {
    const int index = findIndex(id);
    return index < 0 ? nullptr : &groups_.at(index);
}

QString BrowserTabGroupModel::normalizeName(const QString& value) {
    return value.simplified().left(kMaximumGroupNameLength);
}

QString BrowserTabGroupModel::normalizeColor(const QString& value) {
    static const QRegularExpression kColorPattern(
        QStringLiteral("^#[0-9a-fA-F]{6}$"));
    const QString normalized = value.trimmed().toLower();
    return kColorPattern.match(normalized).hasMatch() ? normalized : QString{};
}

int BrowserTabGroupModel::findIndex(const QString& id) const noexcept {
    const QString normalizedId = id.trimmed();
    for (int index = 0; index < groups_.size(); ++index) {
        if (groups_.at(index).id == normalizedId) {
            return index;
        }
    }
    return -1;
}

}  // namespace mediahub::gui
